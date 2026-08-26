## 1. HDS update data and transport capability

- [x] 1.1 Expose the selected Bluetooth and USB HDS firmware version plus an explicit WiFi-update capability/trigger, and verify focused protocol tests cover version decoding and the existing update command packet on both transports.
- [x] 1.2 Add the HDS manifest catalog model and eligibility parser for the OpenScale release-manifest shape, and verify fixture tests cover newer, current, incompatible, and `min_from`-blocked releases.
- [x] 1.3 Implement the session-scoped HDS update controller using the shared network manager and GitHub release-note client, and verify launch, genuine suspend/resume, cache reuse, cancellation, and network-failure behavior in unit tests.

## 2. Connections experience

- [x] 2.1 Wire the controller to the active-scale lifecycle so only the selected connected Bluetooth or USB HDS can expose an eligible release, and verify target changes or disconnects clear availability without a stale command.
- [ ] 2.2 Add the conditional Update button beside Forget and its accessible confirmation dialog, and verify no-update, available-update, Cancel, and selected-scale-change flows in QML-focused tests or manual UI checks. Deferred until the next compatible published HDS firmware release makes the available-update path testable.
- [x] 2.3 Dispatch the existing HDS WiFi-update command only after dialog confirmation and present accurate device-display handoff copy, and verify a command send is never represented as install success.

## 3. Verification and documentation

- [x] 3.1 Add concise Settings manual guidance for the HDS update handoff and its requirement to finish selection and confirmation on the scale display; verify the wiki entry follows the project’s short-form manual convention.
- [ ] 3.2 Run the relevant Qt Creator test targets and the full test suite, then manually verify the Connections page is unchanged when no HDS update is available and the dialog works over both Bluetooth and USB. Automated Qt Creator coverage is complete; Bluetooth/USB hardware verification is deferred until the next compatible published HDS firmware release.

## Deferred follow-up — not part of this change

The OpenScale repository will later define a remote HDS OTA protocol for GUI-driven release selection, confirmation, and progress. It must preserve device-side signed-manifest and asset verification. This does not gate implementation, testing, completion, or release of the Decenza handoff feature above.
