## Implementation

- [x] Decode the public measurement format with exact lengths, finite floats and raw-field semantics.
- [x] Add independent PORTAL transport, shared discovery, saved selection and bounded idle reconnect.
- [x] Integrate connection controls into the existing scale/sensor panel.
- [x] Add capability-checked optional display synchronization without claiming saved-session confirmation.
- [x] Record notifications on the extraction/settling clock and retain interrupted-stream gaps.
- [x] Add optional live/history/last-shot curves, independent axes and visibility settings.
- [x] Preserve paired samples in blobs, projections, local JSON export and record import; keep old shots sparse.
- [x] Preserve PORTAL pairing/display preference in settings backup/import.
- [x] Separate the production contribution from private test-app identity, signing and migration scaffolding.
- [x] Correct the live readout/header and view-button/axis overlaps.

## Automated Validation

- [x] Add decoder, discovery/lifecycle, display-command, timing and persistence regression tests; verify deliberate mutations fail and restored sources pass.
- [x] Add one hardware packet fixture with screenshot provenance and rounded display expectations.
- [x] Local check (fixture outside the repository): render the real EspressoPage, ShotGraph and PORTAL overlay offscreen with inert backend fixtures at tablet, large-scale and compact sizes. Verify both overlap mutations fail.
- [x] Verify the PORTAL settings backup regression and its mutation.
- [x] Run all 118 desktop CTest entries with ASan/UBSan, including both sanitizer canaries (Qt 6.11.2, Clang 23, Xcode libc++ 20 headers).
- [x] Build the production Android arm64 APK with Qt 6.11.2.
- [x] Pass qmllint for all 244 QML files and every repository text-invariant gate.
- [x] Validate OpenSpec strictly and review the final contribution diff.

## Hardware Evidence and Remaining Acceptance

- [x] Owner confirms discovery, connection, live curves and shot-history curves on Android.
- [x] Confirm stored samples from two short extractions remain available after app restart.
- [x] Owner confirms the graph appears on PORTAL's own display.
- [ ] Check persistent sessions on the PORTAL itself; do not claim this behavior before verification.
- [ ] Complete normal target-weight and manual-stop shots with machine, scale and PORTAL together.
- [ ] Exercise controlled PORTAL loss/reconnect and machine sleep/wake on hardware.
- [ ] Verify a PORTAL-containing backup/restore round trip on the tablet.
- [ ] Check the corrected layout on the physical tablet.
- [x] Omit the unverified PORTAL extension from Visualizer uploads while preserving local export/record import.

## Documentation and Review

