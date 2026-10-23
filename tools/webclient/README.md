# Decenza web client

The Wasm interface is an in-progress second interface; the existing HTML interface remains intact. It mounts the shared app QML and reaches host-owned data and operations through typed services. Full application parity and acceptance are still being validated.

Production sources are in `src/webclient`, `src/remote`, `qml/webclient`, and `resources/webclient`. Diagnostic components, recorded traces and Qt reproductions remain development fixtures in this directory. Historical benchmark and acceptance evidence is recorded in the OpenSpec evaluation.

## Build and package

Use **Build on the native Qt Creator project**. The default `DECENZA_WEB_BUNDLE_MODE=LOCAL` configures and incrementally builds Wasm, verifies its package identity, and embeds it before native resource compilation and signing. Missing prerequisites produce a setup error. Source changes invalidate the shared identity and rebuild the matching client automatically. No separate kit build or manual staging is needed.

One-time prerequisites are Qt 6.12.0 native tools and `wasm_singlethread`, activated Emscripten 5.0.5, and Ninja. Native Qt needs Remote Objects, Shader Tools, Quick 3D and Graphs; Wasm needs Shader Tools, Quick 3D, Graphs, WebSockets and CanvasPainter. Install the matching stock Qt Multimedia and Remote Objects sources beside the SDK (`6.12.0/Src`); missing Wasm modules are built into workspace prefixes without changing the SDK. Existing Wasm kit caches, including the one named by `DECENZA_WEB_BUNDLE_DIR`, supply the paths automatically. For a fresh setup, set `DECENZA_WASM_QT_DIR` and `DECENZA_WASM_EMSDK_DIR` if discovery cannot find them. `DECENZA_WASM_HOST_QT_DIR` selects matching native tools when building another native platform; `DECENZA_WASM_BUILD_DIR` optionally reuses a specific Wasm build directory.

`DECENZA_WEB_BUNDLE_MODE=PREBUILT` verifies a supplied `DECENZA_WEB_BUNDLE_DIR` without a child build; `OFF` disables optional local packaging. Release CI's `DECENZA_REQUIRE_WEB_BUNDLE=ON` always uses PREBUILT and requires embedding; missing, corrupt or mismatched artifacts fail configuration. `DECENZA_EMBED_WEB_BUNDLE=OFF` retains the local macOS external resource-directory mode. The single `.github/workflows/release.yml` accepts `platforms=all` or a list. One Wasm producer and selected native jobs compile in parallel; native jobs join the verified bundle only for final linking/packaging. Cross-platform CI and installed-package behavior still require validation.

The running native app serves the packaged interface at `/`, with `/wasm/` as an alias. The original HTML interface remains available at `/legacy/`. Open the native host's origin directly for development and diagnostics; its entry document supplies the matching package and host identity for the shared Qt Remote Objects connection. Build the native project after source changes, then reload the entry document after a host restart.

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

The browser starts on the shared Home page. Routes and host services are being
validated in slices; the OpenSpec change records current evidence and remaining
gates. Run the integrated client against the packaged native host.


### Mac trackpad scrolling

Qt 6.12's Wasm wheel input still hits Flickable's angle-delta/easing path: [QTBUG-141540](https://qt-project.atlassian.net/browse/QTBUG-141540), with direct-scrolling work under review. The web bootstrap uses wheelScrollLines=5 to remove the default 0.6 distance scale, but this only partially mitigates continuous-gesture slowness. The maintainer chose to keep this small adjustment and track the upstream fix while continuing parity. Recheck and remove/reassess the adjustment when Qt's direct scrolling lands; do not add per-page scroll multipliers.
