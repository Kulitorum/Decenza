# Shared native and Wasm UI

Read this before changing an application page, control or UI-facing service. Native
and Wasm compile the **same QML files** from `qml/pages` and `qml/components`.
Wasm must match the app's appearance, interaction and functionality. The browser
shell in `qml/webclient` supplies browser lifecycle boundaries, not replacement pages.

## The path from a control to the host

```text
shared QML → shared Service facade → Qt Remote Objects → native Source → canonical controller/storage
```

Native QML uses that same facade through its in-process Remote Objects host. Wasm
uses its paired host's WebSocket connection. Do not add a direct native-only call
beside a browser implementation of the same action. Extend an existing domain
service first; add a new remote object when a distinct domain needs one.

Use `EquipmentService` as a concrete example:

- `src/remote/equipmentservice.rep` defines the wire properties, slots and signals.
- `equipmentservice.h/.cpp` provide the QML facade, readiness and pending requests.
- `equipmentservicesource.h/.cpp` delegate host work to the existing equipment
  operations, storage and calibration algorithms. Validation and conflict/deletion
  rules stay in those canonical implementations.
- `equipmentservice_qml.h` registers the singleton at compile time.
- `src/main.cpp` publishes the native source. The shared input lists in
  `cmake/shared_remote_inputs.cmake` supply facade/replica metadata to both builds.

## When changing a page or adding an action

1. Edit its existing QML and shared controls. Put formatting, visibility and action
   presentation there once; reuse Theme, backgrounds and existing components.
2. For host data or mutations, use the domain service. Extend the `.rep`, facade
   and native source together. Preserve request IDs, stale-selection checks,
   storage preconditions and explicit failure results. A global completion signal
   must not satisfy another client's request. Disconnect terminates pending writes;
   reconnect refreshes state and never replays mutations.
3. Keep unsaved drafts and normal navigation local to each client. Machine
   operations and preparation workflows belong to the host. Leaving a page or
   closing a browser must not implicitly cancel another client's operation.
4. For destinations and browser history, follow the single navigation procedure in
   [QML_NAVIGATION.md](QML_NAVIGATION.md).
5. Add each new shared QML file to the native `CMakeLists.txt` and Wasm
   `src/webclient/CMakeLists.txt` source sets. Update the complete QML inventory in
   the active Wasm OpenSpec design. Dynamic Loaders and literal resource URLs need
   the same dependency check as directly instantiated types.

Actual platform boundaries use an existing boundary service, for example
`ExternalUrlOpener`, `Keyboard` or local file transfer. A native WebView can use an
external browser tab in Wasm; that does not justify a separate equipment page.
Document genuine platform limitations instead of hiding missing domain operations
behind reduced controls, compatibility fallbacks or new browser HTTP adapters.

Use `RemoteServiceReplica::replace` for ordinary facade attachment. Withdraw
readiness before failing pending work: synchronous notification handlers can
submit another request or replace/delete the facade. Capture the admitting
replica and request identity before notifying; check ownership and that identity
before dispatch, and reject queued replies from a retired replica. Use the
existing `ServiceOperation`/`ServiceRequests` helpers when their admission policy
matches the domain, preserving specialized cancellation and progress behavior.

For client-owned host resources, use the transport's admitted `decenzaClientId`;
release them when `RemoteSessionServer` closes that client, since an unload RPC
may never arrive. `ShotReviewSessions` keeps upload leases per review view and
freezes the refractometer's result destination from actual device-run start to
finish. A cached property refresh is not a new measurement.

## Verify the shared change

Use [the build setup guide](../../tools/webclient/README.md) and follow the
repository's Qt Creator permission/build/test rules in `CLAUDE.md`.
Run relevant service tests and the QML inventory/resource-closure checks through
Qt Creator. Then inspect the affected native screen and the host-served browser
screen at `/` (alias `/wasm/`), including the changed action and failure/draft path.
A source-inclusion or green lint result alone is not runtime parity evidence.

The client is shipped with its host and may connect only to the matching package
and host epoch. After a restart or rebuild, reload the served entry document rather
than bypassing the package check or running an old standalone deployment.

HTML at `/legacy/` is temporary retained behavior. Preserve it until retirement;
do not add abstractions or duplicate UI work to maintain new-feature HTML parity.
HTTP/API and MCP routes still share the listener, so changes to routing, auth or
packaging also need focused checks that those existing interfaces still work.
