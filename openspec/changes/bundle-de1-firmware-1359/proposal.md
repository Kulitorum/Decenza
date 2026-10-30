## Why

Decent made DE1 build 1359 for Decenza. For 5 seconds after the machine goes from Espresso to Idle, it accepts a BLE HotWater request without a tap on the group head controller. That is what a one-click americano / long black needs. 1358 has been on Early access since 2026-10-08 and moves to Stable.

## What Changes

- Stable becomes build 1358 (unchanged image, de1app commit `7e94ef3c`).
- Early access becomes build 1359, a Decenza-specific build from Decent that is not published in de1app or decaid.
- Build 1352 leaves the bundle, since no channel uses it.
- A channel is now the manifest entry whose `channel` field is `stable` or `early-access`, replacing artifact ids hard-coded in C++. Future rotations then need only a manifest edit.

## Capabilities

### Modified Capabilities

- `firmware-update`: the builds bundled on each channel, and how a channel resolves to a build.

## Impact

`resources/firmware/` (manifest and images), `resources/resources.qrc`, `src/core/firmwareassetcache.{h,cpp}`, the firmware tests, `docs/CLAUDE_MD/FIRMWARE_UPDATE.md`. App size grows by about 1 KB net.
