SET (WALRUS_INCDIRS
    ${WALRUS_INCDIRS}
    ${WALRUS_ROOT}/src/
    ${GCUTIL_ROOT}/
    ${GCUTIL_ROOT}/bdwgc/include/
)

IF (${WALRUS_MODE} STREQUAL "debug")
    SET (WALRUS_CXXFLAGS ${WALRUS_CXXFLAGS_DEBUG} ${WALRUS_CXXFLAGS})
    SET (WALRUS_LDFLAGS ${WALRUS_LDFLAGS_DEBUG} ${WALRUS_LDFLAGS})
    SET (WALRUS_DEFINITIONS ${WALRUS_DEFINITIONS} ${WALRUS_DEFINITIONS_DEBUG})
ELSEIF (${WALRUS_MODE} STREQUAL "release")
    SET (WALRUS_CXXFLAGS ${WALRUS_CXXFLAGS_RELEASE} ${WALRUS_CXXFLAGS})
    SET (WALRUS_LDFLAGS ${WALRUS_LDFLAGS_RELEASE} ${WALRUS_LDFLAGS})
    SET (WALRUS_DEFINITIONS ${WALRUS_DEFINITIONS} ${WALRUS_DEFINITIONS_RELEASE})
ENDIF()

IF (${WALRUS_OUTPUT} STREQUAL "shared_lib")
    SET (WALRUS_CXXFLAGS ${WALRUS_CXXFLAGS} ${WALRUS_CXXFLAGS_SHAREDLIB})
    SET (WALRUS_LDFLAGS ${WALRUS_LDFLAGS} ${WALRUS_LDFLAGS_SHAREDLIB})
    SET (WALRUS_DEFINITIONS ${WALRUS_DEFINITIONS} ${WALRUS_DEFINITIONS_SHAREDLIB})
ELSEIF (${WALRUS_OUTPUT} STREQUAL "static_lib" OR ${WALRUS_OUTPUT} STREQUAL "shell")
    SET (WALRUS_CXXFLAGS ${WALRUS_CXXFLAGS} ${WALRUS_CXXFLAGS_STATICLIB})
    SET (WALRUS_LDFLAGS ${WALRUS_LDFLAGS} ${WALRUS_LDFLAGS_STATICLIB})
    SET (WALRUS_DEFINITIONS ${WALRUS_DEFINITIONS} ${WALRUS_DEFINITIONS_STATICLIB})
ENDIF()

IF (${WALRUS_ASAN} STREQUAL "1")
    SET (WALRUS_CXXFLAGS ${WALRUS_CXXFLAGS} -fsanitize=address)
    SET (WALRUS_LDFLAGS ${WALRUS_LDFLAGS} -lasan)
ENDIF()


