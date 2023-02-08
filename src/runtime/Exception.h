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

#ifndef __WalrusException__
#define __WalrusException__

#include "runtime/ExecutionState.h"
#include "util/Vector.h"

namespace Walrus {

class String;
class Tag;

class Exception : public gc {
public:
    static Exception* create(String* m)
    {
        return new Exception(m);
    }

    static Exception* create(ExecutionState& state, String* m)
    {
        return new Exception(state, m);
    }

    static Exception* create(ExecutionState& state, Tag* tag, Vector<uint8_t, GCUtil::gc_malloc_atomic_allocator<uint8_t>>&& userExceptionData)
    {
        return new Exception(state, tag, std::move(userExceptionData));
    }

    bool isBuiltinException() const
    {
        return !!message();
    }

    bool isUserException() const
    {
        return !!tag();
    }

    Optional<String*> message() const
    {
        return m_message;
    }

    Optional<Tag*> tag() const
    {
        return m_tag;
    }

    const Vector<uint8_t, GCUtil::gc_malloc_atomic_allocator<uint8_t>>& userExceptionData() const
    {
        return m_userExceptionData;
    }

private:
    friend class Interpreter;
    Exception(String* message)
    {
        m_message = message;
    }

    Exception(ExecutionState& state);
    Exception(ExecutionState& state, String* message)
        : Exception(state)
    {
        m_message = message;
    }

    Exception(ExecutionState& state, Tag* tag, Vector<uint8_t, GCUtil::gc_malloc_atomic_allocator<uint8_t>>&& userExceptionData)
        : Exception(state)
    {
        m_tag = tag;
        m_userExceptionData = std::move(userExceptionData);
    }

    Optional<String*> m_message;
    Optional<Tag*> m_tag;
    Vector<uint8_t, GCUtil::gc_malloc_atomic_allocator<uint8_t>> m_userExceptionData;
    Vector<std::pair<ExecutionState*, size_t>, GCUtil::gc_malloc_atomic_allocator<std::pair<ExecutionState*, size_t>>> m_programCounterInfo;
};

} // namespace Walrus

#endif // __WalrusException__
