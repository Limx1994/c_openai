#!/usr/bin/env bash
# Build the curl backend and examples with the host toolchain.
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
ROOT="${ROOT:-$SCRIPT_DIR}"
BUILD="${BUILD_DIR:-$ROOT/build}"
JOBS="${JOBS:-10}"
BUILD_TYPE="${BUILD_TYPE:-MinSizeRel}"
CMAKE_BIN="${CMAKE:-cmake}"

case "$JOBS" in
    ''|*[!0-9]*) echo "ERROR: JOBS must be an integer >= 10" >&2; exit 2 ;;
esac
if [ "$JOBS" -lt 10 ]; then
    echo "ERROR: JOBS must be >= 10 (received $JOBS)" >&2
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

CC_BIN="$(resolve_tool "${CC:-gcc}")" || {
    echo "ERROR: host C compiler not found; set CC" >&2
    exit 1
}
AR_BIN="$(resolve_tool "${AR:-ar}")" || {
    echo "ERROR: host archiver not found; set AR" >&2
    exit 1
}
command -v "$CMAKE_BIN" >/dev/null 2>&1 || {
    echo "ERROR: CMake not found; set CMAKE" >&2
    exit 1
}

for required in CMakeLists.txt src/openai_client.c third_party/libcurl/CMakeLists.txt; do
    if [ ! -f "$ROOT/$required" ]; then
        echo "ERROR: missing required project file: $ROOT/$required" >&2
        exit 1
    fi
done

generator_args=()
if [ -n "${CMAKE_GENERATOR:-}" ]; then
    generator_args=(-G "$CMAKE_GENERATOR")
elif command -v mingw32-make >/dev/null 2>&1; then
    generator_args=(-G "MinGW Makefiles")
fi

echo "=== C-OpenAI curl build ==="
echo "  Root:      $ROOT"
echo "  Build:     $BUILD"
echo "  Compiler:  $CC_BIN"
echo "  Archiver:  $AR_BIN"
echo "  Build type: $BUILD_TYPE"
echo "  Parallel:  $JOBS"

"$CMAKE_BIN" -S "$ROOT" -B "$BUILD" "${generator_args[@]}" \
    -DCMAKE_C_COMPILER="$CC_BIN" \
    -DCMAKE_AR="$AR_BIN" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DOPENAI_HTTP_BACKEND=OPENAI_BACKEND_CURL \
    -DOPENAI_BUILD_EXAMPLES=ON

"$CMAKE_BIN" --build "$BUILD" --parallel "$JOBS"

echo "=== Build complete ==="
echo "  chat_example and anthropic_example are under: $BUILD/example"