- [x] Draft a short wiki manual entry covering connection, graphs and raw EC meaning.
- [ ] Publish the manual through the maintainer's wiki workflow (the wiki is a separate repository).
- [x] Prepare representative live-shot and saved-history screenshots on the contributor fork, without private backups or settings.
- [x] Open draft PR [#1938](https://github.com/Kulitorum/Decenza/pull/1938) with completed validation and remaining hardware checks clearly distinguished.
- [x] Reconcile acceptance and archive this OpenSpec change as the final commit. Checks on that revision are read by the maintainer before merge; remaining hardware acceptance is recorded under "Held at merge" below and is deliberately not marked passed.

## PR #1938 Review

- [x] Preserve the non-owner UI, shared scanning and translated section heading; reveal PORTAL controls only after verified connection.
- [x] Move pairing/display preferences into SettingsHardware and test live backup restoration with a connected driver.
- [x] Move capture and phase policy into PortalController and share the freshness/gap threshold.
- [x] Collapse routine packet/state logging with flushes at shot end and faults through a dedicated subsystem helper.
- [x] Append live graph points incrementally and retain constant-time EC bounds.
- [x] Support PORTAL colors in dark/light palettes, flash highlighting and app/web color pickers.
- [x] Remove redundant startup scanning, respect synchronous Qt teardown and asynchronous native cancellation, report failed display writes and avoid irrelevant last-shot cache invalidation.
- [x] Correct stale graph-series comments.
- [x] Validate the review revision: 118/118 native tests with ASan/UBSan, production Android arm64 APK, 244/244 QML files, all text gates, 7 local offscreen UI checks (fixture outside the repository) and three detected/restored mutations (live settings, log collapse, EC append).

## Follow-up Review of d406e54

- [x] Replace overlapping connection flags with an enum state and expose native disconnect completion in the transport contract; match Qt's silent synchronous teardown in the fake.
- [x] Create the platform transport lazily and retain native callback identity checks during cancellation.
- [x] Remove the unnecessary measurement read, keep streaming after optional display errors, and handle subscription failure plus first-notification timeout from radio dispatch.
- [x] Track pending display commands in order, clear status on teardown, and surface failed synchronization outside diagnostics.
- [x] Keep first-connection progress/errors visible after explicit selection; prevent a silent second selection while connecting.
- [x] Preserve real disconnect/error/recovery logs, include malformed packet length/hex, and flush recurring events at natural boundaries.
- [x] Assert automatic graph-on/off and manual control after actual shot finalization.
- [x] Default history JSON to the Visualizer format; explicitly opt local exporters into PORTAL.
- [x] Fix the theme null guard, include PORTAL in the app editor and palette generator, and share sample validity and EC bounds.
- [x] Validate this follow-up revision: 118/118 native tests with ASan/UBSan, 244/244 QML files, all text gates, strict OpenSpec, 7 local offscreen checks, three detected/restored lifecycle mutations, and the private Android arm64 test APK.
- [ ] Confirm reconnection, optional display failure and notification recovery on the physical tablet; verify native Apple cancellation on hardware separately.

## History inspection and status-bar reconnect

- [x] Add PORTAL EC (raw) and outlet-temperature readouts to the history/review cursor, respecting visibility, gaps and temperature units.
- [x] Add an owner-only droplet/status beside the status-bar scale, reusing the idle reconnect path without changing saved layouts or the widget palette.
- [x] Extend the wiki manual draft; publication remains with the maintainer.
- [x] Validate cursor boundaries, gaps and their deliberate mutation; 118/118 native tests, 245/245 QML files, all text gates, strict OpenSpec, 14 local offscreen QtTest results and the private Android arm64 APK.
- [ ] Verify finger scrubbing and status-bar reconnection on the physical tablet.

## Refill reconnect and live status EC

- [x] Reproduce the tablet failure from the persisted log: startup connection, DE1 Idle-to-Refill transition, PORTAL link loss, scans without another connection attempt.
- [x] Allow PORTAL connection during Refill while retaining extraction/active-operation guards.
- [x] Show current raw EC in the status bar and omit stale/disconnected values.
- [x] Validate both controller/discovery replay paths and their failing Refill mutation, live QML EC and its failing-label mutation, 118/118 native tests, 245/245 QML files, all text gates, strict OpenSpec, 15 local offscreen QtTest results and the Android arm64 test build.
- [ ] Verify reconnection during Refill and changing status-bar EC on the physical tablet.

## Maintainer review fixes (2026-09-18)

- [x] Keep users without a PORTAL unchanged: DE1 classification runs before the loose PORTAL name match, and the name match yields to scale/refractometer classification.
- [x] Stop clearing every Apple scale transport's reconnect target on disconnect; add `ScaleBleTransport::forgetTarget()` (no-op by default) and call it only from PORTAL's Forget.
- [x] Connections tab: a nearby unpaired PORTAL affects the discovered list only after that tab started a scan; construct the PORTAL panel through a `Loader`; do not notify on clearing an empty device list.
- [x] One definition of ownership (`BelkaPortal.owned`) replaces six per-site spellings; add `[PORTAL]` to the Connections log view.
- [x] Make both PORTAL curves advanced-mode series across legend, chart, cursor and inspect bar.
- [x] Apply only present PORTAL fields on settings import.
- [x] Give `errorMessage` its own change signal, stop the health timer once stale, log refused actions while busy, collapse malformed packets per length, and count/log PORTAL samples dropped on history load.
- [x] Show the PORTAL legend on the auto-favorite info page, share the EC axis padding constant, derive temperature ticks from the axis, and label raw transport errors.
- [x] Add regression tests for the above, including a committed source guard for the non-owner gating; make the overlay-instantiation check fail instead of skipping. One deliberate mutation (empty-list notification) was confirmed to fail; the others were not mutation-checked.
- [x] Validate: builds on the rebased branch; 118/118 native tests with ASan/UBSan; 248/248 QML files; strict OpenSpec. The maintainer ran the app without a PORTAL on macOS: no `[PORTAL]` log lines, no QML errors, and a Bluetooth HDS scale discovered and streaming. Rendered Settings, graph and legend pages were not inspected by the assistant.

## Held at merge

The hardware items left unchecked above are held by the maintainer's decision to merge into the rolling 2.0 beta without them. They affect only owners of a PORTAL; users without one never reach these paths. They are not marked passed. Follow-up: the contributor confirms them on the tablet in the beta and reports results on PR #1938 or a new issue if a defect is found. The wiki manual entry is likewise held until the maintainer publishes it. Apple CoreBluetooth changes shared with every scale are compiled on macOS and exercised only through a successful scale connect; disconnect, timeout and Bluetooth power-cycle paths are untested on hardware.
