# Technical Design

## Context

Decenza already has:

- A `DE1Device` BLE layer with MMR read/write (`src/ble/de1device.{h,cpp}`) including an MMR-verify mechanism, MMR-value dedupe cache, and firmware-version parsing from `MMR 0x800010`
- A `BleTransport` layer that manages subscriptions and a per-characteristic write queue (`src/ble/bletransport.cpp`)
- The `FW_MAP_REQUEST` UUID (`0000A009-…`) declared at `src/ble/protocol/de1characteristics.h:26` but no code path that uses it

`de1app` uses three GATT characteristics for firmware update (A005 for version reads, A006 for firmware chunk writes with opcode `0x10`, and A009 for erase/verify control with notifications). Decenza already talks to A005 and A006 for MMR purposes; the state machine and A009 handling are missing.

Independent prior art exists: Kal Freese's `github.com/kalfreese/de1-firmware-updater` (Python, BSD-licensed) implements the same BLE protocol and ships in production. It is used as a cross-check during implementation.

## Goals / Non-Goals

**Goals:**
- Add firmware update as a first-class feature, matching `de1app`'s BLE flow
- Keep `DE1Device` thin (I/O only); keep the state machine in its own controller
- Ship to all platforms Decenza already supports (Windows, macOS, Linux, Android, iOS)
- Recover cleanly from any interruption with a one-tap user retry
- Detect the successful-but-silent case (DE1 rebooted during verify, BLE disconnected before we saw the success notify) and report `Succeeded` rather than `Failed`

**Non-Goals:**
- USB-serial firmware update (reaprime/Streamline path) — different transport, out of scope
- Auto-resume of partial uploads at the protocol level — de1app doesn't do it, DE1 side is untested for resume
- Bundling the firmware binary in the Decenza app — keeps install lean, decouples release cadences, avoids redistributing Decent's binary
- Signing or signature verification beyond what's already in the firmware header — out of scope until Decent publishes signed releases
- Firmware rollback or version pinning

## Decisions

### Decision: Firmware source is Decent's update CDN (`fast.decentespresso.com`), channel-selectable

URLs:

- Stable (default): `https://fast.decentespresso.com/download/sync/de1plus/fw/bootfwupdate.dat`
- Nightly (opt-in): `https://fast.decentespresso.com/download/sync/de1nightly/fw/bootfwupdate.dat`

**Why:**
- This is the same host Tcl de1app pulls from. `de1app/updater.tcl` switches between `de1plus`, `de1beta`, and `de1nightly` based on the `app_updates_beta_enabled` setting. Mirroring that endpoint means Decenza users receive exactly the firmware Decent ships through their own update mechanism.
- The GitHub branches (`main`, `stable`, `beta`, `nightly`) do **not** drive what end-users receive — `main` carries bleeding-edge dev blobs, the `nightly` branch has been untouched since 2023, and `stable`/`beta` trail the CDN. Pulling from the CDN is the only source of truth.
- `de1beta` is omitted deliberately: in practice Decent has stopped updating it and it tracks stable, so a three-way picker would mislead users into thinking it means something.
- The channel is user-selectable via `Settings::firmwareNightlyChannel` (persisted as `firmware/nightlyChannel`). Default is stable. Switching channels wipes the cache so the next check contacts the new endpoint fresh — ETags from one channel are meaningless against the other.
- Validation is anchored on the file's 64-byte header (`BoardMarker == 0xDE100001` plus file-size ≥ `ByteCount + 64`), not on the URL label. An unexpected bad file from either channel is caught before we touch the DE1. Payload-level checksum validation is pending a protocol question to Decent (see `TODO(firmware-crc)`); the DE1's verify-phase response is the ultimate gate.

### Decision: Architecture — separate controller, thin device additions

- `FirmwareUpdater` (new) owns the three-phase state machine as a peer of `SteamCalibrator` and `UpdateChecker` under `MainController`
- `FirmwareAssetCache` (new) handles HTTP download, header parsing, and `BoardMarker` / file-size validation — testable without BLE
- `DE1Device` gains three methods and one signal; zero state-machine logic inside it

