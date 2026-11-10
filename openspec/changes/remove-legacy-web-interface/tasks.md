# Tasks

## 1. Inventory and review gate

- [x] 1.1 Inventory legacy page routes, aliases, generators, assets, helpers, upload paths and their callers; distinguish external contracts from page-only code. Verify the inventory covers `shotserver.cpp`, the split ShotServer files, `webtemplates/`, route tests and build references.
- [x] 1.2 Evaluate the surviving shared routing, upload and helper code for simplification and record each decision and its rationale. Verify every affected shared area is accounted for in the inventory.
- [x] 1.3 Before writing any new code, including tests, present each proposed addition and its value to the user and obtain their verification. Verify approval is recorded for each addition; if it is not given, leave that addition unwritten and report any blocked behavior or validation.

## 2. Remove the interface

- [x] 2.1 Delete legacy page generators, interface-exclusive supporting code, assets, declarations and build references, guided by the inventory. Verify no removed generator or helper has a remaining caller and retained APIs and authentication remain in place.
- [x] 2.2 Remove legacy namespace handling and old page redirects while removing legacy-only upload, HTTP restore and shot-download endpoints while preserving independently used API and migration routes. Verify representative old pages return 404 on LAN and remote access and existing migration routes retain their responses.
- [x] 2.3 Simplify affected surviving code where the inventory supports it, and remove or simplify obsolete tests. Verify the relevant existing route, security and session tests pass through Qt Creator; obtain prior user verification for any new code or tests.
- [x] 2.4 Update `docs/CLAUDE_MD/SHOTSERVER.md` and related affected documentation to describe the surviving routes and implementation. Verify no documentation directs users to the removed interface.
- [x] 2.5 Update the wiki manual and release notes to explain the removed legacy interface and old links and point to the current browser interface. Verify the published documentation changes or record the exact outstanding publication step.

## 3. Integration verification

- [x] 3.1 Build and run the relevant tests through Qt Creator; verify the Wasm entry and loader failure, authentication, session, remote access, representative external APIs and file transfers remain functional. Record exact results and any checks requiring a running app or user assistance.
- [x] 3.2 Compare the final diff with the inventory and report actual gross lines removed, lines added and net change separately from the preliminary estimate. Verify every retained or removed shared component has a reason.
