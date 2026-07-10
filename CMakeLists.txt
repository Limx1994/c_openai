cmake_minimum_required(VERSION 3.18)
project(c_openai VERSION 0.0.1 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(OPENAI_LOG_ENABLED "Enable logging (0/1)" 0)
set(OPENAI_LOG_LEVEL 0 CACHE STRING "Log level (0=error 1=warn 2=info 3=debug)")
option(OPENAI_USE_TLS "Enable HTTPS for lwIP backend (0/1)" 1)
set(OPENAI_TIMEOUT 30 CACHE STRING "HTTP request timeout in seconds")
set(OPENAI_HTTP_BACKEND "OPENAI_BACKEND_BOTH" CACHE STRING "HTTP backend")
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

set(COMMON_DEFS
    OPENAI_LOG_ENABLED=${OPENAI_LOG_ENABLED}
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
    target_include_directories(openai_curl PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libcurl/include
    )
    target_compile_options(openai_curl PRIVATE -w -std=gnu11)
    target_compile_definitions(openai_curl PRIVATE
        ${COMMON_DEFS}
        OPENAI_USE_TLS=${OPENAI_USE_TLS_VALUE}
    )
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
    target_compile_definitions(openai_lwip PRIVATE
        ${COMMON_DEFS}
        OPENAI_USE_LWIP=1
        LWIP_ALTCP=1
        LWIP_ALTCP_TLS=1
        LWIP_ALTCP_TLS_MBEDTLS=1
        OPENAI_USE_TLS=${OPENAI_USE_TLS_VALUE}
    )
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
message(STATUS "  Logging: OPENAI_LOG_ENABLED=${OPENAI_LOG_ENABLED} Level=${OPENAI_LOG_LEVEL}")
message(STATUS "  TLS: OPENAI_USE_TLS=${OPENAI_USE_TLS_VALUE}")
message(STATUS "  Timeout: OPENAI_TIMEOUT=${OPENAI_TIMEOUT}s")
message(STATUS "==============================================")
message(STATUS "")
