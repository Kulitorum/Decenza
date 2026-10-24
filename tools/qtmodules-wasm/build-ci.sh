#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "Usage: $0 <wasm-qt-prefix> <host-qt-prefix> <qt-sources-dir>" >&2
    exit 2
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$script_dir/../.." && pwd)"
qt_prefix="$1"
host_qt_prefix="$2"
qt_sources="$3"

if [[ -z "${EMSDK:-}" ]]; then
    echo "Emscripten must be activated before running this script." >&2
    exit 2
fi

# ccache's documented Clang PCH requirements:
# https://ccache.dev/manual/4.9.1.html#_precompiled_headers
export CCACHE_SLOPPINESS=pch_defines,time_macros
export CXXFLAGS='-Xclang -fno-pch-timestamp'

cd "$repo_root"
cmake -S tools/qtmodules-wasm -B build/ci-wasm-multimedia -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$qt_prefix/lib/cmake/Qt6/qt.toolchain.cmake" \
    -DQT_HOST_PATH="$host_qt_prefix" \
    -DQt6_DIR="$qt_prefix/lib/cmake/Qt6" \
    -DQT_MULTIMEDIA_SOURCE_DIR="$qt_sources/qtmultimedia" \
    -DQT_MULTIMEDIA_INSTALL_PREFIX="$repo_root/build/qtmultimedia-wasm-install"
cmake --build build/ci-wasm-multimedia --target qtmultimedia --parallel "$(nproc)"

# Same stock source and isolated-prefix configure as the recorded probe
# (tools/qtremoteobjects-probe/README.md).
grep -Eq '^set\(QT_REPO_MODULE_VERSION "6\.12\.0"\)' "$qt_sources/qtremoteobjects/.cmake.conf"
cmake -S "$qt_sources/qtremoteobjects" -B build/ci-wasm-qtro -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$qt_prefix/lib/cmake/Qt6/qt.toolchain.cmake" \
    -DQT_HOST_PATH="$host_qt_prefix" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_WITH_PCH=ON \
    -DCMAKE_INSTALL_PREFIX="$repo_root/build/qtro-wasm-install" \
    -DCMAKE_STAGING_PREFIX="$repo_root/build/qtro-wasm-install" \
    -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF \
    -DQT_BUILD_TOOLS_BY_DEFAULT=OFF -DQT_BUILD_DOCS=OFF
cmake --build build/ci-wasm-qtro --parallel "$(nproc)"
cmake --install build/ci-wasm-qtro
