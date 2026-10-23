# Qt Multimedia for the WebAssembly kit

This project builds and installs the stock Qt Multimedia 6.12.0 module for the existing `wasm_singlethread` Qt kit. It keeps generated files and the install prefix under this repository's `build/` directory. It does not vendor Qt sources, change the installed Qt SDK, or build Decenza.

By default, CMake looks beside the selected Qt installation for `Src/qtmultimedia`, for example:

```text
/Users/jeffreyh/Qt/6.12.0/Src/qtmultimedia
```

If the source is elsewhere, override the `QT_MULTIMEDIA_SOURCE_DIR` cache path. The CMake project verifies that `.cmake.conf` identifies the source as Qt 6.12.0 and that Qt Creator selected an Emscripten, single-threaded kit. Open this directory as a CMake project in Qt Creator, select the existing Qt 6.12.0 WebAssembly single-threaded kit then build the `qtmultimedia` target. The target configures, builds, and installs the module under:

```text
build/qtmultimedia-wasm-install
```

The external module configure uses the selected kit's `CMAKE_TOOLCHAIN_FILE`, `QT_HOST_PATH`, `Qt6_DIR`, and `CMAKE_PREFIX_PATH`; its native build tools remain those from the host Qt installation. The module defaults to Release independently of the wrapper configuration (`QT_MULTIMEDIA_BUILD_TYPE` can override it). Tests, examples and docs are disabled. Rebuilding the target runs the external build incrementally and refreshes the workspace install.

To use the module in another Wasm CMake project, add the install prefix to that project's package search path before `find_package(Qt6 COMPONENTS Multimedia)`, then link `Qt6::Multimedia` and deploy the module's QML/plugin files with the application. Do not point the build at the native Qt Multimedia package.

CI packages both installed prefixes as a recipe-keyed `.tar.gz` artifact after the shared Wasm bundle passes validation. The artifact includes its SHA-256 sidecar, a manifest identifying Qt 6.12.0, Emscripten 5.0.5, `wasm_singlethread`, the module source URLs, and the build recipe key, plus the source license notices and installed SPDX files. Release runs reuse only an exact-recipe artifact from a successful Wasm job on this repository's current/default branch or a `v*` tag. They verify the checksum and manifest before extracting the prefixes, then renew the verified package unchanged so its original provenance remains intact. The artifact is retained for 90 days and pruning keeps the newest two for each recipe; this artifact storage is separate from the repository's 10 GiB Actions cache budget.

To download one independently, use `gh run download <run-id> -n <artifact-name>`. Verify it with `sha256sum -c <archive>.sha256` before extraction. The `.manifest.json` sidecar repeats the metadata embedded in the archive.

Qt 6.12 includes a WebAssembly media backend. Its platform notes document browser codec limitations and say web-URL streaming works; serving local host files to browser playback remains the application's responsibility. See the [Qt 6.12 WebAssembly notes](https://doc.qt.io/qt-6/qtmultimedia-wasm.html) and [source-build guide](https://doc.qt.io/qt-6/qtmultimedia-building-from-source.html).
