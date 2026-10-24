# Qt Remote Objects WebAssembly transport probe

This isolated probe answers whether the stock Qt Remote Objects library can use the stock Qt WebSockets I/O adapter from a single-threaded Qt 6.12 WebAssembly client. It builds a native host and browser client independently; it does not link Decenza code or touch machine APIs.

The client performs assertions automatically. It checks initial typed property and `QAbstractItemModel` replication, invokes source methods and waits only for `QRemoteObjectPendingCallWatcher::finished`, checks returned values, property updates, a signal, and model row insertion/change/removal, asks the host to close the WebSocket, then reconnects with a new node/socket/adapter and verifies the reset property and exact four-row model snapshot. The browser page displays `PASS` or `FAIL` and the chronological probe log. There is no polling fallback for method replies: an unresolved stock watcher is a failure this probe is intended to expose.

The QtRO transport and `.rep` contract are adapted from the Apache-2.0 licensed SynQt `tests/transport-spike` at revision `a5e4f7272a32c14b596ed77350b635b649a245ec`; see [LICENSE](LICENSE). The watcher fallback, QML shell, retry-on-echo behavior, test matrix, and custom patching are not included. `shared/probe.rep` and the test controller are modified for deterministic mutation and reconnect assertions. The sample WebSocket I/O adapter preserves the source implementation with attribution; the host, static HTTP server, and model checks are probe-specific.

## Build in Qt Creator

First build **unmodified** `Qt/6.12.0/Src/qtremoteobjects` using the existing single-threaded Wasm kit. The recorded Mac run used a byte-identical source copy in `build/qtro-wasm-source` so Qt Creator's project settings stay in this workspace. Use Release, the kit's `qt.toolchain.cmake`, native `QT_HOST_PATH`, and these CMake values:

```text
CMAKE_INSTALL_PREFIX=<workspace>/build/qtro-wasm-install
CMAKE_STAGING_PREFIX=<workspace>/build/qtro-wasm-install
QT_BUILD_TESTS=OFF
QT_BUILD_EXAMPLES=OFF
QT_BUILD_TOOLS_BY_DEFAULT=OFF
QT_BUILD_DOCS=OFF
```

Build and then build the `install` target through Qt Creator. This installs only the additional module into the workspace; it does not alter the installed Qt SDK. The failed initial preset auto-detection was bypassed by selecting the existing Wasm kit directly.

Configure a native kit against the normal Qt 6.12 installation. Open this directory as a CMake project and select the `qtro_probe_host` target. The host listens on loopback HTTP port `8897` and WebSocket port `8898`; pass `--web-root <Wasm-deployment-directory>` as its run argument.

Configure a separate `wasm_singlethread` kit against the Wasm Qt 6.12 installation, after the stock Qt Remote Objects module has been built and installed into `build/qtro-wasm-install`. Set the CMake cache variable `QTRO_PREFIX` to that install directory and select `qtro_probe_client`. Qt Creator's generated HTML target is served by the native host. Open `http://127.0.0.1:8897/` in a browser on the same machine.

**Use one browser run per host process.** Stop and restart `qtro_probe_host` in Qt Creator before reloading or testing another browser. The in-memory test source deliberately stays in its final state after a run; simultaneous clients are not isolated.

The native host serves only static files under the selected web root, without directory traversal, and both listeners bind to loopback. The app performs no writes outside its own in-memory probe state.

## Interpreting the result

One green `PASS` means all asserted directions worked in that browser run, including the queued pending-call watcher and a reconnect with fresh replica/model initialization. It is a focused compatibility measurement, not a general QtRO guarantee for other browsers, TLS configurations, Qt versions, or application contracts. Record the Qt kit, browser version, and visible log with each run.

The adapted reference is [SynQt's transport spike](https://github.com/Kidev/SynQt/tree/a5e4f7272a32c14b596ed77350b635b649a245ec/tests/transport-spike). This probe does not adopt the SynQt framework or its Qt patches. Reconnect here recreates the client node, transport, and replicas; it does not assert that reopening an existing QtRO device works.

Recorded Mac results and scope: [results.md](results.md).