Approaches considered and rejected:

- **Roll the state machine into `DE1Device`.** Rejected. `DE1Device` is already large (MMR verify, GHC detection, flow calibration, heater voltage). Adding a 3-phase update state machine would make it a bottleneck for concurrent features (reconnect mid-update, for example). Violates the "smaller, well-bounded units" guideline in the repo.
- **`FirmwareTransport` abstraction (BLE + future USB).** Rejected as premature abstraction. USB firmware update uses a different protocol, not just a different transport backend. Adding the interface now costs complexity for a migration that may never happen.

### Decision: MMR dedupe cache bypass for firmware writes

`DE1Device::writeMMR` populates `m_lastMMRValues` to elide identical re-writes. Firmware chunk writes must **not** go through this path:

- A full firmware is ~28,000 unique 16-byte chunks; populating the dedupe cache with firmware bytes would do nothing useful and bloat the hash.
- Firmware writes use opcode `0x10` to A006; regular MMR writes use different opcodes. Sharing the write path would conflate the two.

`writeFirmwareChunk()` calls `m_transport->write(...)` directly, bypassing `writeMMR` entirely. Unit tests verify that `m_lastMMRValues` is unchanged after firmware uploads.

### Decision: On-demand A009 subscription

A009 (FWMapRequest) notifications fire only during firmware updates. `BleTransport::onServiceDiscovered` currently subscribes to A005/A00A/A00D/A00E/A011 unconditionally; adding A009 there would leave a handler active for a channel that's silent 99.99 % of the time.

Instead, `FirmwareUpdater::startUpdate()` calls `DE1Device::subscribeFirmwareNotifications()` as the first BLE step and `unsubscribeFirmwareNotifications()` on terminal state transitions (Succeeded, Failed). This keeps the normal-operation BLE surface minimal and avoids any risk of spurious notifications during shots.

### Decision: Failure recovery — clean full-retry, no partial resume

Any failure (BLE disconnect, verify mismatch, timeout) restarts the full erase → upload → verify sequence from scratch. This matches `de1app` and keeps us out of protocol territory the DE1 side has never been tested against.

UX softens the full-retry cost:

- The home-screen banner persists as "Firmware update interrupted — tap to retry" until the user succeeds or explicitly cancels. Persisted via `firmware/inProgressBeforeFailure` in `QSettings` so it survives app restarts.
- Screensaver suppression and navigation guards remain in effect across failure → retry so the user doesn't walk away mid-process.
- The only non-retryable failures are "invalid firmware file" (bad `BoardMarker`) and "URL moved" (HTTP 4xx on the CDN). In those cases Decenza stops retrying until the next app restart, avoiding a tight retry loop against a bad server state. Truncated downloads are retryable since they typically reflect a transient network condition.

### Decision: Verify-disconnect is ambiguous — check version, not just notification

A DE1 reboots automatically after a successful verify. If BLE disconnects right at that moment, we may not see the success notification. The design treats this case specifically:

- On disconnect during `Verifying`, enter a short `Verifying (ambiguous)` substate (not a separate public `State`) with a 15 s grace timer.
- If BLE auto-reconnects within 15 s and the post-reconnect firmware version matches the just-flashed version → transition to `Succeeded`.
- If the grace timer expires or the version doesn't match → transition to `Failed` with the normal retry offer.

This avoids the pathological case where a successful update reports as failed, the user retries, and we erase + re-flash perfectly good firmware.

### Decision: Progress weighting (not linear over chunks)

Linear-over-chunks progress would pin at 0 % and 100 % for several seconds each while erase and verify ran. Weighting: Phase 1 (erase) 0–10 %, Phase 2 (upload) 10–90 %, Phase 3 (verify) 90–100 %. Motion stays continuous; the "Do not disconnect" message remains credible to the user.

### Decision: Auto-check cadence — weekly + startup only

