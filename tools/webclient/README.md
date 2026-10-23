# Decenza web client

The Wasm interface is an in-progress second interface; the existing HTML interface remains intact. It mounts the shared app QML and reaches host-owned data and operations through typed services. Full application parity and acceptance are still being validated.

Production sources are in `src/webclient`, `src/remote`, `qml/webclient`, and `resources/webclient`. Diagnostic components, recorded traces and Qt reproductions remain development fixtures in this directory. Historical benchmark and acceptance evidence is recorded in the OpenSpec evaluation.

## Build and package

Steam and Equipment use the same app QML and shared Qt Remote Objects services in both builds. Build stock Qt Remote Objects for the matching Wasm kit as recorded in `tools/qtremoteobjects-probe/results.md`; set `DECENZA_QTRO_PREFIX` to its install prefix (default `build/qtro-wasm-install`). The installed Qt SDK remains unchanged.

Use separate Qt Creator build directories for the Qt 6.12 `wasm_singlethread` target and the native target. Build Wasm first; its packaging target writes the verified runtime bundle to `src/webclient/deployment/` inside that build directory. Set the native build's `DECENZA_WEB_BUNDLE_DIR` to that deployment directory. Configuration verifies the manifest, hashes and matching source package identity, then embeds the bundle at `:/webclient` on every native platform. Rebuild Wasm before configuring native after a shared source change.

`DECENZA_EMBED_WEB_BUNDLE=OFF` retains the original local macOS `Contents/Resources/webclient` packaging mode. Release CI requires embedding with `DECENZA_REQUIRE_WEB_BUNDLE=ON`; missing, corrupt or mismatched bundles fail configuration. Each native release workflow calls `.github/workflows/wasm-bundle.yml` for stock Qt 6.12.0 modules and Emscripten 5.0.5, then consumes its own run's verified artifact. Cross-platform CI and installed-package behavior still require validation; workflow configuration alone is not release evidence.

The running native app serves the packaged interface at `/`, with `/wasm/` as an alias. The original HTML interface remains available at `/legacy/`. Open the native host's origin directly for development and diagnostics; its entry document supplies the matching package and host identity for the shared Qt Remote Objects connection. Rebuild and repackage both targets after source changes, then reload the entry document after a host restart.

## Mac diagnostics and fixture checks

Build the matching Qt 6.12 Wasm and native targets with Qt Creator, then start
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
