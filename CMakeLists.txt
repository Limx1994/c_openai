cmake_minimum_required(VERSION 3.18)
project(c_openai VERSION 0.0.1 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ================================================================
# 构建模式：独立构建（不链接第三方库）
# 生成两个静态库：libopenai_curl.a 和 libopenai_lwip.a
# 用户可将 .a 文件合并到自己的工程中
# ================================================================

# ================================================================
# 编译选项（通过 cmake -D<OPTION>=<VALUE> 覆盖）
# ================================================================
option(OPENAI_LOG_ENABLED  "启用日志 (0/1)"                            0)
set(OPENAI_LOG_LEVEL       0  CACHE STRING "日志级别 (0=error 1=warn 2=info 3=debug)")
option(OPENAI_USE_TLS      "lwIP 后端启用 HTTPS (0/1)"                1)
set(OPENAI_TIMEOUT         30 CACHE STRING "HTTP 请求超时 (秒)")

# 第三方库头文件路径（用户必须指定）
set(LWIP_INCLUDE_DIR    "" CACHE PATH "lwIP src/include 目录路径")
set(MBEDTLS_INCLUDE_DIR "" CACHE PATH "mbedTLS 根目录路径（包含 include/ 子目录）")

# ================================================================
# 源文件
# ================================================================
set(OPENAI_CORE_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/openai_client.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/openai_json.c
)
set(OPENAI_CURL_SRC  ${CMAKE_CURRENT_SOURCE_DIR}/src/openai_http_curl.c)
set(OPENAI_LWIP_SRC  ${CMAKE_CURRENT_SOURCE_DIR}/src/openai_http_lwip.c)

# ================================================================
# 通用编译定义（两个后端共享）
# ================================================================
set(COMMON_DEFS
    OPENAI_LOG_ENABLED=${OPENAI_LOG_ENABLED}
    OPENAI_LOG_LEVEL=${OPENAI_LOG_LEVEL}
    OPENAI_TIMEOUT=${OPENAI_TIMEOUT}
)

# ================================================================
# 构建 libopenai_curl.a（libcurl 后端）
# ================================================================
add_library(openai_curl STATIC
    ${OPENAI_CORE_SRCS}
    ${OPENAI_CURL_SRC}
)
target_include_directories(openai_curl PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_options(openai_curl PRIVATE -w -std=gnu11)
target_compile_definitions(openai_curl PRIVATE
    ${COMMON_DEFS}
    OPENAI_USE_TLS=${OPENAI_USE_TLS}
)

# ================================================================
# 构建 libopenai_lwip.a（lwIP 后端）
# ================================================================
if(LWIP_INCLUDE_DIR STREQUAL "")
    message(WARNING "LWIP_INCLUDE_DIR 未设置，lwIP 后端编译可能失败。"
                    "用法: cmake .. -DLWIP_INCLUDE_DIR=/path/to/lwip/src/include")
endif()

# 自动推导 mbedTLS 子目录（用户只需指定根目录）
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
    OPENAI_USE_TLS=${OPENAI_USE_TLS}
)

# ================================================================
# 构建信息
# ================================================================
message(STATUS "")
message(STATUS "========== C-OpenAI 独立构建 ==========")
message(STATUS "  输出: libopenai_curl.a  (libcurl 后端)")
message(STATUS "  输出: libopenai_lwip.a  (lwIP 后端)")
message(STATUS "  日志: OPENAI_LOG_ENABLED=${OPENAI_LOG_ENABLED}  Level=${OPENAI_LOG_LEVEL}")
message(STATUS "  TLS:  OPENAI_USE_TLS=${OPENAI_USE_TLS}")
message(STATUS "  超时: OPENAI_TIMEOUT=${OPENAI_TIMEOUT}s")
message(STATUS "========================================")
message(STATUS "")
