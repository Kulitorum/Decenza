## Why

Decenza currently depends on Decent's de1app CDN at update time, so firmware availability and bytes can change outside our release process. Bundling known DE1 firmware builds lets each Decenza release offer a pinned stable image and an explicit opt-in early access image without relying on the CDN path at the moment a user flashes their machine.

## What Changes

- Bundle DE1 firmware build 1352 as the default Stable firmware image, sourced from `decentespresso/decaid` `assets/firmware/de1/de1-1352.bin`.
- Bundle DE1 firmware build 1358 as the opt-in Early access firmware image, sourced from `decentespresso/decaid` PR #594 `assets/firmware/de1/de1-1358.bin`.
- Replace the firmware channel source from remote CDN URLs to Decaid's bundled firmware manifest, included in Decenza with the firmware assets and carrying version, expected size/header metadata, digest, channel, release notes, and provenance.
- Show release notes from the bundled manifest in the app so users can see what the selected Stable or Early access firmware changes before flashing.
- Rename the Settings -> Firmware toggle from "Use nightly firmware channel" to "Use early access firmware" and replace its historical preference with a new `firmware/EA` setting. A one-time upgrade removes the historical nightly preference and sets the new setting to Stable for every existing install.
- Keep the existing flash state machine, downgrade/reflash affordances, simulator safety gate, validation, retry, and BLE procedure unchanged except for where the selected firmware image is loaded from.
- Update the firmware updater documentation and the end-user manual entry for the renamed channel and bundled source.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `firmware-update`: Firmware availability, download/loading, channel selection, and release-note display change from CDN-backed stable/nightly channels to Decaid-manifest-backed bundled stable/early-access firmware images.

## Impact

- Affects `src/core/firmwareassetcache.{h,cpp}`, `src/controllers/firmwareupdater.{h,cpp}` if source abstractions change, `src/core/settings_app.{h,cpp}`, `qml/pages/settings/SettingsFirmwareTab.qml`, firmware tests, resource/CMake packaging, `docs/CLAUDE_MD/FIRMWARE_UPDATE.md`, and the wiki manual.
- Adds two binary firmware resources to the distributed application; release size increases by roughly the sum of both `.bin` images.
- Removes runtime dependency on `fast.decentespresso.com` for DE1 firmware bytes while preserving BLE update behavior and safety semantics.
