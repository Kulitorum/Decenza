#!/usr/bin/env bash
set -euo pipefail

architecture="${1:?Usage: package_linux_appimage.sh <x86_64|aarch64>}"
case "$architecture" in
    x86_64|aarch64) ;;
    *) echo "Unsupported AppImage architecture: $architecture" >&2; exit 2 ;;
esac

# Qt loads libssl dynamically (qtbase/src/plugins/tls/openssl/qsslsocket_openssl_symbols.cpp:778-787),
# so ELF dependency scanning alone misses it.
openssl_lib_dir="$(pkg-config --variable=libdir openssl)"
"./linuxdeploy-${architecture}.AppImage" \
    --appdir AppDir \
    --library "$openssl_lib_dir/libssl.so.3" \
    --library "$openssl_lib_dir/libcrypto.so.3" \
    --plugin qt \
    --output appimage

for required in usr/lib/libssl.so.3 usr/lib/libcrypto.so.3 usr/plugins/tls/libqopensslbackend.so; do
    if [ ! -s "AppDir/$required" ]; then
        echo "AppImage is missing its OpenSSL runtime component: $required" >&2
        exit 1
    fi
done
