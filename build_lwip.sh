#!/bin/bash
# Build script for lwIP backend (embedded/STM32)
# Uses ARM GCC from STM32CubeIDE
set -e

# ============================================================
# Toolchain paths (STM32CubeIDE)
# ============================================================
ARM_GCC="D:/APPS/st32ide/STM32CubeIDE_2.1.1/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740/tools/bin/arm-none-eabi-gcc.exe"
ARM_AR="D:/APPS/st32ide/STM32CubeIDE_2.1.1/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740/tools/bin/arm-none-eabi-ar.exe"

# Project root (junction to avoid spaces)
ROOT="E:/c_openai"
BUILD="$ROOT/build_lwip"

# MCU: Cortex-M4 (STM32F4)
MCU_FLAGS="-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16"

# ============================================================
# Create directories
# ============================================================
mkdir -p "$BUILD/lwip_core" "$BUILD/lwip_api" "$BUILD/lwip_sys"
mkdir -p "$BUILD/mbedtls" "$BUILD/mbedtls_crypto"
mkdir -p "$BUILD/openai" "$BUILD/lib" "$BUILD/arch"

# ============================================================
# Generate lwipopts.h
# ============================================================
echo "=== Step 0: Generating lwipopts.h ==="
cat > "$BUILD/lwipopts.h" << 'EOF'
#ifndef LWIP_LWIPOPTS_H
#define LWIP_LWIPOPTS_H

#define NO_SYS 0
#define LWIP_SOCKET 1
#define LWIP_NETCONN 0
#define LWIP_NETIF_API 0
#define LWIP_PROVIDE_ERRNO 1
#define LWIP_TIMEVAL_PRIVATE 1

#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_TCP 1
#define LWIP_UDP 1
#define LWIP_DNS 1

#define MEM_SIZE (16 * 1024)
#define MEMP_NUM_PBUF 16
#define MEMP_NUM_TCP_PCB 8
#define MEMP_NUM_TCP_PCB_LISTEN 4
#define MEMP_NUM_TCP_SEG 16
#define MEMP_NUM_SYS_TIMEOUT 10

#define TCP_MSS 1460
#define TCP_SND_BUF (4 * TCP_MSS)
#define TCP_SND_QUEUELEN (2 * TCP_SND_BUF / TCP_MSS)
#define TCP_WND (4 * TCP_MSS)

#define LWIP_ALTCP 1
#define LWIP_ALTCP_TLS 1
#define LWIP_ALTCP_TLS_MBEDTLS 1
#define MEMP_NUM_ALTCP_PCB 4
#define MEMP_NUM_ALTCP_TLS_PCB 4

#define CHECKSUM_GEN_IP 0
#define CHECKSUM_GEN_UDP 0
#define CHECKSUM_GEN_TCP 0
#define CHECKSUM_CHECK_IP 0
#define CHECKSUM_CHECK_UDP 0
#define CHECKSUM_CHECK_TCP 0
#define LWIP_STATS 0
#define LWIP_DEBUG 0

#endif
EOF

# Generate arch/cc.h
cat > "$BUILD/arch/cc.h" << 'EOF'
#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H
#include <errno.h>
#include <stdint.h>
#include <limits.h>
typedef uint8_t u8_t;
typedef int8_t s8_t;
typedef uint16_t u16_t;
typedef int16_t s16_t;
typedef uint32_t u32_t;
typedef int32_t s32_t;
typedef uintptr_t mem_ptr_t;
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END
#define PACK_STRUCT_STRUCT __attribute__((packed))
#define LWIP_PLATFORM_DIAG(x)
#define LWIP_PLATFORM_ASSERT(x)
#endif
EOF

# Generate arch/sys_arch.h
cat > "$BUILD/arch/sys_arch.h" << 'EOF'
#ifndef LWIP_ARCH_SYS_ARCH_H
#define LWIP_ARCH_SYS_ARCH_H
typedef int sys_prot_t;
typedef void* sys_sem_t;
typedef void* sys_mutex_t;
typedef void* sys_mbox_t;
typedef void* sys_thread_t;
#endif
EOF

# Generate sys_arch.c (stub for bare-metal)
cat > "$BUILD/sys_arch.c" << 'EOF'
#include "lwip/sys.h"
#include "arch/cc.h"
#include "arch/sys_arch.h"
#include <stdlib.h>

