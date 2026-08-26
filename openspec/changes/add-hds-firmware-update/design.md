## Context

The OpenScale HDS firmware already implements signed WiFi pull OTA. Its Bluetooth and USB command protocol has a WiFi-update trigger, but its current updater reads the scale's physical buttons for release selection and confirmation. Decenza already has a shared network manager and GitHub release-note retrieval logic, while scale firmware versions arrive from existing HDS transport packets.

See proposal.md and the hds-firmware-update spec for the behavioral contract.

## Goals / Non-Goals

**Goals:**

- Make an available HDS update discoverable without adding normal-state Connections UI.
- Fetch only at launch and on real application resume; reuse a successful manifest for all scale changes in the session.
- Give the user release notes and an explicit confirmation before dispatching the existing HDS command.
- Preserve the HDS as the cryptographic and hardware-compatibility authority.

**Non-Goals:**

- Downloading, proxying, or validating HDS firmware assets in Decenza.
- Adding a recurring polling timer, persisted availability state, or an update banner.
- Blocking this feature on remote version selection, confirmation, or progress support from a future HDS firmware.

## Decisions

### Use the HDS manifest for availability and the release API for prose

Read the same `releases/latest/download/manifest.json` catalog URL that the HDS reads. It supplies the eligible release versions and compatibility metadata. Fetch the selected release's GitHub release body through shared release-client code for the dialog, because the manifest supplies a release-notes URL but not the Markdown itself.

The scale independently retrieves and verifies the manifest signature and assets when it starts OTA. Decenza's manifest read is an advisory UI input, not the authorization to install firmware.

### Tie requests to lifecycle edges, not an interval

Start one manifest request during app initialization and another only after a Suspended → Active lifecycle transition. Retain the last good result in memory. Scale selection and connection changes only re-evaluate that cached catalog, which avoids network traffic when the user changes scales.

The existing app updater's hourly cadence is too frequent, and the DE1 firmware updater's startup-plus-weekly cadence is too infrequent. Reuse their shared network and lifecycle infrastructure rather than either schedule.

### Add an explicit HDS update capability to the active scale surface

Expose the existing HDS firmware version and a capability-safe `startFirmwareUpdate` operation from the active Bluetooth and USB HDS drivers. A controller follows the active scale and clears its state immediately on target or connection changes. This avoids treating generic scale names or raw command bytes as QML-facing behavior.

### Keep the current HDS interaction as the initial completion path

After Decenza dispatches the existing trigger, it explains that the HDS display owns release selection and hold-to-confirm. No acknowledgement currently proves completion over the host transport, so reconnecting on the target version is the only positive outcome Decenza can infer.

The alternate design—using Decenza to emulate the HDS's physical buttons—is not viable: the OTA picker reads device GPIO directly and suppresses normal transport input while running.

### Treat remote GUI control as a separate OpenScale protocol follow-up

A future OpenScale capability can accept a requested semantic version and emit state/progress over Bluetooth/USB. It must have the HDS fetch its own signed catalog, locate the exact compatible release, and validate all assets; Decenza must never provide URLs, hashes, or firmware bytes. Keep the physical-button path for compatibility. This work is deliberately recorded as a dependency-free follow-up rather than a prerequisite.

## Risks / Trade-offs

- [Manifest and release notes can be stale for the session] → HDS rechecks its signed catalog at installation time; a stale Decenza offer cannot bypass device validation.
- [Network errors leave no visible result] → This is intentional: the normal Connections surface stays quiet. Log diagnostics and try again on the next launch or resume.
- [Current HDS firmware gives no end-to-end host progress] → Use precise handoff copy and re-evaluate installed version after reconnection; defer rich progress to the OpenScale protocol follow-up.
- [Future hardware metadata may outgrow the host's eligibility view] → Treat HDS validation as final and keep the host's presentation constrained to manifest candidates it can identify.

## Migration Plan

1. Release Decenza with the conditional availability control and existing HDS command handoff.
2. After the next compatible HDS firmware release is public, validate on Bluetooth and USB HDS devices that the current on-device picker remains unchanged and update failure leaves the installed firmware running.
3. In a later OpenScale release, add remote update capability/version reporting and progress events.
4. Extend Decenza only after that capability is available; retain the current device-display flow for older HDS firmware.
