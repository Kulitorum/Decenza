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
- [x] Render the real EspressoPage, ShotGraph and PORTAL overlay offscreen with inert backend fixtures at tablet, large-scale and compact sizes. Verify both overlap mutations fail.
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
- [ ] Verify preservation of the custom JSON extension by the external Visualizer service, or retain it as an explicitly unsupported integration path.

## Documentation and Review

- [x] Draft a short wiki manual entry covering connection, graphs and raw EC meaning.
- [ ] Publish the manual through the maintainer's wiki workflow (the wiki is a separate repository).
- [x] Prepare representative live-shot and saved-history screenshots on the contributor fork, without private backups or settings.
- [x] Open draft PR [#1938](https://github.com/Kulitorum/Decenza/pull/1938) with completed validation and remaining hardware checks clearly distinguished.
- [ ] Before any requested merge, reconcile acceptance, archive this OpenSpec change and read checks on that final revision.
