# DE1 Firmware Update — Operator Reference

Decenza ships a built-in DE1 firmware updater that mirrors what the original `de1app` Tcl/Tk app does, so you can drop the Decent tablet entirely and still keep your machine current. Validated end-to-end on real hardware (v1333 → v1352 on a Decent tablet + DE1PRO PCB 1.3).

## Where to find it

**Settings → Firmware tab.**

Two buttons:

- **Check now** — forces a check against Decent's update CDN for a newer firmware. Bypasses the once-per-week throttle.
- **Update now** — runs the three-phase flash. Disabled until a check has confirmed there's a new version available, and disabled mid-shot.

## What happens automatically

- 30 seconds after the app starts (and at most once per 168 hours), Decenza checks the active firmware channel on `fast.decentespresso.com` for a newer version. The default (stable) channel is `https://fast.decentespresso.com/download/sync/de1plus/fw/bootfwupdate.dat`; the nightly channel, opt-in via the toggle in Settings → Firmware, is `.../de1nightly/fw/bootfwupdate.dat`. The check is a lightweight HTTP HEAD; on ETag change, a `Range: bytes=0-63` GET fetches only the 64-byte header for version comparison. The full body is only downloaded when you tap "Update now".
- The remote `Version` is compared against `MMR 0x800010` (the firmware build number the DE1 reports over BLE). Strictly greater = newer.
- Persistence: `firmware/lastCheckedAt` (epoch seconds) lives in `QSettings`.

## What an update looks like

**Expect ~15–20 minutes on Android** — not 45 seconds. Android BLE write-with-response is serialised at ~30 ms per ACK; 28,992 chunks at that rate is ~16 minutes of upload alone. On desktop platforms BLE is faster (macOS/Linux can hit closer to 2–3 minutes), but the Android tablet is the common case.

- **Phase 1 — Erase**: `FWMapRequest(erase=1, map=1)` sent on characteristic `A009`. DE1 erases the inactive flash bank. Modern firmware sends a single erase-complete notification (`fwToErase=0`). We then wait a fixed 10 s (Android) / 1 s (elsewhere) per de1app's convention before starting the chunk pump.
- **Phase 2 — Upload**: 28,992 sequential 20-byte packets streamed to characteristic `A006`. Each packet is `[len=16][addr24 BE][16-byte payload]`. Length byte is 16 (`0x10`) — same format as MMR writes which use length 4. Address is 24-bit **big-endian**. ACK-driven progress so the bar tracks bytes on the wire, not bytes in the BLE queue.
- **Phase 3 — Verify**: `FWMapRequest(erase=0, map=1, FirstError=FF,FF,FF)` on `A009`. DE1 verifies the inactive bank and replies with a 3-byte `FirstError`. `{0xFF, 0xFF, 0xFD}` = success; anything else = the byte offset of the first corrupt block.
- On success, the DE1 remaps the inactive bank as active. It does **not** auto-reboot in practice — on every captured flash (upgrade and downgrade) the machine keeps running the old firmware until the user flips its power switch. Decenza prompts for a power-cycle immediately after verify (see `FirmwareUpdater::AwaitingReboot`) and its auto-reconnect logic re-establishes BLE once the DE1 comes back up on the new firmware.

### BLE subscription timing (matters)

The FWMapRequest (A009) CCCD subscription is enabled **right before verify** — *not* at the start of erase. This matches `de1app/de1_comms.tcl:962`. The heavy upload-write burst appears to invalidate the A009 subscription on Android, so a re-subscribe immediately before the verify request is required for the verify response to reach the app.

## Safety: dual-bank flash

The DE1's main MCU has two firmware slots. Our update only touches the inactive slot — the currently-running firmware is never modified. The bootloader atomically remaps active↔inactive only after verify succeeds.

Consequences:

- A failed update cannot brick the DE1. The active slot is still the old firmware, so a power-cycle boots back to v1333 (or whatever was running).
- You can iterate safely during debugging. Every attempt writes to the spare bank; the active bank is untouched until a successful verify.
- The `InBootLoader = 0x13` state in `DE1::State` is a first-class state Decent designed for exactly this recovery path.

## What you'll see if it fails

The Firmware tab shows a red strip with the error message and a **Retry** button. Retry restarts the full erase-upload-verify sequence from scratch — there's no partial-resume on the protocol side.

Failure types and what they mean:

| Message | Likely cause | What to do |
|---|---|---|
| "Erase did not complete. Retry, or power-cycle the DE1." | No erase-complete notification within 30 s | Retry. If repeated, power-cycle the DE1 and reconnect. |
| "DE1 disconnected during firmware update" | BLE dropped mid-update | Bring the DE1 back into range, reconnect, tap Retry. The bootloader handles half-flashed inactive banks gracefully — next boot still uses the active bank. |
| "Verification failed at block A.B.C" | DE1 detected corruption at byte offset A·B·C during verify | Retry. If it repeats, this is worth a bug report — include the block offset. |
| "DE1 did not reconnect after verify" | Disconnected during verify, didn't come back within the 180 s (3 min) ambiguous-verify grace window | Power-cycle the DE1 and reconnect; if the version reads as the new build, the update actually succeeded and we just missed the confirmation — Decenza will retroactively flip to "Update complete" once the new version reads back. |
| "No response from DE1 during verify" | The 60 s verify timeout fired without a notification | Most commonly a missing subscribe before verify (fixed) or a bootloader that validated and rebooted without emitting a response (the ambiguous-verify path will catch this via post-reconnect version check). Retry is usually correct. |
| "The firmware file is not valid. Please report this." | Downloaded `.dat` failed BoardMarker check | **Non-retryable.** The CDN probably served a corrupted file — this should never happen. Report it. |
| "Finish current operation first" | Tried to update mid-shot/steam/flush/descale | End the current operation and tap Retry. |

## Where the firmware comes from

Decent's own update CDN, the same host Tcl de1app uses. Two channels:

| Channel | URL | Who should pick it |
|---|---|---|
| Stable (default) | `https://fast.decentespresso.com/download/sync/de1plus/fw/bootfwupdate.dat` | Everyone, unless you have a reason to opt into pre-release firmware. |
| Nightly (opt-in) | `https://fast.decentespresso.com/download/sync/de1nightly/fw/bootfwupdate.dat` | Testers who want what Decent's de1app users on the nightly channel get. |

Decent's own `de1beta` channel is not wired — in practice it has not been updated reliably for a long time and tracks stable. Switching channels via the toggle wipes the local cache so the next check contacts the new endpoint fresh.

**Downgrades are allowed**, matching de1app. If the remote firmware on the selected channel is *older* than what's on the DE1 (e.g. you flip nightly → stable after having flashed a nightly build), the Firmware tab labels the action as "Downgrade now" and shows a yellow strip with both the installed and the available version. `FirmwareUpdater::isDowngrade` exposes this to QML. The three-phase flash procedure is direction-agnostic: it writes whatever's in the cached `.dat`. The equality-only pre-flight guard at the start of Phase 1 only short-circuits when installed == downloaded.

The downloaded file is cached at `QStandardPaths::AppDataLocation/firmware/bootfwupdate.dat` with a sidecar `.meta.json` storing `{etag, version, downloadedAtEpoch}`. A subsequent check returns `304 Not Modified` from the CDN when the ETag hasn't changed, and we don't re-download.

## What's validated client-side

Only `BoardMarker == 0xDE100001` (offset 4 of the 64-byte header) and the on-disk file size is at least `ByteCount + 64`. The header's `CheckSum` / `DCSum` / `HeaderChecksum` fields use algorithms that aren't currently documented anywhere we can verify; the DE1's own verify-phase response is the authoritative correctness check. Kal Freese's working Python updater skips these too. A `TODO(firmware-crc)` marker in `FirmwareAssetCache` documents where client-side checksum validation would plug in once Decent confirms the algorithms.

## What gets logged

Every state transition, upload-progress heartbeat (every 5 %), and failure goes through `qCDebug`/`qCWarning` with the `decenza.firmware` Qt logging category and the `[firmware]` prefix. Milestone lines carry a `[+MM:SS.ms]` elapsed prefix from the moment `startUpdate` was tapped, so the log trail tells you exactly how long each phase took.

Example field-report log for a successful update:

```
[+00:00.000] [firmware] check started, installed= 1333
[+00:00.295] [firmware] state: Checking for update -> Idle
[+00:00.005] [firmware] state: Idle -> Downloading firmware
[+00:00.008] [firmware] state: Ready to install -> Erasing flash
[+00:09.733] [firmware] state: Erasing flash -> Uploading firmware
[+00:58.827] [firmware] upload progress: 1449 / 28992 (5%)
[+02:00.xxx] [firmware] upload progress: 2898 / 28992 (10%)
...
[+15:45.xxx] [firmware] all 28992 chunks ACKed, settling 1500 ms before verify
[+15:47.xxx] [firmware] state: Uploading firmware -> Verifying
[+15:47.xxx] [firmware] A009 write FWMapRequest: 00 00 00 01 ff ff ff
[+15:49.xxx] [firmware] A009 notify: 00 00 00 01 ff ff fd
[+15:49.xxx] [firmware] state: Verifying -> Update complete
```

Failure log lines include the phase, chunk progress (`acked/queued/total`), retry-availability, and reason — useful when triaging "why didn't my update work?" reports.

## Simulator behaviour