sys_prot_t sys_arch_protect(void) { return 0; }
void sys_arch_unprotect(sys_prot_t pval) { (void)pval; }
err_t sys_sem_new(sys_sem_t *sem, u8_t count) { *sem = malloc(4); return ERR_OK; }
void sys_sem_free(sys_sem_t *sem) { if (*sem) free(*sem); }
void sys_sem_signal(sys_sem_t *sem) {}
u32_t sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout) { (void)sem; (void)timeout; return 0; }
err_t sys_mutex_new(sys_mutex_t *mutex) { *mutex = malloc(4); return ERR_OK; }
void sys_mutex_free(sys_mutex_t *mutex) { if (*mutex) free(*mutex); }
void sys_mutex_lock(sys_mutex_t *mutex) {}
void sys_mutex_unlock(sys_mutex_t *mutex) {}
err_t sys_mbox_new(sys_mbox_t *mbox, int size) { (void)size; *mbox = malloc(4); return ERR_OK; }
void sys_mbox_free(sys_mbox_t *mbox) { if (*mbox) free(*mbox); }
void sys_mbox_post(sys_mbox_t *mbox, void *msg) { (void)mbox; (void)msg; }
err_t sys_mbox_trypost(sys_mbox_t *mbox, void *msg) { (void)mbox; (void)msg; return ERR_OK; }
u32_t sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout) { (void)mbox; (void)msg; (void)timeout; return 0; }
sys_thread_t sys_thread_new(const char *name, lwip_thread_fn thread, void *arg, int stacksize, int prio) { (void)name; (void)thread; (void)arg; (void)stacksize; (void)prio; return NULL; }
u32_t sys_jiffies(void) { return 0; }
u32_t sys_now(void) { return 0; }
EOF

# ============================================================
# Common flags
# ============================================================
LWIP_DEFS="-DLWIP_IPV4=1 -DLWIP_IPV6=0 -DLWIP_UDP=1 -DLWIP_TCP=1 -DLWIP_SOCKET=1 -DNO_SYS=0 -DLWIP_TIMEVAL_PRIVATE=1"
LWIP_INCS="-I$ROOT/third_party/lwip/src/include -I$BUILD -I$BUILD/arch"
LWIP_CFLAGS="-std=gnu11 -w $MCU_FLAGS"

MBEDTLS_DEFS="-DMBEDTLS_CONFIG_FILE=\"mbedtls_config.h\""
MBEDTLS_INCS="-I$ROOT/third_party/mbedtls/include \
-I$ROOT/third_party/mbedtls/tf-psa-crypto/include \
-I$ROOT/third_party/mbedtls/tf-psa-crypto/core \
-I$ROOT/third_party/mbedtls/tf-psa-crypto/extras \
-I$ROOT/third_party/mbedtls/tf-psa-crypto/utilities \
-I$ROOT/third_party/mbedtls/tf-psa-crypto/drivers/builtin/include \
-I$ROOT/third_party/mbedtls/tf-psa-crypto/drivers/builtin/src \
-I$ROOT/third_party/mbedtls/library"
MBEDTLS_CFLAGS="-std=gnu11 -w $MCU_FLAGS"

OPENAI_DEFS="-DOPENAI_USE_LWIP=1 -DLWIP_ALTCP=1 -DLWIP_ALTCP_TLS=1 -DLWIP_ALTCP_TLS_MBEDTLS=1"
OPENAI_INCS="-I$ROOT/include \
-I$ROOT/third_party/lwip/src/include \
-I$ROOT/third_party/mbedtls/include \
-I$ROOT/third_party/mbedtls/tf-psa-crypto/include \
-I$ROOT/third_party/mbedtls/tf-psa-crypto/core \
-I$ROOT/third_party/mbedtls/tf-psa-crypto/extras \
-I$ROOT/third_party/mbedtls/tf-psa-crypto/utilities \
-I$ROOT/third_party/mbedtls/tf-psa-crypto/drivers/builtin/include \
-I$ROOT/third_party/mbedtls/tf-psa-crypto/drivers/builtin/src \
-I$ROOT/third_party/mbedtls/library \
-I$BUILD -I$BUILD/arch"
OPENAI_CFLAGS="-std=gnu11 -w $MCU_FLAGS"

# ============================================================
# STEP 1: Compile lwIP
# ============================================================
echo ""
echo "=== Step 1: Compiling lwIP ==="

LWIP_CORE_SRCS="def.c inet_chksum.c init.c ip.c mem.c memp.c netif.c pbuf.c raw.c stats.c sys.c tcp.c tcp_in.c tcp_out.c timeouts.c udp.c"
LWIP_IPV4_SRCS="ipv4/icmp.c ipv4/ip4.c ipv4/ip4_addr.c"
LWIP_API_SRCS="api_lib.c api_msg.c err.c netbuf.c netdb.c sockets.c tcpip.c"

LWIP_OBJS=""

for src in $LWIP_CORE_SRCS $LWIP_IPV4_SRCS; do
    basename=$(echo "$src" | sed 's|/|_|g; s|\.c$|.o|')
    objpath="$BUILD/lwip_core/$basename"
    srcpath="$ROOT/third_party/lwip/src/core/$src"
    [ ! -f "$srcpath" ] && echo "  SKIP: $src" && continue
    echo "  [$src]"
    "$ARM_GCC" $LWIP_DEFS $LWIP_INCS $LWIP_CFLAGS -c "$srcpath" -o "$objpath"
    LWIP_OBJS="$LWIP_OBJS $objpath"
done

for src in $LWIP_API_SRCS; do
    basename=$(echo "$src" | sed 's|\.c$|.o|')
    objpath="$BUILD/lwip_api/$basename"
    srcpath="$ROOT/third_party/lwip/src/api/$src"
    [ ! -f "$srcpath" ] && echo "  SKIP: $src" && continue
    echo "  [api/$src]"
    "$ARM_GCC" $LWIP_DEFS $LWIP_INCS $LWIP_CFLAGS -c "$srcpath" -o "$objpath"
    LWIP_OBJS="$LWIP_OBJS $objpath"
