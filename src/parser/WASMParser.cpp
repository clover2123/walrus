/*
 * Copyright (c) 2022-present Samsung Electronics Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "Walrus.h"

#include "parser/WASMParser.h"
#include "interpreter/ByteCode.h"
#include "interpreter/Opcode.h"
#include "runtime/Module.h"

#include "wabt/walrus/binary-reader-walrus.h"

namespace wabt {

static Walrus::Value::Type toValueKindForFunctionType(Type type)
{
    switch (type) {
    case Type::I32:
        return Walrus::Value::Type::I32;
    case Type::I64:
        return Walrus::Value::Type::I64;
    case Type::F32:
        return Walrus::Value::Type::F32;
    case Type::F64:
        return Walrus::Value::Type::F64;
    case Type::FuncRef:
        return Walrus::Value::Type::FuncRef;
    default:
        RELEASE_ASSERT_NOT_REACHED();
    }
}

static Walrus::Value::Type toValueKindForLocalType(Type type)
{
    switch (type) {
    case Type::I32:
        return Walrus::Value::Type::I32;
    case Type::I64:
        return Walrus::Value::Type::I64;
    case Type::F32:
        return Walrus::Value::Type::F32;
    case Type::F64:
        return Walrus::Value::Type::F64;
    default:
        RELEASE_ASSERT_NOT_REACHED();
    }
}

class WASMBinaryReader : public wabt::WASMBinaryReaderDelegate {
public:
    struct BlockInfo {
        enum BlockType {
            IfElse,
            Loop
        };
        BlockType m_blockType;
        Type m_returnValueType;
        size_t m_position;
        union {
            struct {
                size_t m_elsePosition;
            };
        };

        BlockInfo(BlockType type, Type returnValueType)
            : m_blockType(type)
            , m_returnValueType(returnValueType)
            , m_position(0)
            , m_elsePosition(0)
        {
        }
    };

    WASMBinaryReader(Walrus::Module* module)
        : m_module(module)
        , m_currentFunction(nullptr)
        , m_currentFunctionType(nullptr)
        , m_functionStackSizeSoFar(0)
        , m_lastStackTreatSize(0)
    {
    }

    virtual void BeginModule(uint32_t version) override
    {
        m_module->m_version = version;
    }

    virtual void EndModule() override {}

    virtual void OnTypeCount(Index count)
    {
        // TODO reserve vector if possible
    }

    virtual void OnFuncType(Index index,
                            Index paramCount,
                            Type* paramTypes,
                            Index resultCount,
                            Type* resultTypes) override
    {
        Walrus::FunctionType::FunctionTypeVector param;
        param.reserve(paramCount);
        for (size_t i = 0; i < paramCount; i++) {
            param.push_back(toValueKindForFunctionType(paramTypes[i]));
        }
        Walrus::FunctionType::FunctionTypeVector result;
        for (size_t i = 0; i < resultCount; i++) {
            result.push_back(toValueKindForFunctionType(resultTypes[i]));
        }
        m_module->m_functionType.push_back(
            new Walrus::FunctionType(index, std::move(param), std::move(result)));
    }

    virtual void OnImportCount(Index count) override
    {
        m_module->m_import.reserve(count);
    }

    virtual void OnImportFunc(Index importIndex,
                              std::string moduleName,
                              std::string fieldName,
                              Index funcIndex,
                              Index sigIndex) override
    {
        m_module->m_function.push_back(
            new Walrus::ModuleFunction(m_module, funcIndex, sigIndex));
        m_module->m_import.push_back(new Walrus::ModuleImport(
            importIndex, new Walrus::String(moduleName),
            new Walrus::String(fieldName), funcIndex, sigIndex));
    }

    virtual void OnExportCount(Index count)
    {
        m_module->m_export.reserve(count);
    }

    virtual void OnExport(int kind, Index exportIndex, std::string name, Index itemIndex)
    {
        m_module->m_export.pushBack(new Walrus::ModuleExport(static_cast<Walrus::ModuleExport::Type>(kind), new Walrus::String(name), exportIndex, itemIndex));
    }

    virtual void OnFunctionCount(Index count) override
    {
        m_module->m_function.reserve(count);
    }

    virtual void OnFunction(Index index, Index sigIndex) override
    {
        ASSERT(m_currentFunction == nullptr);
        ASSERT(m_currentFunctionType == nullptr);
        m_module->m_function.push_back(new Walrus::ModuleFunction(m_module, index, sigIndex));
    }

    virtual void OnStartFunction(Index funcIndex) override
    {
        m_module->m_seenStartAttribute = true;
        m_module->m_start = funcIndex;
    }

    virtual void BeginFunctionBody(Index index, Offset size) override
    {
        ASSERT(m_currentFunction == nullptr);
        m_currentFunction = m_module->function(index);
        m_currentFunctionType = m_module->functionType(m_currentFunction->functionTypeIndex());
        m_functionStackSizeSoFar = m_currentFunctionType->paramStackSize();
    }

    virtual void OnLocalDeclCount(Index count) override
    {
        m_currentFunction->m_local.reserve(count);
    }

    virtual void OnLocalDecl(Index decl_index, Index count, Type type) override
    {
        while (count) {
            auto wType = toValueKindForLocalType(type);
            m_currentFunction->m_local.pushBack(wType);
            auto sz = Walrus::valueSizeInStack(wType);
            m_functionStackSizeSoFar += sz;
            m_currentFunction->m_requiredStackSizeDueToLocal += sz;
            count--;
        }
    }

    void computeStackSizePerOpcode(size_t stackGrowSize, size_t stackShrinkSize)
    {
        m_functionStackSizeSoFar += stackGrowSize;
        m_currentFunction->m_requiredStackSize = std::max(
            m_currentFunction->m_requiredStackSize, m_functionStackSizeSoFar);
        // TODO check underflow
        m_functionStackSizeSoFar -= stackShrinkSize;
    }

    virtual void OnOpcode(uint32_t opcode) override
    {
        size_t stackGrowSize = Walrus::g_byteCodeInfo[opcode].stackGrowSize();
        size_t stackShrinkSize = Walrus::g_byteCodeInfo[opcode].stackShrinkSize();
        computeStackSizePerOpcode(stackGrowSize, stackShrinkSize);
    }

    virtual void OnCallExpr(uint32_t index) override
    {
        auto functionType = m_module->functionType(m_module->function(index)->functionIndex());

        size_t stackShrinkSize = functionType->paramStackSize();
        size_t stackGrowSize = functionType->resultStackSize();

        m_currentFunction->pushByteCode(Walrus::Call(index));
        computeStackSizePerOpcode(stackGrowSize, stackShrinkSize);
        m_lastStackTreatSize = stackGrowSize;
    }

    virtual void OnI32ConstExpr(uint32_t value) override
    {
        m_currentFunction->pushByteCode(Walrus::I32Const(value));
        m_lastStackTreatSize = Walrus::valueSizeInStack(Walrus::Value::Type::I32);
    }

    virtual void OnI64ConstExpr(uint64_t value) override
    {
        m_currentFunction->pushByteCode(Walrus::I64Const(value));
        m_lastStackTreatSize = Walrus::valueSizeInStack(Walrus::Value::Type::I64);
    }

    virtual void OnF32ConstExpr(uint32_t value) override
    {
        float* f = reinterpret_cast<float*>(&value);
        m_currentFunction->pushByteCode(Walrus::F32Const(*f));
        m_lastStackTreatSize = Walrus::valueSizeInStack(Walrus::Value::Type::F32);
    }

    virtual void OnF64ConstExpr(uint64_t value) override
    {
        double* f = reinterpret_cast<double*>(&value);
        m_currentFunction->pushByteCode(Walrus::F64Const(*f));
        m_lastStackTreatSize = Walrus::valueSizeInStack(Walrus::Value::Type::F64);
    }

    std::pair<uint32_t, uint32_t> resolveLocalOffsetAndSize(Index localIndex)
    {
        if (localIndex < m_currentFunctionType->param().size()) {
            size_t offset = 0;
            for (Index i = 0; i < localIndex; i++) {
                offset += Walrus::valueSizeInStack(m_currentFunctionType->param()[i]);
            }
            return std::make_pair(offset, Walrus::valueSizeInStack(m_currentFunctionType->param()[localIndex]));
        } else {
            localIndex -= m_currentFunctionType->param().size();
            size_t offset = m_currentFunctionType->paramStackSize();
            for (Index i = 0; i < localIndex; i++) {
                offset += Walrus::valueSizeInStack(m_currentFunction->m_local[i]);
            }
            return std::make_pair(offset, Walrus::valueSizeInStack(m_currentFunction->m_local[localIndex]));
        }
    }

    virtual void OnLocalGetExpr(Index localIndex) override
    {
        auto r = resolveLocalOffsetAndSize(localIndex);
        m_currentFunction->pushByteCode(Walrus::LocalGet(r.first, r.second));
        m_lastStackTreatSize = r.second;
        computeStackSizePerOpcode(r.second, 0);
    }

    virtual void OnLocalSetExpr(Index localIndex) override
    {
        auto r = resolveLocalOffsetAndSize(localIndex);
        m_currentFunction->pushByteCode(Walrus::LocalSet(r.first, r.second));
        m_lastStackTreatSize = r.second;
        computeStackSizePerOpcode(0, r.second);
    }

    virtual void OnDropExpr() override
    {
        m_currentFunction->pushByteCode(Walrus::Drop(m_lastStackTreatSize));
        m_lastStackTreatSize = 0;
    }

    virtual void OnBinaryExpr(uint32_t opcode) override
    {
        auto code = static_cast<Walrus::OpcodeKind>(opcode);
        m_currentFunction->pushByteCode(Walrus::BinaryOperation(code));
        m_lastStackTreatSize = Walrus::ByteCodeInfo::byteCodeTypeToMemorySize(Walrus::g_byteCodeInfo[code].m_resultType);
    }

    virtual void OnUnaryExpr(uint32_t opcode) override
    {
        auto code = static_cast<Walrus::OpcodeKind>(opcode);
        m_currentFunction->pushByteCode(Walrus::UnaryOperation(code));
        m_lastStackTreatSize = Walrus::ByteCodeInfo::byteCodeTypeToMemorySize(Walrus::g_byteCodeInfo[code].m_resultType);
    }

    virtual void OnIfExpr(Type sigType) override
    {
        BlockInfo b(BlockInfo::IfElse, sigType);
        b.m_position = m_currentFunction->currentByteCodeSize();
        b.m_elsePosition = 0;
        m_blockInfo.push_back(b);
        m_currentFunction->pushByteCode(Walrus::JumpIfFalse());
    }

    virtual void OnElseExpr() override
    {
        m_currentFunction->pushByteCode(Walrus::Jump());
        BlockInfo& blockInfo = m_blockInfo.back();
        blockInfo.m_elsePosition = m_currentFunction->currentByteCodeSize();
        m_currentFunction->peekByteCode<Walrus::JumpIfFalse>(blockInfo.m_position)
            ->setOffset(m_currentFunction->currentByteCodeSize() - blockInfo.m_position);
    }

    virtual void OnLoopExpr(Type sigType) override
    {
        BlockInfo b(BlockInfo::Loop, sigType);
        b.m_position = m_currentFunction->currentByteCodeSize();
        m_blockInfo.push_back(b);
    }

    BlockInfo& findBlockInfoInBr(Index depth)
    {
        ASSERT(m_blockInfo.size());
        auto iter = m_blockInfo.rbegin();
        while (depth) {
            iter++;
            depth--;
        }
        return *iter;
    }

    size_t dropStackValuesBeforeBrIfNeeds(Index depth)
    {
        size_t dropValueSize = 0;
        auto iter = m_blockInfo.rbegin();
        while (depth) {
            if (iter->m_returnValueType != Type::Void) {
                dropValueSize += Walrus::valueSizeInStack(toValueKindForLocalType(iter->m_returnValueType));
            }
            iter++;
            depth--;
        }

        return dropValueSize;
    }

    virtual void OnBrExpr(Index depth)
    {
        auto& blockInfo = findBlockInfoInBr(depth);
        auto offset = (int32_t)blockInfo.m_position - (int32_t)m_currentFunction->currentByteCodeSize();
        auto dropSize = dropStackValuesBeforeBrIfNeeds(depth);
        if (dropSize) {
            m_currentFunction->pushByteCode(Walrus::Drop(dropSize));
        }
        m_currentFunction->pushByteCode(Walrus::Jump(offset));
    }

    virtual void OnBrIfExpr(Index depth)
    {
        auto& blockInfo = findBlockInfoInBr(depth);
        auto dropSize = dropStackValuesBeforeBrIfNeeds(depth);
        if (dropSize) {
            size_t pos = m_currentFunction->currentByteCodeSize();
            m_currentFunction->pushByteCode(Walrus::JumpIfFalse());
            m_currentFunction->pushByteCode(Walrus::Drop(dropSize));
            auto offset = (int32_t)blockInfo.m_position - (int32_t)m_currentFunction->currentByteCodeSize();
            m_currentFunction->pushByteCode(Walrus::Jump(offset));
            m_currentFunction->peekByteCode<Walrus::JumpIfFalse>(pos)
                ->setOffset(m_currentFunction->currentByteCodeSize() - pos);
        } else {
            auto offset = (int32_t)blockInfo.m_position - (int32_t)m_currentFunction->currentByteCodeSize();
            m_currentFunction->pushByteCode(Walrus::JumpIfTrue(offset));
        }
    }

    virtual void OnEndExpr() override
    {
        if (m_blockInfo.size()) {
            auto blockInfo = m_blockInfo.back();
            m_blockInfo.pop_back();

            if (blockInfo.m_returnValueType != Type::Void) {
                m_lastStackTreatSize = Walrus::valueSizeInStack(toValueKindForLocalType(blockInfo.m_returnValueType));
            } else {
                m_lastStackTreatSize = 0;
            }

            if (blockInfo.m_blockType == BlockInfo::BlockType::IfElse) {
                if (blockInfo.m_elsePosition) {
                    size_t jumpPos = blockInfo.m_elsePosition - sizeof(Walrus::Jump);
                    m_currentFunction->peekByteCode<Walrus::Jump>(jumpPos)->setOffset(
                        m_currentFunction->currentByteCodeSize() - jumpPos);
                } else {
                    m_currentFunction->peekByteCode<Walrus::JumpIfFalse>(blockInfo.m_position)
                        ->setOffset(m_currentFunction->currentByteCodeSize() - blockInfo.m_position);
                }
            }
        } else {
            m_currentFunction->pushByteCode(Walrus::End());
        }
    }

    void EndFunctionBody(Index index) override
    {
#if !defined(NDEBUG)
        if (getenv("DUMP_BYTECODE") && strlen(getenv("DUMP_BYTECODE"))) {
            m_currentFunction->dumpByteCode();
        }
#endif

        ASSERT(m_currentFunction == m_module->function(index));
        m_currentFunction = nullptr;
        m_currentFunctionType = nullptr;
    }

    Walrus::Module* m_module;
    Walrus::ModuleFunction* m_currentFunction;
    Walrus::FunctionType* m_currentFunctionType;
    uint32_t m_functionStackSizeSoFar;
    uint32_t m_lastStackTreatSize;
    std::vector<BlockInfo> m_blockInfo;
};

} // namespace wabt

namespace Walrus {

Optional<Module*> WASMParser::parseBinary(Store* store, const uint8_t* data, size_t len)
{
    Module* module = new Module(store);
    wabt::WASMBinaryReader delegate(module);

    ReadWasmBinary(data, len, &delegate);
    return module;
}

} // namespace Walrus
