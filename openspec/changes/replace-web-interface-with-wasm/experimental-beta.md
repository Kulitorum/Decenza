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

Final paired builds, full suite, embedded-bundle runtime, confirmation pointer/ID checks, and branch CI results will be recorded here before the PR is described as beta-ready. No release tag, version bump or publication is part of PR preparation.

## Validation retained as incomplete

- Trusted HTTPS, physically separate host/client, phone/tablet, real-machine supervision and host-impact soak: tasks 1–4 and 28.3. Local HTTP/simulator evidence does not pass them.
- Complete keyboard/screen-reader and browser matrix, including the known Qt trackpad and accessibility issues: tasks 4.3–4.4, 26 and 28.3. Report these limitations in beta notes.
- Remaining file upload/download/restore/install workflows, media/cache cases, secondary action/dialog and common-component acceptance: their existing tasks 8, 15–26. Shared source inclusion is not runtime acceptance.
- Signed installer/clean-install checks and native CI results for every distributed platform: 9.1/9.3/28.2/28.4. A green local Debug build is not a signed-release check.

Keep the original outstanding tasks unchecked. Before merging, reconcile/archive an accurately scoped completed slice under CLAUDE.md's OpenSpec merge rule; do not archive the complete parity change as achieved. The PR must distinguish experimental-beta implementation from the remaining validation holds.
