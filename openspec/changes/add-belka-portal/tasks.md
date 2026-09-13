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
- [ ] Before any requested merge, reconcile acceptance, archive this OpenSpec change and read checks on that final revision.

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
