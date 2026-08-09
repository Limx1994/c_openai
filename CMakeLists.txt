cmake_minimum_required(VERSION 3.18)
project(c_openai VERSION 0.0.1 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE MinSizeRel CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release MinSizeRel RelWithDebInfo)
endif()

option(OPENAI_LOG_ENABLED "Enable logging (0/1)" 0)
set(OPENAI_LOG_LEVEL 0 CACHE STRING "Log level (0=error 1=warn 2=info 3=debug)")
option(OPENAI_USE_TLS "Enable HTTPS for lwIP backend (0/1)" 1)
option(OPENAI_BUILD_EXAMPLES "Build example programs" OFF)
option(OPENAI_BUILD_TESTS "Build project tests" OFF)
set(OPENAI_TIMEOUT 30 CACHE STRING "HTTP request timeout in seconds")
set(OPENAI_HTTP_BACKEND "OPENAI_BACKEND_CURL" CACHE STRING "HTTP backend")
set_property(CACHE OPENAI_HTTP_BACKEND PROPERTY STRINGS
    OPENAI_BACKEND_BOTH OPENAI_BACKEND_CURL OPENAI_BACKEND_LWIP)

set(LWIP_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lwip/src/include" CACHE PATH "lwIP src/include directory")
set(MBEDTLS_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mbedtls" CACHE PATH "mbedTLS root directory")

set(OPENAI_CORE_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/openai_client.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/openai_json.c
)
set(OPENAI_CURL_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/openai_http_curl.c)
set(OPENAI_LWIP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/openai_http_lwip.c)

if(OPENAI_LOG_ENABLED)
    set(OPENAI_LOG_ENABLED_VALUE 1)
else()
    set(OPENAI_LOG_ENABLED_VALUE 0)
endif()

set(COMMON_DEFS
    OPENAI_LOG_ENABLED=${OPENAI_LOG_ENABLED_VALUE}
    OPENAI_LOG_LEVEL=${OPENAI_LOG_LEVEL}
    OPENAI_TIMEOUT=${OPENAI_TIMEOUT}
)

if(OPENAI_USE_TLS)
    set(OPENAI_USE_TLS_VALUE 1)
else()
    set(OPENAI_USE_TLS_VALUE 0)
endif()

if(OPENAI_HTTP_BACKEND STREQUAL "OPENAI_BACKEND_BOTH" OR
   OPENAI_HTTP_BACKEND STREQUAL "OPENAI_BACKEND_CURL")
    add_library(openai_curl STATIC
        ${OPENAI_CORE_SRCS}
        ${OPENAI_CURL_SRC}
    )
    target_include_directories(openai_curl PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libcurl/include
    )
    target_compile_options(openai_curl PRIVATE -w -std=gnu11)
    target_compile_definitions(openai_curl PRIVATE
        ${COMMON_DEFS}
        OPENAI_USE_TLS=${OPENAI_USE_TLS_VALUE}
    )
endif()

if(OPENAI_BUILD_EXAMPLES)
    if(NOT TARGET openai_curl)
        message(FATAL_ERROR "OPENAI_BUILD_EXAMPLES requires the curl backend")
    endif()

    set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
    if(WIN32)
        set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
    endif()
    add_subdirectory(third_party/libcurl EXCLUDE_FROM_ALL)
    target_link_libraries(openai_curl PUBLIC libcurl)
    add_subdirectory(example)
endif()

if(OPENAI_HTTP_BACKEND STREQUAL "OPENAI_BACKEND_BOTH" OR
   OPENAI_HTTP_BACKEND STREQUAL "OPENAI_BACKEND_LWIP")
    if(LWIP_INCLUDE_DIR STREQUAL "")
        message(WARNING "LWIP_INCLUDE_DIR is not set; lwIP backend may fail to compile. "
                        "Usage: cmake .. -DLWIP_INCLUDE_DIR=/path/to/lwip/src/include")
    endif()

    set(MBEDTLS_EXTRA_INCS "")
    if(NOT MBEDTLS_INCLUDE_DIR STREQUAL "")
        set(MBEDTLS_EXTRA_INCS
            ${MBEDTLS_INCLUDE_DIR}/tf-psa-crypto/include
            ${MBEDTLS_INCLUDE_DIR}/tf-psa-crypto/drivers/builtin/include
            ${MBEDTLS_INCLUDE_DIR}/tf-psa-crypto/core
            ${MBEDTLS_INCLUDE_DIR}/library
        )
    endif()

    add_library(openai_lwip STATIC
        ${OPENAI_CORE_SRCS}
        ${OPENAI_LWIP_SRC}
    )
    target_include_directories(openai_lwip PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lwip_port
        ${LWIP_INCLUDE_DIR}
        ${MBEDTLS_INCLUDE_DIR}/include
        ${MBEDTLS_EXTRA_INCS}
    )
    target_compile_options(openai_lwip PRIVATE -w -std=gnu11)
    set(OPENAI_LWIP_DEFS
        ${COMMON_DEFS}
        OPENAI_USE_LWIP=1
        OPENAI_USE_TLS=${OPENAI_USE_TLS_VALUE}
    )
    if(OPENAI_USE_TLS)
        list(APPEND OPENAI_LWIP_DEFS
            LWIP_ALTCP=1
            LWIP_ALTCP_TLS=1
            LWIP_ALTCP_TLS_MBEDTLS=1
        )
    endif()
    target_compile_definitions(openai_lwip PRIVATE ${OPENAI_LWIP_DEFS})

    if(NOT OPENAI_USE_TLS)
        # The bundled compile-check lwipopts.h enables TLS. Load lwIP options
        # once, then remove the TLS feature macros before compiling this target.
        # Real integrations should likewise leave these macros undefined when
        # OPENAI_USE_TLS is disabled.
        set(OPENAI_LWIP_NO_TLS_HEADER
            "${CMAKE_CURRENT_BINARY_DIR}/generated/openai_lwip_no_tls.h")
        file(GENERATE OUTPUT "${OPENAI_LWIP_NO_TLS_HEADER}" CONTENT
"#include \"lwip/opt.h\"\n#undef LWIP_ALTCP_TLS\n#undef LWIP_ALTCP_TLS_MBEDTLS\n")
        target_compile_options(openai_lwip PRIVATE
            $<$<COMPILE_LANGUAGE:C>:-include>
            $<$<COMPILE_LANGUAGE:C>:${OPENAI_LWIP_NO_TLS_HEADER}>
        )
    endif()
endif()

if(OPENAI_BUILD_TESTS)
    enable_testing()
    add_executable(core_api_tests
        tests/core_api_tests.c
        ${OPENAI_CORE_SRCS}
    )
    target_include_directories(core_api_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_definitions(core_api_tests PRIVATE ${COMMON_DEFS})
    if(UNIX)
        target_link_libraries(core_api_tests PRIVATE m)
    endif()
    add_test(NAME core_api_tests COMMAND core_api_tests)

    if(TARGET openai_lwip AND NOT OPENAI_USE_TLS)
        add_executable(openai_lwip_no_tls_link tests/lwip_no_tls_link.c)
        target_include_directories(openai_lwip_no_tls_link PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lwip_port
            ${LWIP_INCLUDE_DIR})
        target_link_libraries(openai_lwip_no_tls_link PRIVATE openai_lwip)
        add_test(NAME openai_lwip_no_tls_link COMMAND openai_lwip_no_tls_link)

        add_executable(openai_lwip_transport_tests
            tests/lwip_transport_tests.c)
        target_include_directories(openai_lwip_transport_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lwip_port
            ${LWIP_INCLUDE_DIR})
        target_link_libraries(openai_lwip_transport_tests PRIVATE openai_lwip)
        add_test(NAME openai_lwip_transport_tests
            COMMAND openai_lwip_transport_tests)
    endif()
endif()

message(STATUS "")
message(STATUS "========== C-OpenAI standalone build ==========")
message(STATUS "  Backend: ${OPENAI_HTTP_BACKEND}")
if(TARGET openai_curl)
    message(STATUS "  Output: libopenai_curl.a  (libcurl backend)")
endif()
if(TARGET openai_lwip)
    message(STATUS "  Output: libopenai_lwip.a  (lwIP backend)")
endif()
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Logging: OPENAI_LOG_ENABLED=${OPENAI_LOG_ENABLED_VALUE} Level=${OPENAI_LOG_LEVEL}")
message(STATUS "  TLS: OPENAI_USE_TLS=${OPENAI_USE_TLS_VALUE}")
message(STATUS "  Timeout: OPENAI_TIMEOUT=${OPENAI_TIMEOUT}s")
message(STATUS "==============================================")
message(STATUS "")
