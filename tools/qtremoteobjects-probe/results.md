# QtRO/Wasm Mac compatibility result — 2026-10-21

**GO for a bounded QtRO integration experiment on the Mac.** Stock Qt Remote Objects 6.12.0 built for the single-threaded Wasm kit and communicated successfully with a separate native Qt host. No Decenza architecture or HTML interface was changed for this experiment.

## Environment and provenance

- macOS 27.2; Safari 27.2 and the Codex in-app browser.
- Qt 6.12.0 native macOS and `wasm_singlethread`; Emscripten 5.0.5; Wasm Release.
- Stock source: `/Users/jeffreyh/Qt/6.12.0/Src/qtremoteobjects`. All original source files match the workspace copy byte-for-byte.
- Additional module prefix: `build/qtro-wasm-install`. The installed SDK was not patched or modified.
- Adapted [SynQt transport spike](https://github.com/Kidev/SynQt/tree/a5e4f7272a32c14b596ed77350b635b649a245ec/tests/transport-spike), revision `a5e4f7272a32c14b596ed77350b635b649a245ec`, Apache-2.0. License and modified-file notices are retained.
- No SynQt watcher polling fallback, Asyncify flag, framework dependency, or Qt patch. The 50 ms test observation timer checks assertions/model data; it never completes pending calls or pumps Qt events.

## Results

| Check | In-app browser | Safari |
|---|---|---|
| Generated replica receives initial typed properties | PASS | PASS |
| Host property changes and signal payload arrive | PASS | PASS |
| Returned method value completes through `QRemoteObjectPendingCallWatcher::finished` | PASS | PASS |
| Model initial rows and values | PASS | PASS |
| Model row insertion, data change, row removal | PASS | PASS |
| Deliberate WebSocket close is observed | PASS | PASS |
| New node/socket/replica receives changed host properties and exact fresh model | PASS | PASS |

A negative control reloaded the browser against the completed, mutated source. It received `counter=100`, `label=reconnected`, four rows, and correctly reported `FAIL Timed out during initial-state`. Restart the host in Qt Creator for each run; concurrent test clients are not isolated.

The first in-app run took about 1.46 seconds from START to final PASS, including the deliberate disconnect and 500 ms reconnect delay. This is a functional-test duration, not an application performance measurement.

All compilation and installation ran through Qt Creator. Successful build IDs: module `1792509381761` (9.85 s), installation `1792509381762`, Wasm probe `1792509381764` (12.71 s), native host `1792509381765` (3.28 s). All reported zero warnings/errors. An earlier native compile caught a missing lambda capture in the probe; it was fixed before either browser run. Native build `1792509381768` also passed after attribution comments were added.

Local evidence: `build/qtro-evidence/` contains compiler logs, artifact hashes, `iab-console.json`, `safari-result.txt`, `negative-stale-source.json`, and screenshots. The QtRO static archive is 3,221,662 bytes; the probe Wasm binary is 14,685,465 bytes. These are not estimates of Decenza's eventual bundle size.

## Decision boundary

Proceed to a small shared-service experiment using generated source/replica types instead of handwritten property synchronization. Keep the same production QML and native implementation; measure whether it actually removes duplicate code before broad migration.

This result does **not** establish production authentication/authorization, package pairing, trusted HTTPS/WSS, physical-device control, mobile browsers, long-session reliability, multi-client concurrency, application performance, or full QML/API compatibility. Reconnect here intentionally rebuilds the client node and transport; it does not prove that reopening an existing QtRO device works. The native test host only serves loopback and uses disposable in-memory data; it is not the Decenza process.

QtRO remains officially unsupported for Wasm. The successful local test narrows that risk; it does not change Qt's support status or close the deferred product acceptance gates.
