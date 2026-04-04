#!/usr/bin/env bash
# ============================================================
# build.sh - P2P project build script (macOS / Linux)
#
# Usage:
#   ./build.sh [target] [options]
#
# Targets:
#   all        Build everything: library + p2p_ping + p2p_server (default)
#   lib        Build static library only (p2p_static)
#   ping       Build p2p_ping only  (implies lib)
#   server     Build p2p_server only (implies lib)
#   clean      Clean build output
#
# Options:
#   --build-dir <dir>   CMake build directory (default: build)
#   --config <cfg>      Build configuration: Debug or Release (default: Debug)
#   --help              Show this help
#
# Examples:
#   ./build.sh
#   ./build.sh all
#   ./build.sh ping
#   ./build.sh server --config Release
#   ./build.sh clean
#   ./build.sh all --build-dir build_release --config Release
# ============================================================

set -euo pipefail

TARGET="all"
BUILD_DIR="build"
BUILD_CONFIG="Debug"

# ---- parse arguments ----
while [[ $# -gt 0 ]]; do
    case "$1" in
        all|lib|ping|server|clean)
            TARGET="$1"; shift ;;
        --build-dir)
            BUILD_DIR="$2"; shift 2 ;;
        --config)
            BUILD_CONFIG="$2"; shift 2 ;;
        --help|-h|help)
            cat <<EOF

 build.sh - P2P project build script (macOS / Linux)

 Usage:
   ./build.sh [target] [options]

 Targets:
   all        Build everything: library + p2p_ping + p2p_server  (default)
   lib        Build static library only (p2p_static)
   ping       Build p2p_ping only  (implies lib)
   server     Build p2p_server only (implies lib)
   clean      Clean build output

 Options:
   --build-dir <dir>   CMake build directory  (default: build)
   --config <cfg>      Debug | Release         (default: Debug)
   --help              Show this help

 Examples:
   ./build.sh
   ./build.sh all
   ./build.sh ping
   ./build.sh server --config Release
   ./build.sh clean
   ./build.sh all --build-dir build_release --config Release

EOF
            exit 0 ;;
        *)
            echo "[WARN] Unknown argument: $1"; shift ;;
    esac
done

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

# ---- auto-detect generator (Ninja preferred) ----
if command -v ninja &>/dev/null; then
    CMAKE_GENERATOR="Ninja"
else
    CMAKE_GENERATOR="Unix Makefiles"
fi

# ---- configure if needed ----
if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    echo "[INFO] Configuring CMake in $BUILD_DIR (generator: $CMAKE_GENERATOR) ..."
    cmake -S . -B "$BUILD_DIR" -G "$CMAKE_GENERATOR" -DCMAKE_BUILD_TYPE="$BUILD_CONFIG"
fi

# ---- clean ----
if [[ "$TARGET" == "clean" ]]; then
    echo "[INFO] Cleaning $BUILD_DIR ..."
    cmake --build "$BUILD_DIR" --target clean
    exit $?
fi

# ---- build ----
case "$TARGET" in
    all)
        echo "[INFO] Building: p2p_static + p2p_ping + p2p_server [$BUILD_CONFIG]"
        cmake --build "$BUILD_DIR" --config "$BUILD_CONFIG"
        ;;
    lib)
        echo "[INFO] Building: p2p_static [$BUILD_CONFIG]"
        cmake --build "$BUILD_DIR" --config "$BUILD_CONFIG" --target p2p_static
        ;;
    ping)
        echo "[INFO] Building: p2p_ping [$BUILD_CONFIG]"
        cmake --build "$BUILD_DIR" --config "$BUILD_CONFIG" --target p2p_ping
        ;;
    server)
        echo "[INFO] Building: p2p_server [$BUILD_CONFIG]"
        cmake --build "$BUILD_DIR" --config "$BUILD_CONFIG" --target p2p_server
        ;;
esac

echo ""
echo "[OK] Build succeeded."
echo "     Build dir : $BUILD_DIR"
echo "     Binaries  :"
case "$TARGET" in
    all)
        echo "       $BUILD_DIR/p2p_ping/p2p_ping"
        echo "       $BUILD_DIR/p2p_server/p2p_server"
        ;;
    ping)   echo "       $BUILD_DIR/p2p_ping/p2p_ping" ;;
    server) echo "       $BUILD_DIR/p2p_server/p2p_server" ;;
    lib)    echo "       $BUILD_DIR/libp2p_static.a" ;;
esac
