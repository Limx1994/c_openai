# third_party/CMakeLists.txt
# CMake configuration for lwIP and mbedTLS submodules
#
# Usage:
#   git clone --recursive https://github.com/Limx1994/c_openai.git
#   Or after clone: git submodule update --init --recursive

cmake_minimum_required(VERSION 3.10)

# ================================
# lwIP Configuration
# ================================
set(LWIP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lwip)

# Build lwIP core as static library
add_library(lwip_core STATIC
    ${LWIP_DIR}/src/core/def.c
    ${LWIP_DIR}/src/core/inet_chksum.c
    ${LWIP_DIR}/src/core/init.c
    ${LWIP_DIR}/src/core/ip.c
    ${LWIP_DIR}/src/core/mem.c
    ${LWIP_DIR}/src/core/memp.c
    ${LWIP_DIR}/src/core/netif.c
    ${LWIP_DIR}/src/core/pbuf.c
    ${LWIP_DIR}/src/core/raw.c
    ${LWIP_DIR}/src/core/stats.c
    ${LWIP_DIR}/src/core/sys.c
    ${LWIP_DIR}/src/core/tcp.c
    ${LWIP_DIR}/src/core/tcp_in.c
    ${LWIP_DIR}/src/core/tcp_out.c
    ${LWIP_DIR}/src/core/timeouts.c
    ${LWIP_DIR}/src/core/udp.c
    ${LWIP_DIR}/src/core/ipv4/icmp.c
    ${LWIP_DIR}/src/core/ipv4/ip4.c
    ${LWIP_DIR}/src/core/ipv4/ip4_addr.c
    ${LWIP_DIR}/src/core/ipv4/ethip4.c
    ${LWIP_DIR}/src/netif/ethernet.c
)

target_include_directories(lwip_core PUBLIC
    ${LWIP_DIR}/src/include
    ${LWIP_DIR}/src/include/lwip
    ${LWIP_DIR}/src/include/lwip/priv
    ${LWIP_DIR}/src/include/lwip/prot
    ${LWIP_DIR}/src/include/netif
    ${LWIP_DIR}/contrib/ports/unix/include
)

target_compile_definitions(lwip_core PUBLIC
    LWIP_IPV4=1
    LWIP_IPV6=0
    LWIP_UDP=1
    LWIP_TCP=1
    LWIP_NETIF=1
)

target_compile_options(lwip_core PRIVATE -Wno-unused-parameter -Wno-sign-compare)

# Build lwIP POSIX sockets wrapper
add_library(lwip_socket STATIC
    ${LWIP_DIR}/src/api/sockets.c
    ${LWIP_DIR}/src/api/netdb.c
)

target_include_directories(lwip_socket PUBLIC
    ${LWIP_DIR}/src/include
    ${LWIP_DIR}/src/include/lwip
    ${LWIP_DIR}/contrib/ports/unix/include
)

target_compile_definitions(lwip_socket PUBLIC
    LWIP_SOCKET=1
    LWIP_POSIX_SOCKETS_IO_NAMES=1
)

target_link_libraries(lwip_socket PUBLIC lwip_core)
target_compile_options(lwip_socket PRIVATE -Wno-unused-parameter -Wno-sign-compare)

# ALTCP (Alternate TCP) for TLS support
add_library(lwip_altcp STATIC
    ${LWIP_DIR}/src/altcp/altcp.c
    ${LWIP_DIR}/src/altcp/altcp_tcp.c
    ${LWIP_DIR}/src/altcp/altcp_tls.c
)

target_include_directories(lwip_altcp PUBLIC
    ${LWIP_DIR}/src/include
    ${LWIP_DIR}/src/include/lwip
)

target_compile_definitions(lwip_altcp PUBLIC
    LWIP_ALTCP=1
)

target_link_libraries(lwip_altcp PUBLIC lwip_core)
target_compile_options(lwip_altcp PRIVATE -Wno-unused-parameter -Wno-sign-compare)

# Combined lwip target for convenience
add_library(lwip ALIAS lwip_socket)

# ================================
# mbedTLS Configuration
# ================================
set(MBEDTLS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls)

# mbedTLS uses its own CMake build system
# We use an interface library and link to system mbedtls
add_library(mbedtls INTERFACE)

# Try to find mbedtls package first
find_package(MbedTLS QUIET)
if(MBEDTLS_FOUND)
    message(STATUS "Found system mbedTLS")
    target_link_libraries(mbedtls INTERFACE MbedTLS::mbedtls MbedTLS::mbedx509 MbedTLS::mbedcrypto)
else()
    # Try pkg-config
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(MBEDTLS_PC mbedtls QUIET)
        if(MBEDTLS_PC_FOUND)
            message(STATUS "Found mbedTLS via pkg-config")
            target_link_libraries(mbedtls INTERFACE ${MBEDTLS_PC_LIBRARIES})
            target_include_directories(mbedtls INTERFACE ${MBEDTLS_PC_INCLUDE_DIRS})
        endif()
    endif()
endif()

target_include_directories(mbedtls INTERFACE
    ${MBEDTLS_DIR}/include
)

# ================================
# Export variables for parent CMake
# ================================
set(LWIP_INCLUDE_DIRS
    ${LWIP_DIR}/src/include
    ${LWIP_DIR}/src/include/lwip
    ${LWIP_DIR}/src/include/lwip/priv
    ${LWIP_DIR}/src/include/netif
    ${LWIP_DIR}/contrib/ports/unix/include
    PARENT_SCOPE
)

set(MBEDTLS_INCLUDE_DIRS
    ${MBEDTLS_DIR}/include
    PARENT_SCOPE
)