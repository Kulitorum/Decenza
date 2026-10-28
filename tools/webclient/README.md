# Decenza web client

The experimental Wasm interface mounts the shared app QML and reaches host-owned data and operations through typed services. The existing HTML interface remains intact. Whole-application acceptance is still being validated.

Production sources are in `src/webclient`, `src/remote`, `qml/webclient`, and `resources/webclient`. Diagnostic components, recorded traces and Qt reproductions remain development fixtures in this directory. Historical benchmark and acceptance evidence is recorded in the OpenSpec evaluation.

UI changes follow [the shared UI guide](../../docs/CLAUDE_MD/SHARED_UI.md).

## Build and package

Use **Build on the native Qt Creator project**. The default `DECENZA_WEB_BUNDLE_MODE=LOCAL` configures and incrementally builds Wasm, verifies its package identity, and embeds it before native resource compilation and signing. Missing prerequisites produce a setup error. Source changes invalidate the shared identity and rebuild the matching client automatically. No separate kit build or manual staging is needed.

Complete the [one-time toolchain setup](#one-time-toolchain-setup) below before
configuring a fresh checkout. Existing Wasm kit caches can supply these paths
automatically, but a new checkout must not depend on another checkout's cache.

Local native Debug builds default `DECENZA_WASM_FAST_LINK=ON`: Wasm C++ objects
keep Release optimization, while the final link skips Binaryen's whole-program
optimization. Set it OFF in native CMake settings to measure release performance.
Release CI and standalone Wasm builds default OFF. The measured local link fell
from 52.3 s to 3.0 s; the development binary grew from about 86 MiB to 96 MiB.
Both package validation and the host accept Wasm files up to 128 MiB.

Packaging stores a Brotli copy of the runtime and scripts beside the originals;
the host serves it to browsers that accept `br` and the original otherwise.
`DECENZA_WEB_BROTLI_QUALITY` defaults to 5 (about 1 s), and the release Wasm job
sets 11 (about 2 minutes, roughly 15% smaller).

`DECENZA_WEB_BUNDLE_MODE=PREBUILT` verifies a supplied `DECENZA_WEB_BUNDLE_DIR` without a child build; `OFF` disables optional local packaging. Release CI's `DECENZA_REQUIRE_WEB_BUNDLE=ON` always uses PREBUILT and requires embedding; missing, corrupt or mismatched artifacts fail configuration. `DECENZA_EMBED_WEB_BUNDLE=OFF` retains the local macOS external resource-directory mode. The single `.github/workflows/release.yml` accepts `platforms=all` or a list. One Wasm producer and selected native jobs start in parallel. Ninja native jobs join the verified bundle for final linking/packaging; iOS performs setup in parallel and compiles once through its final archive. Cross-platform CI and installed-package behavior still require validation.

The running native app serves the packaged interface at `/`, with `/wasm/` as an alias. The original HTML interface remains available at `/legacy/`. Open the native host's origin directly for development and diagnostics; its entry document supplies the matching package and host identity for the shared Qt Remote Objects connection. Build the native project after source changes, then reload the entry document after a host restart.

## One-time toolchain setup

Install **Qt 6.12.0** native tools and **`wasm_singlethread`**, **Emscripten
5.0.5**, Ninja, and the **`brotli`** command-line tool (Homebrew ships 1.2.1, which
fixes CVE-2026-111497; on Linux use your distribution's patched package). Pin Emscripten to this version rather than `latest`: the Qt
Wasm binaries and our CI use this toolchain together. Native Qt needs Remote
Objects, Shader Tools, Quick 3D, Graphs and CanvasPainter; Wasm needs Shader
Tools, Quick 3D, Graphs, WebSockets and CanvasPainter. Install the matching stock
Qt Multimedia and Remote Objects sources beside Qt (`6.12.0/Src`). The native
build builds missing Wasm modules into workspace prefixes without modifying Qt.

### Existing local SDK: share it across checkouts

On the maintainer's Mac, the activated SDK is already at
`~/Development/GitHub/emsdk` (currently
`/Users/jeffreyh/Development/GitHub/emsdk`). **Reuse it for Decenza,
Decenza-Desktop, and other local checkouts; do not clone/install another SDK per
checkout.** The shared SDK's `.emscripten` must exist and its active version must
remain 5.0.5. Do not activate another version there while another build is using it.

For a persistent setup shared by projects using the same native kit, open
**Qt Creator → Preferences → Kits → the native kit → CMake configuration** and
add this parameter, keeping the existing parameters:

```text
-DDECENZA_WASM_EMSDK_DIR:PATH=/Users/jeffreyh/Development/GitHub/emsdk
```

Alternatively, set `EMSDK` to that directory in the kit's environment. Use the
SDK's **expanded absolute path**, not literal `~`. A project-only choice belongs
in **Projects → Build Settings → CMake → Initial Configuration**. Apply/reconfigure
the existing project after changing its setup. Qt documents the distinction in
[CMake build settings](https://doc.qt.io/qtcreator/creator-build-settings-cmake.html)
and [kit preferences](https://doc.qt.io/qtcreator/creator-preferences-kits.html).

Editing only `CMakeCache.txt` unblocks the current build but is lost when that
cache is wiped. Keep the path in the kit or initial configuration instead; do
not commit this machine-specific path. Leave `DECENZA_WEB_BUNDLE_MODE=LOCAL`
and build the native project normally; no second Wasm project or ordered manual
builds are required.

Share the SDK, **not CMake build directories**. A Wasm build cache belongs to its
source checkout; the driver rejects one whose `CMAKE_HOME_DIRECTORY` points
elsewhere. Leave `DECENZA_WASM_BUILD_DIR` unset for automatic discovery/creation,
or select a Wasm build belonging to the current checkout.

### New developers: install and activate emsdk

On macOS/Linux, choose one SDK location outside the Decenza checkout. For example:

```sh
git clone https://github.com/emscripten-core/emsdk.git "$HOME/Development/GitHub/emsdk"
"$HOME/Development/GitHub/emsdk/emsdk" install 5.0.5
"$HOME/Development/GitHub/emsdk/emsdk" activate 5.0.5
source "$HOME/Development/GitHub/emsdk/emsdk_env.sh"
emcc --version
```

The final command should report Emscripten **5.0.5**. If the SDK already exists,
reuse it and skip cloning. On Windows, use `emsdk.bat` for install/activate and
run `emsdk_env.bat` in Command Prompt instead of sourcing the shell script.

Set `DECENZA_WASM_EMSDK_DIR` in the native project's CMake settings to the
absolute SDK root, as above. If Qt discovery also needs help, set
`DECENZA_WASM_QT_DIR` to the `6.12.0/wasm_singlethread` prefix and
`DECENZA_WASM_HOST_QT_DIR` to the matching native tools prefix (for example
`6.12.0/macos`, even when the native application target is Android).

### Environment and common setup failures

`emsdk activate` selects the toolchain; `source emsdk_env.sh` sets up **that
shell's** environment. Executing the script without sourcing it does not update
the shell. An already-running Qt Creator also does not inherit changes made in a
terminal afterward. For the normal single-project build, setting
`DECENZA_WASM_EMSDK_DIR` is sufficient: `scripts/build_webclient.py` supplies
`EMSDK`, `EM_CONFIG`, and the SDK's compiler/Node/Python paths to its child build.
Do not replace the native kit's compiler or toolchain with Emscripten.

- **“Install/activate Emscripten 5.0.5…”**: check the absolute SDK root and its
  `.emscripten` file. Installing without activating is incomplete.
- **`emcc` not found in a terminal**: source that SDK's `emsdk_env.sh`, then check
  `emcc --version`. This version check does not build Decenza.
- **Wrong SDK/Node/Python in a separate Wasm kit**: that kit needs the same
  activated SDK environment. Prefer the normal native project build, which
  supplies it automatically.
- **Another-checkout error**: fix `DECENZA_WASM_BUILD_DIR` and any stale
  `DECENZA_WEB_BUNDLE_DIR`; do not reuse the other checkout's CMake cache.

Assistants must still follow `CLAUDE.md`: application builds/tests run through
Qt Creator, and a blocked build path requires asking. SDK setup instructions are
not authorization to bypass that workflow or a user's reservation of Qt Creator.

## Native ZIP support

The native build fetches pinned QuaZip 1.6 through `cmake/quazip.cmake`, builds
it statically, and packages its license. It reuses a toolchain-provided ZLIB target
when present; otherwise it builds pinned zlib 1.3.2 statically. The dependency C
compiles use the configured compiler cache too.
There is no system QuaZip installation or extra deployment step. FetchContent
keeps sources under the build directory; for offline configuration, set
`FETCHCONTENT_SOURCE_DIR_QUAZIP` and `FETCHCONTENT_SOURCE_DIR_ZLIB` to existing
copies of those pinned sources. Wasm delegates ZIP work to its native host and
does not compile these libraries.

`src/core/ziparchive.cpp` is the shared chunked reader/writer for backups,
diagnostic ZIPs, conversion bundles and desktop shot imports. Keep archive I/O
there rather than reintroducing Qt's private ZIP APIs. Standard DEFLATE levels
0–9 are available; the default is 6. Already-compressed media is stored directly
instead of spending CPU compressing it again. Large entries use ZIP64 as needed.

## Mac diagnostics and fixture checks

Build the native project with Qt Creator to produce both targets, then start
the native host through Qt Creator and open its packaged `/` or `/wasm/` entry. Append
`?diagnostics` to load the developer diagnostics root, `?accessibility-repro`
for the isolated editable-field reproduction, or
`?accessibility-repro=list-item` for the isolated ListItem reproduction. The
diagnostics root uses the shared production text/suggestion controls and graph
view with local fixture data; the two accessibility reproductions are Qt-only.
These opt-in roots are development diagnostics, not app pages or substitutes
for app workflow coverage.

Run `tst_sharedformcontrols`, `tst_subsystemlogview`, and
`tst_operationcompletionoverlay` from Qt Creator's test runner. The first
exercises shared production controls; the second uses the production
`SubsystemLogView` with a deterministic service fixture. For the separate Qt
Multimedia lifecycle project, follow `tools/webclient/media-repro/README.md`
with the same `wasm_singlethread` kit. Builds and test runs through the
Qt Creator workflow do not by themselves establish browser behavior or runtime
acceptance.

## Current limits

The browser starts on the shared Home page. The complete app inventory and Mac
workflow checks are recorded in the OpenSpec change; source inclusion is not a
claim that every device has passed acceptance. Broad release still needs the
remaining mobile/screen-reader, device-performance, supervised-machine and
installed-platform checks. Host-impact timing was not measured; see the recorded
practical acceptance decision. Run the integrated client against its packaged host.

Screen-reader support is activated per browser document through Qt's **Enable
Screen Reader** button; the accessibility tree stays inactive otherwise. Actual
mobile screen-reader acceptance remains open. On iPhone, the video screensaver
currently cycles images rather than providing continuous playback while
[QTBUG-151807](https://bugreports.qt.io/browse/QTBUG-151807) is unresolved.


### Mac trackpad scrolling

The shared `BrowserDisplay` adapts Wasm pixel gestures to Qt's existing scrolling
path; the bootstrap also sets `wheelScrollLines=5`. The maintainer verified Edge
and Safari trackpad scrolling, including an Edge restart. Track the upstream
[QTBUG-141540](https://qt-project.atlassian.net/browse/QTBUG-141540) when reassessing
these adaptations; do not add per-page scroll multipliers.