Firmware ships every few months. Polling more often is wasteful:

- App startup: one HEAD check 30 s after main window shown (respecting `firmware/lastCheckedAt`; if < 168 h since last check, skip).
- Weekly: one `QTimer` armed at app start with `qMax(168h - elapsed, 30s)`. `QTimer` is millisecond-accurate up to ~24.8 days, so 168 h is safe.
- Manual: "Check now" button in `SettingsFirmwareTab` always performs a HEAD, bypassing the 168 h gate.

Check implementation: HEAD first with `If-None-Match`. On `304 Not Modified`, no further network I/O. On `200 OK` with a changed `ETag`, issue a `Range: bytes=0-63` GET to fetch only the firmware header (64 bytes) and parse the `BoardMarker` + `Version`. The full payload (~453 KB) is only downloaded when the user taps "Update now".

Header layout on the wire (all `u32` little-endian unless noted):

| Offset | Field | Notes |
|---|---|---|
| 0 | `CheckSum` | algorithm pending; not validated client-side (see `TODO(firmware-crc)`) |
| 4 | `BoardMarker` | must equal `0xDE100001` — the only client-side validation we gate on |
| 8 | `Version` | linear build number (current main: 1352 for "v1352") |
| 12 | `ByteCount` | payload size; file-size sanity is `fileSize >= ByteCount + 64` |
| 16 | `CPUBytes` | logged for diagnostics |
| 20 | `Unused` | always zero |
| 24 | `DCSum` | second checksum, algorithm pending |
| 28 | `IV` (32 bytes) | payload-decryption IV — opaque to Decenza, streamed verbatim |
| 60 | `HeaderChecksum` | checksum over first 60 bytes, algorithm pending |

The IV + HeaderChecksum fields are newer than de1app's `binary.tcl:379-390` spec. de1app's upload loop streams the entire file byte-for-byte from offset 0, letting the DE1 firmware do the decryption — Decenza does the same, so the 64-byte header layout has no special handling during upload.

No per-connect check. The original design considered it and was simplified per user feedback: DE1s reconnect frequently during normal use, and hammering Decent's CDN on every reconnect adds no value when firmware ships monthly at most.

### Decision: Platform scope — all of them, with one platform gate

`QLowEnergyController` abstracts the BLE differences across Windows, macOS, Linux, Android, iOS. One behavioral gate:

- **Android post-erase wait is 10 s; elsewhere 1 s.** Matches `de1app` (`de1_comms.tcl:913, 920`). Likely because Android's BLE stack queues writes asynchronously and the first firmware chunk can race the erase-complete notification on slower Android devices.
- Gate via `QOperatingSystemVersion::currentType() == QOperatingSystemVersion::Android`, not `#ifdef`. One binary behaves correctly whether running on a Decent Android tablet or on Linux desktop.

## Risks

- **Bricking a real DE1.** Mitigation: we cannot commit an automated test that flashes a real machine. Manual verification on maintainer hardware before merge, header validation before erase, precondition gates refusing to start during shots.
- **CDN serves a bad `.dat`.** Mitigation: `BoardMarker` must equal `0xDE100001` and file size must be ≥ `ByteCount + 64`. A wrong `BoardMarker` is non-retryable (disable flow until app restart, no retry loop against bad state). A truncated download is retryable (likely transient). Payload corruption beyond that is caught by the DE1 in verify phase.
- **BLE pacing on iOS stricter than elsewhere.** Mitigation: no code changes for iOS; rely on Qt's write queue. Manual verification covers iOS explicitly. If pacing is a problem, the 1 ms chunk-pump timer is configurable (injected through constructor) and can be raised for iOS in a follow-up.
- **User backgrounds the app mid-update on mobile.** Mitigation: full-screen "Do not switch apps" overlay during active phases. If the OS kills the app anyway, we fall through to the BLE-disconnect path (clean failure, clear retry offer).

## Migration

None. This is a new capability; no existing data, UI, or BLE flow is modified.