# SOURCE FILES
FILE (GLOB_RECURSE WALRUS_SRC ${WALRUS_ROOT}/src/*.cpp)
LIST (REMOVE_ITEM WALRUS_SRC ${WALRUS_ROOT}/src/shell/Shell.cpp)
LIST (REMOVE_ITEM WALRUS_SRC ${WALRUS_ROOT}/src/api/wasm.cpp)

SET (WALRUS_SRC_LIST
    ${WALRUS_SRC}
)

# GCUTIL
IF (${WALRUS_OUTPUT} STREQUAL "shared_lib")
    SET (WALRUS_THIRDPARTY_CFLAGS ${WALRUS_THIRDPARTY_CFLAGS} ${WALRUS_CXXFLAGS_SHAREDLIB})
ELSEIF (${WALRUS_OUTPUT} STREQUAL "static_lib")
    SET (WALRUS_THIRDPARTY_CFLAGS ${WALRUS_THIRDPARTY_CFLAGS} ${WALRUS_CXXFLAGS_STATICLIB})
ENDIF()

SET (GCUTIL_CFLAGS ${WALRUS_THIRDPARTY_CFLAGS})

IF (WALRUS_SMALL_CONFIG)
    SET (GCUTIL_CFLAGS ${GCUTIL_CFLAGS} -DSMALL_CONFIG -DMAX_HEAP_SECTS=512)
ENDIF()

SET (GCUTIL_MODE ${WALRUS_MODE})

ADD_SUBDIRECTORY (third_party/GCutil)

SET (WALRUS_LIBRARIES ${WALRUS_LIBRARIES} gc-lib)

# wabt
SET (WABT_ARCH ${WALRUS_ARCH})
IF (${WALRUS_MODE} STREQUAL "debug")
    SET (WABT_CXX_FLAGS ${WALRUS_THIRDPARTY_CFLAGS} -O0 -g3)
ELSEIF (${WALRUS_MODE} STREQUAL "release")
    SET (WABT_CXX_FLAGS ${WALRUS_THIRDPARTY_CFLAGS} -O2 -fno-stack-protector)
ENDIF()

SET (WABT_DEFINITIONS ${WALRUS_DEFINITIONS})
ADD_SUBDIRECTORY (third_party/wabt)
SET (WALRUS_LIBRARIES ${WALRUS_LIBRARIES} wabt)

# BUILD
INCLUDE_DIRECTORIES (${WALRUS_INCDIRS})

IF (${WALRUS_OUTPUT} MATCHES "_lib")
    # walrus library using wasm-c-api
    IF (${WALRUS_OUTPUT} STREQUAL "static_lib")
        ADD_LIBRARY (${WALRUS_TARGET} STATIC ${WALRUS_SRC_LIST} ${WALRUS_ROOT}/src/api/wasm.cpp)
    ELSE ()
        ADD_LIBRARY (${WALRUS_TARGET} SHARED ${WALRUS_SRC_LIST} ${WALRUS_ROOT}/src/api/wasm.cpp)
    ENDIF()

    TARGET_LINK_LIBRARIES (${WALRUS_TARGET} PRIVATE ${WALRUS_LIBRARIES} ${WALRUS_LDFLAGS} ${LDFLAGS_FROM_ENV})
    TARGET_INCLUDE_DIRECTORIES (${WALRUS_TARGET} PUBLIC ${WALRUS_ROOT}/src/api/)
    TARGET_COMPILE_DEFINITIONS (${WALRUS_TARGET} PRIVATE ${WALRUS_DEFINITIONS} "WASM_API_EXTERN=__attribute__((visibility(\"default\")))")
    TARGET_COMPILE_OPTIONS (${WALRUS_TARGET} PRIVATE ${WALRUS_CXXFLAGS} ${CXXFLAGS_FROM_ENV})
ELSEIF (${WALRUS_OUTPUT} MATCHES "shell")
    ADD_EXECUTABLE (${WALRUS_TARGET} ${WALRUS_SRC_LIST} ${WALRUS_ROOT}/src/shell/Shell.cpp)

    TARGET_LINK_LIBRARIES (${WALRUS_TARGET} PRIVATE ${WALRUS_LIBRARIES} ${WALRUS_LDFLAGS} ${LDFLAGS_FROM_ENV})
    TARGET_COMPILE_DEFINITIONS (${WALRUS_TARGET} PRIVATE ${WALRUS_DEFINITIONS})
    TARGET_COMPILE_OPTIONS (${WALRUS_TARGET} PRIVATE ${WALRUS_CXXFLAGS} ${WALRUS_CXXFLAGS_SHELL} ${CXXFLAGS_FROM_ENV} ${PROFILER_FLAGS})
ELSEIF (${WALRUS_OUTPUT} STREQUAL "api_test")
   # BUILD WASM API TESTS
    ADD_LIBRARY (${WALRUS_TARGET} STATIC ${WALRUS_SRC_LIST} ${WALRUS_ROOT}/src/api/wasm.cpp)

    TARGET_LINK_LIBRARIES (${WALRUS_TARGET} PRIVATE ${WALRUS_LIBRARIES} ${WALRUS_LDFLAGS} ${LDFLAGS_FROM_ENV})
    TARGET_INCLUDE_DIRECTORIES (${WALRUS_TARGET} PUBLIC ${WALRUS_ROOT}/src/api/)
    TARGET_COMPILE_DEFINITIONS (${WALRUS_TARGET} PRIVATE ${WALRUS_DEFINITIONS} "WASM_API_EXTERN=__attribute__((visibility(\"default\")))")
    TARGET_COMPILE_OPTIONS (${WALRUS_TARGET} PRIVATE ${WALRUS_CXXFLAGS} ${CXXFLAGS_FROM_ENV})

    function(c_api_example NAME)
        set(EXENAME wasm-c-api-${NAME})
        add_executable(${EXENAME} ${WALRUS_THIRD_PARTY_ROOT}/wasm-c-api/example/${NAME}.c)
        if (COMPILER_IS_MSVC)
            set_target_properties(${EXENAME} PROPERTIES COMPILE_FLAGS "-wd4311")
        else ()
            set_target_properties(${EXENAME} PROPERTIES COMPILE_FLAGS "-std=gnu11 -Wno-pointer-to-int-cast")
        endif ()

        target_link_libraries(${EXENAME} ${WALRUS_TARGET})
    endfunction()

#c_api_example(callback)
#c_api_example(finalize)
#c_api_example(global)
    c_api_example(hello)
#c_api_example(hostref)
#c_api_example(multi)
#c_api_example(memory)
#c_api_example(reflect)
#c_api_example(serialize)
#c_api_example(start)
#c_api_example(table)
#c_api_example(trap)
#c_api_example(threads)
ENDIF()
