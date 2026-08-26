## Why

Half Decent Scale owners can already run a signed WiFi update from the scale, but Decenza does not tell them when one is available or offer a convenient entry point. Surface the compatible published update beside the selected HDS so an owner can start the existing safe device-side flow without routinely visiting the scale's setup menu.

## What Changes

- Fetch the same OpenScale release manifest the HDS uses when Decenza launches and after a genuine app resume; retain it in memory for the session rather than polling.
- When the current connected scale is an HDS over Bluetooth or USB, compare its known firmware version against the manifest's eligible published releases.
- Leave Settings → Connections visually unchanged when there is no update, no applicable HDS, or no usable manifest.
- Show an **Update** action beside **Forget** only when an update is available. Its confirmation dialog shows the selected release's GitHub release notes and explains the handoff.
- Start the existing HDS WiFi OTA command over the selected Bluetooth or USB transport. The initial release retains the HDS's on-device version selection and confirmation; Decenza must not claim that installation completed until the scale reconnects on the selected version.
- Record, but do not block this feature on, a follow-up OpenScale capability for GUI-driven version selection, confirmation, and OTA-progress reporting. The scale must continue to fetch and validate its own signed manifest and assets.

## Capabilities

### New Capabilities

- `hds-firmware-update`: Discover and present compatible HDS firmware releases, hand the existing OTA flow to the selected scale, and define the non-blocking follow-up for remote OTA controls.

### Modified Capabilities

- `settings-ui`: Add the conditional HDS update action and confirmation dialog to Settings → Connections.

## Impact

- New Qt/C++ update controller, manifest parser, GitHub release-notes client reuse, and tests.
- `ScaleDevice` / HDS Bluetooth and USB drivers expose the existing firmware-version and OTA-trigger capability to the controller.
- `SettingsConnectionsTab.qml` gains only a conditional action and dialog; no new normal-state page content.
- Uses the existing application `QNetworkAccessManager` and the OpenScale public manifest and release APIs.
- Follow-up coordination with `../openscale` is documented but does not gate this Decenza change or require a new HDS firmware release.
- Manual UI and Bluetooth/USB hardware verification of an actually available update is deferred until the next compatible HDS firmware release is public; this does not block merging the availability and handoff implementation.
