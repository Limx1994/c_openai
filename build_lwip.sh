#!/usr/bin/env bash
# Cross-build the C-OpenAI lwIP backend archive.
#
# This script intentionally does not fabricate a sys_arch implementation or
# claim to produce runnable firmware. A real lwIP Socket API integration needs
# an RTOS-backed lwIP port, network driver, and (for HTTPS) mbedTLS libraries.
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
ROOT="${ROOT:-$SCRIPT_DIR}"
BUILD="${BUILD_DIR:-$ROOT/build_lwip}"
JOBS="${JOBS:-10}"
BUILD_TYPE="${BUILD_TYPE:-MinSizeRel}"
OPENAI_USE_TLS="${OPENAI_USE_TLS:-1}"
CMAKE_BIN="${CMAKE:-cmake}"
MCU_FLAGS="${MCU_FLAGS:--mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16}"
read -r -a mcu_flag_args <<< "$MCU_FLAGS"

case "$JOBS" in
    ''|*[!0-9]*) echo "ERROR: JOBS must be an integer >= 10" >&2; exit 2 ;;
esac
if [ "$JOBS" -lt 10 ]; then
    echo "ERROR: JOBS must be >= 10 (received $JOBS)" >&2
    exit 2
fi
if [ "$OPENAI_USE_TLS" != 0 ] && [ "$OPENAI_USE_TLS" != 1 ]; then
    echo "ERROR: OPENAI_USE_TLS must be 0 or 1" >&2
    exit 2
fi

resolve_tool()
{
    if [ -x "$1" ]; then
        printf '%s\n' "$1"
    else
        command -v "$1"
    fi
}

find_arm_tool()
{
    tool_name="$1"
    explicit_path="$2"
    if [ -n "$explicit_path" ]; then
        resolve_tool "$explicit_path"
        return
    fi
    if [ -n "${ARM_TOOLCHAIN_BIN:-}" ]; then
        resolve_tool "$ARM_TOOLCHAIN_BIN/$tool_name"
        return
    fi
    if command -v "$tool_name" >/dev/null 2>&1; then
        command -v "$tool_name"
        return
    fi

    for candidate in \
        D:/APPS/st32ide/STM32CubeIDE_*/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*/tools/bin/"$tool_name".exe \
        C:/ST/STM32CubeIDE_*/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*/tools/bin/"$tool_name".exe; do
        if [ -x "$candidate" ]; then
            printf '%s\n' "$candidate"
            return
        fi
    done
    return 1
}

ARM_GCC_BIN="$(find_arm_tool arm-none-eabi-gcc "${ARM_GCC:-}")" || {
    echo "ERROR: ARM GCC not found; set ARM_GCC or ARM_TOOLCHAIN_BIN" >&2
    exit 1
}
ARM_AR_BIN="$(find_arm_tool arm-none-eabi-ar "${ARM_AR:-}")" || {
    echo "ERROR: ARM ar not found; set ARM_AR or ARM_TOOLCHAIN_BIN" >&2
    exit 1
}
ARM_NM_BIN="$(find_arm_tool arm-none-eabi-nm "${ARM_NM:-}")" || {
    echo "ERROR: ARM nm not found; set ARM_NM or ARM_TOOLCHAIN_BIN" >&2
    exit 1
}
command -v "$CMAKE_BIN" >/dev/null 2>&1 || {
    echo "ERROR: CMake not found; set CMAKE" >&2
    exit 1
}

required_files="
CMakeLists.txt
src/openai_client.c
src/openai_json.c
src/openai_http_lwip.c
third_party/lwip/src/include/lwip/sockets.h
third_party/lwip_port/lwipopts.h
third_party/lwip_port/arch/cc.h
"
if [ "$OPENAI_USE_TLS" = 1 ]; then
    required_files="$required_files
third_party/lwip/src/include/lwip/altcp_tls.h
third_party/mbedtls/include/mbedtls/ssl.h
third_party/mbedtls/tf-psa-crypto/include/psa/crypto.h
"
fi
for required in $required_files; do
    if [ ! -f "$ROOT/$required" ]; then
        echo "ERROR: missing required source/header: $ROOT/$required" >&2
        exit 1
    fi
