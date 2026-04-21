# DE1 Firmware Update — Operator Reference

Decenza ships a built-in DE1 firmware updater that mirrors what the original `de1app` Tcl/Tk app does, so you can drop the Decent tablet entirely and still keep your machine current.

## Where to find it

**Settings → Firmware tab.**

Two buttons:

- **Check now** — forces a check against Decent's GitHub for a newer firmware. Bypasses the once-per-week throttle.
- **Update now** — runs the three-phase flash. Disabled until a check has confirmed there's a new version available, and disabled mid-shot.

## What happens automatically

- 30 seconds after the app starts (and at most once per 168 hours), Decenza checks `https://raw.githubusercontent.com/decentespresso/de1app/main/de1plus/fw/bootfwupdate.dat` for a newer version. The check is a lightweight HTTP HEAD; the firmware body is only downloaded when you tap "Update now".
- The remote `Version` is compared against `MMR 0x800010` (the firmware build number the DE1 reports over BLE). Strictly greater = newer.
- Persistence: `firmware/lastCheckedAt` and `firmware/dismissedVersion` live in `QSettings`.

## What an update looks like

- ~45 seconds end to end on a healthy BLE link.
- Phase 1 — Erase: the DE1 wipes its flash. We see two `FWMapRequest` notifications (start, then complete) on characteristic `A009`. We then wait 10 s on Android / 1 s on other platforms before Phase 2 (mirroring `de1app`'s behaviour).
- Phase 2 — Upload: ~28,000 sequential 16-byte chunks streamed to characteristic `A006` with opcode `0x10`, paced at 1 ms.
- Phase 3 — Verify: one final `FWMapRequest`. The DE1 replies with a 3-byte `FirstError`. `{0xFF, 0xFF, 0xFD}` = success; anything else = the byte offset of the first corrupt block.
- The DE1 auto-reboots on success. Decenza's auto-reconnect logic re-establishes the BLE link.

## What you'll see if it fails

The Firmware tab shows a red strip with the error message and a **Retry** button. Retry restarts the full erase-upload-verify sequence from scratch — there's no partial-resume on the protocol side, but a clean retry is cheap (~45 s) and reliable.

Failure types and what they mean:

| Message | Likely cause | What to do |
|---|---|---|
| "Erase did not complete. Retry, or power-cycle the DE1." | Erase took >30 s with no `fwToErase=0` notification | Retry. If repeated, power-cycle the DE1 and reconnect. |
| "DE1 disconnected during firmware update" | BLE dropped mid-update | Bring the DE1 back into range, reconnect, tap Retry. The DE1's bootloader handles being half-flashed gracefully — next boot it'll re-accept a fresh upload. |
| "Verification failed at block A.B.C" | DE1 detected corruption at byte offset A·B·C during verify | Retry. If it repeats, this is worth a bug report — include the block offset. |
| "DE1 did not reconnect after verify" | Disconnected during verify, didn't come back within 15 s | Power-cycle the DE1 and reconnect; if the version reads as the new build, the update actually succeeded. |
| "The firmware file is not valid. Please report this." | Downloaded `.dat` failed BoardMarker check | **Non-retryable.** GitHub probably served a corrupted file — this should never happen. Report it. |
| "Finish current operation first" | Tried to update mid-shot/steam/flush/descale | End the current operation and tap Retry. |

## Where the firmware comes from

`https://raw.githubusercontent.com/decentespresso/de1app/main/de1plus/fw/bootfwupdate.dat` — Decent's own repository, no mirrors.

The downloaded file is cached at `QStandardPaths::AppDataLocation/firmware/bootfwupdate.dat` with a sidecar `.meta.json` storing the ETag, version, and download timestamp. A subsequent check returns `304 Not Modified` from GitHub when nothing has changed, and we don't re-download.

## What's validated client-side

Only `BoardMarker == 0xDE100001` (offset 4 of the 64-byte header) and the on-disk file size is at least `ByteCount + 64`. The header's `CheckSum` / `DCSum` / `HeaderChecksum` fields use algorithms that aren't currently documented anywhere we can verify; the DE1's own verify-phase response is the authoritative correctness check. (See the `TODO(firmware-crc)` marker if Decent confirms the algorithm.)

## What gets logged

Every state transition and every failure goes through `AsyncLogger` with the `[firmware]` tag and the `decenza.firmware` Qt logging category. Field bug reports can grep the debug log for `[firmware]` to recover the full timeline of an update attempt.

Failure log lines include the phase, chunk progress (`N/total`), retry-availability, and reason — useful when triaging "why didn't my update work?" reports.

## Simulator behaviour

When the DE1 simulator is active (`DE1Device::simulationMode() == true`), the firmware tab reports `updateAvailable = false` regardless of remote state, and `startUpdate` is a no-op. We don't pretend to flash a fake device.

## Cross-platform notes

- **All platforms** Decenza supports get the same flow: Windows, macOS, Linux, Android, iOS.
- **Android** uses a 10 s post-erase wait (vs. 1 s elsewhere) to give the Android BLE stack room to drain the queue between erase and the first chunk write — `de1app`'s historical workaround for an Android-specific race.
- **iOS** has the strictest BLE pacing of any platform; if you see chunk-pump stalls on iOS, the chunk-pump interval (`setChunkPumpIntervalMs`) is tunable via the `FirmwareUpdater` constructor injection.

## Testing without a real DE1

The firmware module ships with 51 unit tests across:

- `tst_firmwarepackets` — packet builder byte layouts (FWMapRequest, firmware chunk, parser)
- `tst_firmwareheader` — file header parser + on-disk validator
- `tst_firmwareassetcachehelpers` — sidecar JSON, Range header computation
- `tst_de1device_firmware` — DE1Device's writeFWMapRequest / writeFirmwareChunk / fwMapResponse signal
- `tst_firmwareupdater` — full state-machine flows (happy path, error paths, retry, dismiss, verify-disconnect retroactive success)

Build with `-DBUILD_TESTS=ON` and run individual binaries from `build/<config>/tests/Debug/`.

## Pointers into the code

| File | What lives there |
|---|---|
| `src/ble/protocol/firmwarepackets.h` | Byte-layout helpers (FWMapRequest, firmware chunk, notification parser) |
| `src/core/firmwareheader.h` | `.dat` header parser + `validateFile()` |
| `src/core/firmwareassetcache.{h,cpp}` | HTTP HEAD + Range download + sidecar persistence |
| `src/ble/de1device.{h,cpp}` | `writeFWMapRequest`, `writeFirmwareChunk`, `subscribeFirmwareNotifications`, `fwMapResponse` signal |
| `src/controllers/firmwareupdater.{h,cpp}` | The state machine and the QML-facing `Q_PROPERTY` surface |
| `qml/pages/settings/SettingsFirmwareTab.qml` | The UI |
| `openspec/changes/add-firmware-update/` | Original proposal, design notes, and OpenSpec scenarios |
| `docs/plans/2026-04-20-firmware-update-design.md` | Narrative design doc with the full sequence diagrams and error matrix |