When the DE1 simulator is active (`DE1Device::simulationMode() == true`), the firmware tab is fully usable for UI development — only the flash itself is blocked:

- `checkForUpdate` and `onCheckFinished` run normally against the live CDN, so the Available / Installed version surfaces populate.
- `MainController`'s `installedVersionProvider` returns `1` while in simulation mode, so both the stable and nightly channels register as "update available" (exercising the channel toggle + the downgrade path).
- `FirmwareUpdater::isSimulated` is exposed as a Q_PROPERTY; the QML gates the "Update now" button on `!fw.isSimulated` and shows a grey "Simulator connected — flashing is disabled" strip on the tab.
- `FirmwareUpdater::startUpdate` refuses unconditionally when `DE1Device::simulationMode()` is true, as a hard safety net against direct invocation from MCP/tests/remote-control paths that bypass the UI gate.

## Cross-platform notes

- **All platforms** Decenza supports get the same flow: Windows, macOS, Linux, Android, iOS.
- **Android** uses a 10 s post-erase wait (vs. 1 s elsewhere) to give the Android BLE stack room to drain the queue between erase and the first chunk write — `de1app`'s historical workaround for an Android-specific race.
- **Android BLE throughput** is the main user-visible bottleneck. Typical ~30 ms per write-with-response ACK means 28,992 chunks ≈ 15 minutes. Not a bug, just the OS's GATT per-connection-event budget.
- **iOS** has the strictest BLE pacing of any platform; if you see chunk-pump stalls on iOS, the chunk-pump interval (`setChunkPumpIntervalMs`) is tunable via the `FirmwareUpdater` constructor injection.
- **Computer sleep doesn't affect Android tablet uploads.** If you're debugging via `adb logcat` from a laptop and the laptop sleeps, the logcat connection drops but the tablet and the BLE session keep running. We confirmed this by accident during real-hardware testing — upload completed and DE1 rebooted to the new firmware while the PC was asleep.

## Testing without a real DE1

The firmware module ships with **63 unit tests** across:

- `tst_firmwarepackets` (13) — packet builder byte layouts (FWMapRequest, firmware chunk, parser)
- `tst_firmwareheader` (12) — `.dat` file header parser + on-disk validator
- `tst_firmwareassetcachehelpers` (12) — sidecar JSON round-trip, Range-header computation
- `tst_de1device_firmware` (13) — `DE1Device::writeFWMapRequest` / `writeFirmwareChunk` (bypasses MMR dedupe cache), `fwMapResponse` signal
- `tst_firmwareupdater` (13) — full state-machine flows: happy path, erase timeout, disconnect during upload, verify failure, precondition refused, race guard, dismiss, retry restart, verify-disconnect retroactive success + grace timeout

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

## What we learned validating on real hardware

Eight non-obvious bugs surfaced only after trying a real flash. Worth reading before making changes to this code:

1. **Byte 0 of a chunk packet is a length field (`16`/`0x10`), not a magic "firmware opcode"**. Same field as MMR writes (`0x04` for 4-byte values). Fooled the initial research agent because `16 == 0x10` coincidentally.
2. **Chunk address is big-endian.** Little-endian chunks land at byte-swapped addresses, get rejected by the bootloader, and some of those swapped addresses hit peripheral registers — we saw the DE1's pumps fire mid-upload as a result. Matches `make_U24P0` in `de1app/binary.tcl` and Kal Freese's `struct.pack(">BBH", …)`.
3. **Modern firmware sends one erase notification, not two.** `de1app`'s spec mentions a "while erasing" notification followed by "erase complete", but this firmware (v1333+) skips the first. Don't gate progression on the two-notification dance — de1app itself just waits a fixed 10 s/1 s.
4. **A009 notifications must be re-subscribed right before verify.** The heavy upload-write burst invalidates the CCCD state on Android. Match `de1app/de1_comms.tcl:962`'s ordering exactly.
5. **`writeComplete` subscription must be deferred past `BleTransport` attach.** At `MainController` construction time, `DE1Device::transport()` is still null. A constructor-time subscribe silently no-ops. Defer the `connect(transport, writeComplete, ...)` call to the first point we know the transport is alive (e.g. `beginUploadPhase`).
6. **Queue depth ≠ wire depth.** `BleTransport::queueCommand` enqueues immediately but BLE drains at ACK speed. Progress-from-queued-count jumps to 90 % in seconds and hangs; progress-from-ACK-count tracks the wire and is what the user should see.
7. **Verify timeouts need to be generous.** 10 s is too tight; 60 s works. Bootloader verification of the entire 453 KB image plus signature/checksum takes real time.
8. **QML context properties are case-sensitive.** The context name is `MainController` (capital M), not `mainController`. Getting this wrong resolves the object to undefined silently — the tab rendered with `fw = null` for ages before we caught it.