done

generator_args=()
if [ -n "${CMAKE_GENERATOR:-}" ]; then
    generator_args=(-G "$CMAKE_GENERATOR")
elif command -v mingw32-make >/dev/null 2>&1; then
    generator_args=(-G "MinGW Makefiles")
fi

echo "=== C-OpenAI lwIP archive build ==="
echo "  Root:       $ROOT"
echo "  Build:      $BUILD"
echo "  Compiler:   $ARM_GCC_BIN"
echo "  Build type: $BUILD_TYPE"
echo "  MCU flags:  $MCU_FLAGS"
echo "  TLS:        $OPENAI_USE_TLS"
echo "  Parallel:   $JOBS"

"$CMAKE_BIN" -S "$ROOT" -B "$BUILD" "${generator_args[@]}" \
    -DCMAKE_SYSTEM_NAME=Generic \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DCMAKE_C_COMPILER="$ARM_GCC_BIN" \
    -DCMAKE_AR="$ARM_AR_BIN" \
    -DCMAKE_C_FLAGS="$MCU_FLAGS" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DOPENAI_HTTP_BACKEND=OPENAI_BACKEND_LWIP \
    -DOPENAI_USE_TLS="$OPENAI_USE_TLS" \
    -DOPENAI_BUILD_EXAMPLES=OFF \
    -DOPENAI_BUILD_TESTS=OFF \
    -DLWIP_INCLUDE_DIR="$ROOT/third_party/lwip/src/include" \
    -DMBEDTLS_INCLUDE_DIR="$ROOT/third_party/mbedtls"

"$CMAKE_BIN" --build "$BUILD" --target openai_lwip --parallel "$JOBS"

OPENAI_A="$BUILD/libopenai_lwip.a"
if [ ! -s "$OPENAI_A" ]; then
    echo "ERROR: expected archive was not generated: $OPENAI_A" >&2
    exit 1
fi

archive_members="$($ARM_AR_BIN t "$OPENAI_A")"
for expected in openai_client openai_json openai_http_lwip; do
    if ! printf '%s\n' "$archive_members" | grep -q "$expected"; then
        echo "ERROR: archive is missing object for $expected" >&2
        exit 1
    fi
done

if [ -n "${OPENAI_LINK_OBJECT:-}" ] || [ -n "${OPENAI_PLATFORM_LINK_RSP:-}" ]; then
    if [ -z "${OPENAI_LINK_OBJECT:-}" ] || [ -z "${OPENAI_PLATFORM_LINK_RSP:-}" ]; then
        echo "ERROR: set both OPENAI_LINK_OBJECT and OPENAI_PLATFORM_LINK_RSP" >&2
        exit 1
    fi
    if [ ! -f "$OPENAI_LINK_OBJECT" ] || [ ! -f "$OPENAI_PLATFORM_LINK_RSP" ]; then
        echo "ERROR: final-link object or response file does not exist" >&2
        exit 1
    fi
    if ! "$ARM_NM_BIN" -u "$OPENAI_LINK_OBJECT" | grep -q 'openai_'; then
        echo "ERROR: OPENAI_LINK_OBJECT must reference at least one openai_* symbol" >&2
        exit 1
    fi

    LINK_ELF="${OPENAI_LINK_ELF:-$BUILD/openai_lwip_link_check.elf}"
    "$ARM_GCC_BIN" "${mcu_flag_args[@]}" "$OPENAI_LINK_OBJECT" "$OPENAI_A" \
        "@$OPENAI_PLATFORM_LINK_RSP" -Wl,-Map="$LINK_ELF.map" -o "$LINK_ELF"
    echo "  Final platform link verified: $LINK_ELF"
else
    echo "  Final platform link: NOT VERIFIED"
    echo "  Supply OPENAI_LINK_OBJECT and OPENAI_PLATFORM_LINK_RSP to verify"
    echo "  against a real RTOS/lwIP/mbedTLS platform link."
fi

echo "=== Archive build complete ==="
echo "  $OPENAI_A"
