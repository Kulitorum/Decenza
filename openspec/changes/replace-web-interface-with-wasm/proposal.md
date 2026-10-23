# Proposal

## Why

Decenza maintains its native QML interface and a separate HTML/JavaScript web interface, with ongoing work to keep their features aligned. The target is one Wasm web interface sharing native QML, appearance and functionality. The Wasm app now owns `/`; the existing HTML interface moves beneath `/legacy/` while compatibility, usability and maintenance value are proven. Maintaining two web interfaces indefinitely is not a goal.

## What Changes

- Serve the Qt WebAssembly client at `/` for remote computers, phones, and tablets. Retain `/wasm/` as a compatibility alias to the same app. Remote use targets a browser-trusted HTTPS origin; localhost HTTP remains available for development.
- Cover the entire native application QML surface, not selected pages: application shell, every page and settings tab, dialogs, overlays, controls, configurable widgets, screensavers, simulator UI, and their supporting scripts/assets. Native and Wasm consume the same presentation source files, with full look, feel and action parity. Track every file and dependency to implementation and validation tasks; build inclusion alone is not completion.
- Keep that native device authoritative for Bluetooth/USB, machine operations, stop-at-weight, settings, profiles, history, integrations, and credentials. Native and browser share one active preparation workflow selection for espresso, steam, hot water, and flush, along with preparation settings; page navigation, editors, history/search views, dialogs, and unsaved drafts remain independent.
- Build and ship the Wasm client and native host together as one package. The client connects only to its serving app origin and requires the matching package identity; mixed versions are rejected, not supported through degraded behavior.
- Serve the client from the existing ShotServer origin. Reuse its APIs and native services, adding a small typed remote presentation layer where QML currently calls local objects.
- Run a phased evaluation: inventory and baseline; actual QML/graph rendering on desktop and mobile; an end-to-end remote read/write slice; and difficult existing web workflows in the second interface.
- Make every evaluation phase end in recorded evidence and a go/no-go decision. A failed gate ends further Wasm work pending a revised decision; a loading demo alone is not sufficient evidence.
- Preserve existing HTML functionality, bookmark context, API consumers, file routes, and authentication as the HTML pages move below `/legacy/`. Redirect or retain compatible aliases for existing non-root HTML bookmarks. The root route serves Wasm; `/legacy/` remains the direct HTML entry point.
- Keep REST, MCP, MQTT, relay, download, and file-upload contracts rooted as they are. HTML page links, assets, and form destinations must resolve within `/legacy/` where appropriate; `/api/...` remains unchanged. `/wasm/` continues to serve the same Wasm application.
- Support current desktop Chrome/Edge, Firefox, and Safari, Android Chrome, and iPhone/iPad Safari, with real mobile-device checks early.

## Capabilities

### New Capabilities

- `wasm-browser-client`: Root Wasm delivery with `/wasm/` alias, shared QML presentation, mobile usability, and compatibility for legacy HTML routes under `/legacy/`.
- `remote-ui-session`: Browser-to-host state, commands, synchronization, reconnect behavior, and native ownership of machine operations and data.

## Impact

- A separate browser entry point and CMake target, Qt 6.12-compatible Emscripten toolchain, shared QML/components, typed remote adapters, and packaged browser assets.
- ShotServer delivery/routing/authentication, existing REST handlers, and an additive session/synchronization API. Existing REST, MCP, MQTT, Pocket relay, and download contracts remain supported.
- QML dependencies on concrete local C++ singletons, custom graph rendering, fonts, image providers, file transfers, and browser accessibility need validation.
- Native release packaging includes the browser bundle at the root entry and `/wasm/` alias; existing native platforms and HTML frontend remain supported under `/legacy/`. Tests, browser checks, ShotServer documentation, and the wiki manual are part of rollout.
- Per the maintainer's 2026-10-20 direction, initial evaluation uses a browser Wasm process talking to native Qt Decenza and its DE1 simulator on the same Mac. Use the existing root CMake project with separate native/Wasm build directories. The loopback HTTP transport probe and current single-thread build are historical development baselines, not remote-deployment requirements or a conclusion that threads are unnecessary. Remote Android-host and phone/tablet validation remain later acceptance gates and target trusted HTTPS. The hostname and certificate provisioning approach is undecided. Creating a new cloud relay is outside this change.
- Earlier additive/default-HTML planning is historical and superseded by the 2026-10-22 routing decision: `/` serves Wasm, `/wasm/` remains an alias, and HTML moves to `/legacy/` with compatible redirects for non-root bookmarks. This change does not retire the HTML implementation. Individual Mac slices establish progress toward native-app parity, not acceptance of the entire replacement. Shared services should serve native and Wasm directly, without adding abstractions solely to perpetuate the temporary HTML frontend.
- The maintainer explicitly reconfirmed complete app-QML coverage on 2026-10-21. Earlier feasibility-only time boxes and limited-page milestones are historical evidence, not the remaining implementation scope. Continue on the Mac with HTML available under `/legacy/`; separately report unverified mobile, remote HTTPS, accessibility and real-machine acceptance. Browser Quit remains the expressly approved behavior difference.
