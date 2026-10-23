# Experimental beta readiness — 2026-10-23

The maintainer authorized release packaging, focused acceptance and PR preparation before complete parity/platform acceptance. This is a release checkpoint within the existing change, not completion of its 190-task checklist. HTML remains at `/legacy/`; Wasm serves `/` and `/wasm/`. No legacy retirement, stable release or public remote/mobile support claim is included.

## Focused acceptance

- Before final packaging integration, matching Mac package `3d757ba3` passed both Qt Creator builds, the 353-file QML gate and all 203 suites (run 1792763853090). Native PID 96139 was the sole host, launched through Qt Creator from the original workspace.
- On that simulator/headless host, first-tap Recipe activation selected Sublunar Espresso Latte without starting a shot; native and browser showed the same applied plan. The open picker retained its order, and closing/reopening moved that recipe first. A second tap started the simulator. Closing the browser did not stop it; native Stop ended it, and a fresh browser observed Ready without replay. Gran Bar / D-Flow / Q, 18 g, 54 g and grind 10 were reapplied. The simulator shot remains in history; no permanent deletion was performed. Bean/equipment MRU acceptance remains incomplete under 14.1.
- Prior recorded recipe save/restore/conflict and retained-draft checks remain applicable; final focused/full fixtures must pass after integration. No new provider calls, crash submissions, real-machine operation or security-setting changes are authorized by these checks.
- Independent review found and corrected an iOS USB compile boundary in the shared Connections source. iOS now excludes unavailable USB types/calls and reports the capability unavailable. A platform build is still required to validate the fix.
- Current-main integration preserves its live-adjust controls and host timing restoration, refractometer teardown/reconnect and weighing fixes. Native and Wasm use the same upstream `LiveAdjustButton.qml`; Flush adjustments delegate to the context-checked host service.

## Packaging and final gate

Release workflows now produce a Wasm deployment from the same source/version code as their native package, install Qt Remote Objects and require manifest/hash/package verification. The native executable embeds the verified bundle under `:/webclient`, used by the same bounded ShotServer reader on every platform. Local Mac filesystem packaging remains an explicit development option. Missing or mismatched release input fails configuration. CI configuration is not evidence that a signed distributed artifact works.

Final local package **78900df0b177cb6f1b8ee9095a1cf13f95bfa527cebd92b56cca3c864eb7ce3c** passed Qt Creator Wasm build 1792763852379 (55.381 s, zero warnings) and native embedded build 1792763852381 (82.601 s, three existing linker warnings, QML gate 354/354 clean). Full run **1792763853092 passed 203/203 suites in 36.930 s**, with no failures, skips or test warnings. The new real-pointer confirmation test was first proven to fail on an intentionally wrong echoed request ID in run 1792763853091; production code was restored before final testing or pushing. Deny A, Allow B, exact IDs once and disabled controls now pass. This does not instantiate the full global support coordinator or replace its existing deadline/requester/reconnect fixtures.

Qt Creator stopped the old host, port ownership was checked, and Run launched the final original-workspace app once (PID 37143). Its session package matched the Wasm build; simulator/headless mode and Ready were confirmed. The browser rejected re-pairing its old document and required Reload, then rendered the new shared Settings UI. The leftover development filesystem bundle still held `3d757ba3`, while the served client/host used `78900df0`: successful pairing therefore verifies the embedded delivery path rather than that old directory. Root, alias and legacy routes returned 200. A fresh MCP session on port 8888 listed 67 tools and 24 resources and completed a state read without error, then was closed.

On the final simulator, the browser started the selected Thorough flush. Shared +5/-5 buttons changed its displayed host duration 30→35→30 seconds, and native displayed the same running workflow. It completed naturally and both returned Ready. Quick (3 s, 7 mL/s) was restored without starting it; recipe/profile/dose/yield/grind stayed at their original values. The native Stop check remains the earlier espresso result, not this naturally completed flush. A retained Home preset row and the compact popup could both be visible after completion; this secondary presentation case remains within existing Home/common-control acceptance, not a new task or a passed parity claim.

All six platform build-only workflows were dispatched on the PR source branch. The first shared producer reached and built stock Multimedia, then failed because the all-OS SDK's `qt-cmake` wrapper lacks Unix execute permission. The workflow now invokes CMake with the same explicit Qt toolchain instead; corrected Linux run **39976321572** is in progress. Remaining native/platform artifact results are pending, not passes. No release tag, version bump or publication has occurred.

Qt Creator now has only the original root project open. Native and Wasm use its separate kits; CI builds the complete package in order. A local Qt Creator Build still builds its selected kit, so local Wasm then native builds remain two steps.

## Validation retained as incomplete

- Trusted HTTPS, physically separate host/client, phone/tablet, real-machine supervision and host-impact soak: tasks 1–4 and 28.3. Local HTTP/simulator evidence does not pass them.
- Complete keyboard/screen-reader and browser matrix, including the known Qt trackpad and accessibility issues: tasks 4.3–4.4, 26 and 28.3. Report these limitations in beta notes.
- Remaining file upload/download/restore/install workflows, media/cache cases, secondary action/dialog and common-component acceptance: their existing tasks 8, 15–26. Shared source inclusion is not runtime acceptance.
- Signed installer/clean-install checks and native CI results for every distributed platform: 9.1/9.3/28.2/28.4. A green local Debug build is not a signed-release check.

Keep the original outstanding tasks unchecked. Before merging, reconcile/archive an accurately scoped completed slice under CLAUDE.md's OpenSpec merge rule; do not archive the complete parity change as achieved. The PR must distinguish experimental-beta implementation from the remaining validation holds.