done

# sys_arch stub
echo "  [sys_arch.c]"
"$ARM_GCC" $LWIP_DEFS $LWIP_INCS $LWIP_CFLAGS -c "$BUILD/sys_arch.c" -o "$BUILD/lwip_sys/sys_arch.o"
LWIP_OBJS="$LWIP_OBJS $BUILD/lwip_sys/sys_arch.o"

LWIP_A="$BUILD/lib/liblwip.a"
echo "  -> liblwip.a"
rm -f "$LWIP_A"
"$ARM_AR" rc "$LWIP_A" $LWIP_OBJS

# ============================================================
# STEP 2: Compile mbedTLS
# ============================================================
echo ""
echo "=== Step 2: Compiling mbedTLS ==="

MBEDTLS_LIB_SRCS="debug.c ssl_cache.c ssl_ciphersuites.c ssl_client.c ssl_cookie.c ssl_msg.c ssl_ticket.c ssl_tls.c ssl_tls12_client.c ssl_tls12_server.c ssl_tls13_client.c ssl_tls13_server.c ssl_tls13_generic.c ssl_tls13_keys.c"
CRYPTO_SRCS="aes.c bignum.c bignum_core.c bignum_mod.c bignum_mod_raw.c cipher.c cipher_wrap.c cmac.c ctr_drbg.c ecdsa.c ecp.c ecp_curves.c entropy.c entropy_poll.c hash.c hmac_drbg.c md.c md_wrap.c nist_kw.c oid.c pem.c pk.c pk_wrap.c pk_ec.c pk_rsa.c pkcs5.c platform.c platform_util.c rsa.c sha256.c sha512.c threading.c timing.c"

MBEDTLS_OBJS=""
for src in $MBEDTLS_LIB_SRCS; do
    basename=$(echo "$src" | sed 's|\.c$|.o|')
    objpath="$BUILD/mbedtls/$basename"
    srcpath="$ROOT/third_party/mbedtls/library/$src"
    [ ! -f "$srcpath" ] && echo "  SKIP: library/$src" && continue
    echo "  [library/$src]"
    "$ARM_GCC" $MBEDTLS_DEFS $MBEDTLS_INCS $MBEDTLS_CFLAGS -c "$srcpath" -o "$objpath"
    MBEDTLS_OBJS="$MBEDTLS_OBJS $objpath"
done

for src in $CRYPTO_SRCS; do
    basename=$(echo "$src" | sed 's|\.c$|.o|')
    objpath="$BUILD/mbedtls_crypto/$basename"
    srcpath="$ROOT/third_party/mbedtls/tf-psa-crypto/drivers/builtin/src/$src"
    [ ! -f "$srcpath" ] && echo "  SKIP: crypto/$src" && continue
    echo "  [crypto/$src]"
    "$ARM_GCC" $MBEDTLS_DEFS $MBEDTLS_INCS $MBEDTLS_CFLAGS -c "$srcpath" -o "$objpath"
    MBEDTLS_OBJS="$MBEDTLS_OBJS $objpath"
done

MBEDTLS_A="$BUILD/lib/libmbedtls.a"
echo "  -> libmbedtls.a"
rm -f "$MBEDTLS_A"
"$ARM_AR" rc "$MBEDTLS_A" $MBEDTLS_OBJS

# ============================================================
# STEP 3: Compile openai
# ============================================================
echo ""
echo "=== Step 3: Compiling openai ==="

OPENAI_SRCS="openai_client.c openai_json.c openai_http_lwip.c"
OPENAI_OBJS=""
for src in $OPENAI_SRCS; do
    objname=$(echo "$src" | sed 's|\.c$|.o|')
    objpath="$BUILD/openai/$objname"
    srcpath="$ROOT/src/$src"
    echo "  [$src]"
    "$ARM_GCC" $OPENAI_DEFS $OPENAI_INCS $OPENAI_CFLAGS -c "$srcpath" -o "$objpath"
    OPENAI_OBJS="$OPENAI_OBJS $objpath"
done

OPENAI_A="$BUILD/lib/libopenai.a"
echo "  -> libopenai.a"
rm -f "$OPENAI_A"
"$ARM_AR" rc "$OPENAI_A" $OPENAI_OBJS

# ============================================================
# Summary
# ============================================================
echo ""
echo "========================================"
echo "  Build Complete!"
echo "========================================"
echo "  lwIP objects:   $(echo $LWIP_OBJS | wc -w) files"
echo "  mbedTLS objects: $(echo $MBEDTLS_OBJS | wc -w) files"
echo "  openai objects:  $(echo $OPENAI_OBJS | wc -w) files"
echo ""
echo "  Libraries:"
ls -la "$BUILD/lib/"*.a
echo ""
echo "  Link these into your STM32 project with:"
echo "    -lopenai -lmbedtls -llwip -lc -lm -lnosys"
