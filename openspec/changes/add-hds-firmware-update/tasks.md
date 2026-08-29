## 1. HDS update data and transport capability

- [x] 1.1 Expose the selected Bluetooth and USB HDS firmware version plus an explicit WiFi-update capability/trigger, and verify focused protocol tests cover version decoding and the existing update command packet on both transports.
- [x] 1.2 Add the HDS manifest catalog model and eligibility parser for the OpenScale release-manifest shape, and verify fixture tests cover newer, current, incompatible, and `min_from`-blocked releases.
- [x] 1.3 Implement the session-scoped HDS update controller using the shared network manager and GitHub release-note client, and verify launch, genuine suspend/resume, cache reuse, cancellation, and network-failure behavior in unit tests.

## 2. Connections experience

- [x] 2.1 Wire the controller to the active-scale lifecycle so only the selected connected Bluetooth or USB HDS can expose an eligible release, and verify target changes or disconnects clear availability without a stale command.
- [ ] 2.2 Add the conditional Update button beside Forget and its accessible confirmation dialog, and verify no-update, available-update, Cancel, and selected-scale-change flows in QML-focused tests or manual UI checks. The button and dialog are built; the available-update path is verified under task 5.2, which `v3.1.14-preview.1` now makes reachable.
- [x] 2.3 Dispatch the HDS WiFi-update command only after dialog confirmation, and verify a command send is never represented as install success.

## 3. Verification and documentation

- [x] 3.1 Add concise Settings manual guidance for the HDS update handoff; verify the wiki entry follows the project's short-form manual convention.
- [ ] 3.2 Manually verify the Connections page is unchanged when no HDS update is available. Automated Qt Creator coverage for this section is complete; the suite re-runs under task 5.1 once section 4 lands.

## 4. Targeted, unattended install (OpenScale PR #165)

OpenScale [PR #165](https://github.com/decentespresso/openscale/pull/165) is merged and carried by `v3.1.14-preview.1`. Naming the release in the start command installs it with no on-OLED picker and no hold-to-confirm, and the same command is now available over the WiFi WebSocket.

- [ ] 4.1 Add one shared encoder for the three-byte target-version payload (`0x80 | value`, components capped at 127) beside the existing Decent packet helpers, and verify unit tests cover encoding, the 127 cap, and that no component ever encodes to a byte below `0x80`. Neither transport driver may hand-roll the bias.
- [ ] 4.2 Extend `startFirmwareUpdate` to carry the target version through `ScaleDevice`, `ScaleDeviceProxy`, and the controller, sourcing it from the already-resolved available release. Send it unconditionally, with no gate on the reported firmware version, and verify a request with no resolved version is refused rather than sent bare.
- [ ] 4.3 Send the versioned command on Bluetooth in the existing seven-byte packet, and on USB as the exact five-byte frame the firmware's framer expects rather than a padded packet. Verify both wire forms in protocol tests, including that the USB write leaves no trailing bytes.
- [ ] 4.4 Give `DecentScaleWifi` firmware-version reporting and update support: expose the version it already parses, implement `startFirmwareUpdate` as the `wifi_update <version>` control command on the existing `/snapshot` WebSocket, and handle the `ota_version_invalid` and `ota_busy` refusals. Verify with focused tests against the documented WebSocket contract.
- [ ] 4.5 Include a WiFi-connected HDS in the controller's active-scale eligibility so the Update action appears for it, and verify availability clears on target change or disconnect exactly as it does for Bluetooth and USB.
- [ ] 4.6 Replace the device-display handoff copy: the dialog states that the update has started on the scale, never that it is installed, and no longer directs the user to the scale's display. Verify the started state and the absence of any completion claim.
- [ ] 4.7 Update the wiki manual entry to match — the update now completes without touching the scale, and works over WiFi as well as Bluetooth and USB. Keep it to the project's short-form convention.

## 5. Hardware verification

- [ ] 5.1 Run the full test suite through Qt Creator.
- [ ] 5.2 Verify on hardware against `v3.1.14-preview.1`, where stable `3.1.13` is an eligible signed downgrade and an install can be driven to completion: on Bluetooth, USB, and WiFi, confirm no picker appears, the scale reconnects on the target version, and a failed update leaves the installed firmware running.
- [ ] 5.3 Verify the fallback against firmware predating PR #165: the versioned command reaches that scale's own picker with no power-off, timer, or other side effect.

## Upstream follow-up — not part of this change

OpenScale's `buildLedResponsePacket()` packs the reported version as one nibble each for minor and patch, so a `3.1.16` scale reports itself as `3.2.0` over Bluetooth and USB. Decenza's `decodeHdsFirmwareVersion` inherits that, and both our eligibility comparison and the scale's already-installed refusal read it. PR #165 records the fix as its own upstream change. Correct through `3.1.15`; re-check the eligibility comparison once a release past that exists.
