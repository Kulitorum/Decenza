## Why

Half Decent Scale owners can already run a signed WiFi update from the scale, but Decenza does not tell them when one is available or offer a convenient entry point. Surface the compatible published update beside the selected HDS so an owner can start the safe device-side flow without routinely visiting the scale's setup menu.

OpenScale [PR #165](https://github.com/decentespresso/openscale/pull/165) is merged and carried by `v3.1.14-preview.1`. It lets a connected app name the release to install, so the update no longer requires the owner to re-select that release on the scale's OLED picker and hold a button to confirm it. It also adds the same start command to the WiFi WebSocket, which makes an HDS connected over WiFi updatable at all.

## What Changes

- Fetch the same OpenScale release manifest the HDS uses when Decenza launches and after a genuine app resume; retain it in memory for the session rather than polling.
- When the current connected scale is an HDS over Bluetooth, USB, or WiFi, compare its known firmware version against the manifest's eligible published releases.
- Leave Settings → Connections visually unchanged when there is no update, no applicable HDS, or no usable manifest.
- Show an **Update** action beside **Forget** only when an update is available. Its confirmation dialog shows the selected release's GitHub release notes and explains what will happen.
- Name the release in the start command so the scale installs it unattended: the three-byte version payload on the existing Bluetooth and USB opcode, and the version argument on the WiFi `wifi_update` control command.
- Send the versioned form unconditionally. Firmware predating PR #165 ignores the payload and starts its own picker, which is the correct graceful fallback, so Decenza needs no capability negotiation and no version gate on the command itself.
- Add HDS update support to the WiFi scale transport, which currently reads the firmware version for diagnostics only and exposes no update path.
- Treat the scale's acknowledgement as *queued*, not installed. The scale still fetches and verifies its own signed manifest and assets, still refuses a release its own eligibility rules would not offer, and reports catalog-level refusals on its display and serial log only. Decenza must not claim installation completed until the scale reconnects on the target version.

## Capabilities

### New Capabilities

- `hds-firmware-update`: Discover and present compatible HDS firmware releases, and start a named, unattended install on the selected scale over Bluetooth, USB, or WiFi.

### Modified Capabilities

- `settings-ui`: Add the conditional HDS update action and confirmation dialog to Settings → Connections.

## Impact

- New Qt/C++ update controller, manifest parser, GitHub release-notes client reuse, and tests.
- `ScaleDevice` / HDS Bluetooth and USB drivers expose the firmware version and a version-carrying OTA trigger to the controller.
- `DecentScaleWifi` gains firmware-version reporting and an OTA trigger over its existing `/snapshot` WebSocket; it has neither today.
- A shared encoder builds the biased three-byte version payload once, so the Bluetooth and USB drivers do not each hand-roll the wire format.
- `SettingsConnectionsTab.qml` gains only a conditional action and dialog; no new normal-state page content.
- Uses the existing application `QNetworkAccessManager` and the OpenScale public manifest and release APIs.
- Manual verification against a real HDS is now possible: `v3.1.14-preview.1` is published and stable `3.1.13` is an eligible signed downgrade, so both the availability and the install paths are reachable on hardware.
- Upstream, flagged by PR #165 as its own follow-up: OpenScale's `buildLedResponsePacket()` gives minor and patch one nibble each, so a `3.1.16` scale would report itself as `3.2.0` over Bluetooth and USB. Exact through `3.1.15`, and its errors round up, so it costs an offer rather than causing a wrong one.
