## 1. Bundled Firmware Assets

- [x] 1.1 Add `resources/firmware/de1/de1-1352.bin` from `https://github.com/decentespresso/decaid/blob/main/assets/firmware/de1/de1-1352.bin`.
- [x] 1.2 Add `resources/firmware/de1/de1-1358.bin` from Decaid PR #594 blob `assets/firmware/de1/de1-1358.bin`.
- [x] 1.3 Pull in Decaid's firmware manifest for the bundled DE1 entries, preserving build, channel, asset path, byte length, SHA-256, expected header fields, release notes, supported models, and provenance.
- [x] 1.4 Register the firmware `.bin` files and manifest in the Qt resource/CMake packaging so they are present on Windows, macOS, Linux, Android, and iOS.

## 2. Firmware Source Logic

- [x] 2.1 Replace CDN URL/channel selection in the firmware asset source with bundled Stable and Early access catalog selection.
- [x] 2.2 Make availability checks classify Newer/Same/Older by comparing the connected DE1 version to the selected bundled catalog entry, without network access.
- [x] 2.3 Make update initiation load the selected bundled firmware image, validate size/header/digest against the manifest, and emit the existing ready-to-flash result to `FirmwareUpdater`.
- [x] 2.4 Remove or retire obsolete ETag, HEAD, Range, resume, sidecar, and remote-download behavior from the default firmware path.
- [x] 2.5 Ensure invalid or missing bundled metadata/file failures are non-retryable and occur before any BLE write.
- [x] 2.6 Preserve existing flash-state behavior for upgrade, downgrade, reflash, retry after BLE failure, simulator mode, and manual reboot.
- [x] 2.7 Expose the selected bundled firmware entry's release notes, version label, and channel label to QML.

## 3. Settings And UI

- [x] 3.1 Replace the historical `firmware/nightlyChannel` setting with `firmware/EA`: a one-time upgrade removes the old key, resets the new setting to Stable, stamps completion, and covers reset/idempotence/new-Early-access selections with tests.
- [x] 3.2 Rename the Settings -> Firmware toggle fallback, translation key usage if appropriate, accessibility name, and explanatory note from nightly wording to "Use early access firmware".
- [x] 3.3 Update the firmware source note to say firmware is bundled with Decenza and no longer fetched from Decent's CDN.
- [x] 3.4 Add release-note display for the selected bundled firmware entry, keeping it tied visually to the selected channel/version.
- [ ] 3.5 Verify the Firmware tab still presents installed, available, downgrade, reflash, progress, failure, simulator, awaiting-reboot, and release-note states correctly with the new channel labels.

## 4. Tests

- [x] 4.1 Add manifest/catalog tests that assert both bundled entries exist, map Stable -> 1352 and Early access -> 1358, and carry the expected Decaid metadata including release notes.
- [x] 4.2 Add bundled-file validation tests that read both resource images and verify byte length, SHA-256, board marker, header version, body byte count, and CPU byte count.
- [x] 4.3 Replace network cache helper/source tests that are no longer meaningful with bundled-source tests for Newer, Same/reflash, Older/downgrade, channel switch, and invalid catalog/file failure.
- [x] 4.4 Update firmware updater tests only where assumptions mention remote download/cache behavior; keep BLE flash-state tests otherwise unchanged.
- [x] 4.5 Add or update QML-facing tests/coverage so release notes for the selected bundled firmware are exposed to the Firmware tab.
- [ ] 4.6 Run the firmware-focused test set through Qt Creator MCP.

## 5. Documentation

- [x] 5.1 Update `docs/CLAUDE_MD/FIRMWARE_UPDATE.md` to describe bundled Stable/Early access firmware, Decaid manifest validation, release-note display, no CDN fetch, and the renamed toggle.
- [x] 5.2 Update the GitHub wiki manual Firmware section with a short user-facing note: where to find the toggle, Stable is bundled 1352, Early access is bundled 1358, release notes are shown in the Firmware tab, and flashing still requires keeping the app open and the DE1 connected.

## 6. Verification

- [ ] 6.1 Run the full local test suite through Qt Creator MCP before PR handoff.
- [ ] 6.2 Manually inspect or screenshot the Settings -> Firmware tab to confirm "Early access" wording, visible release notes, and no remaining visible "nightly" firmware text.
- [x] 6.3 Confirm release/package contents include both bundled firmware binaries and the Decaid manifest.
