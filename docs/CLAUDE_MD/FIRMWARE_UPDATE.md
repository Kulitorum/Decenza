# DE1 Firmware Update — Operator Reference

Decenza ships a built-in DE1 firmware updater that mirrors what the original `de1app` Tcl/Tk app does, so you can drop the Decent tablet entirely and still keep your machine current. Validated end-to-end on real hardware (v1333 → v1352 on a Decent tablet + DE1PRO PCB 1.3).

## Where to find it

**Settings → Firmware tab.**

Two buttons:

- **Check now** — forces a check against the bundled firmware catalog. Bypasses the once-per-week throttle.
- **Update now** — runs the three-phase flash. Disabled until a check has confirmed there's a new version available, and disabled mid-shot.

## What happens automatically

- 30 seconds after the app starts (and at most once per 168 hours), Decenza compares the selected bundled firmware entry against the DE1's installed build. Stable is bundled build 1352; Early access, opt-in via Settings → Firmware, is bundled build 1358. No network request is needed for the check or for the firmware bytes.
- The bundled manifest build is compared against `MMR 0x800010` (the firmware build number the DE1 reports over BLE). Strictly greater = newer.
- Persistence: `firmware/lastCheckedAt` (epoch seconds) lives in `QSettings`.

## What an update looks like

**Expect ~15–20 minutes on Android** — not 45 seconds. Android BLE write-with-response is serialised at ~30 ms per ACK; 28,992 chunks at that rate is ~16 minutes of upload alone. On desktop platforms BLE is faster (macOS/Linux can hit closer to 2–3 minutes), but the Android tablet is the common case.

- **Phase 0 — Sleep**: the DE1 is put to sleep before the erase command. An awake machine runs its heaters, PID and refill logic for the ~18 minutes a flash takes, on the same MCU that is erasing and programming flash. reaprime/decaid does this; de1app has the line and leaves it commented out. It has to happen *before* `setFirmwareFlashInProgress(true)` — `goToSleep()` carries its own guard check and is dropped once the flash is engaged.
- **Phase 1 — Erase**: `FWMapRequest(erase=1, map=1)` sent on characteristic `A009`. DE1 erases the inactive flash bank. Modern firmware sends a single erase-complete notification (`fwToErase=0`). The chunk pump starts as soon as that notification lands, with a 10 s fallback on every platform if it never does. Measured at +1.31 s on a DE1+/PCB 1.3, so the notification is the normal path and the timer is the exception. Matches reaprime/decaid; de1app gates on nothing and always waits the fixed 10 s.
- **Phase 2 — Upload**: 28,992 sequential 20-byte packets streamed to characteristic `A006`. Each packet is `[len=16][addr24 BE][16-byte payload]`. Length byte is 16 (`0x10`) — same format as MMR writes which use length 4. Address is 24-bit **big-endian**. ACK-driven progress so the bar tracks bytes on the wire, not bytes in the BLE queue.
- **Phase 3 — Verify**: `FWMapRequest(erase=0, map=1, FirstError=FF,FF,FF)` on `A009`. DE1 verifies the inactive bank and replies with a 3-byte `FirstError`. `{0xFF, 0xFF, 0xFD}` = success; anything else = the byte offset of the first corrupt block.

  **A notification is a verdict only when `WindowIncrement == 0`, `fwToErase == 0`, `fwToMap == 1` *and* `FirstError != FF FF FF`.** Anything else is the bootloader talking mid-scan, and its `FirstError` is a cursor rather than a corrupt-block address — reading it as one turns normal progress into a permanent-looking failure that survives Retry. We keep waiting; the 60 s verify timeout is the backstop. This is reaprime/decaid's `_isTerminalVerificationResponse` (`unified_de1.firmware.dart:142-147`).

  **The `FirstError != FF FF FF` condition is the one that is easy to drop.** `FF FF FF` is the bootloader's "no error found" value *and* what we write into the verify request, which the DE1 echoes — so an in-progress notification carries it with `WindowIncrement == 0` and `fwToMap == 1`, passing any filter built on the other three conditions, comparing unequal to `FF FF FD`, and reporting `Verification failed at block 255.255.255` on a flash that was still verifying. Success is `FF FF FD`; a real failure carries a real address; `FF FF FF` is neither.
- On success, the DE1 remaps the inactive bank as active. It does **not** auto-reboot in practice — on every captured flash (upgrade and downgrade) the machine keeps running the old firmware until the user flips its power switch. Decenza prompts for a power-cycle immediately after verify (see `FirmwareUpdater::AwaitingReboot`) and its auto-reconnect logic re-establishes BLE once the DE1 comes back up on the new firmware.

### BLE subscription timing (matters)

The FWMapRequest (A009) CCCD subscription is enabled **right before verify** — *not* at the start of erase. This matches `de1app/de1_comms.tcl:991`. The heavy upload-write burst appears to invalidate the A009 subscription on Android, so a re-subscribe immediately before the verify request is required for the verify response to reach the app.

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
| "The firmware file is not valid. Please report this." | Bundled `.bin` failed manifest/header/digest validation | **Non-retryable.** The application bundle probably contains mismatched firmware assets — this should never happen. Report it. |
| "Finish current operation first" | Tried to update mid-shot/steam/flush/descale | End the current operation and tap Retry. |

## Where the firmware comes from

Decenza ships the firmware images in the app bundle, using Decaid's firmware manifest as the source of truth. Two channels:

| Channel | Bundled build | Who should pick it |
|---|---|---|
| Stable (default) | 1352 (`de1-1352.bin`) | Everyone, unless you have a reason to opt into pre-release firmware. |
| Early access (opt-in) | 1358 (`de1-1358.bin`) | Testers who want to try the newer bundled firmware before it becomes Decenza's default. |

The Firmware tab shows release notes from the selected Decaid manifest entry, tied to the displayed channel and version. Early access is persisted as `firmware/EA`; the one-time upgrade removes the obsolete `firmware/nightlyChannel` key and resets every install to Stable before a user opts into Early access again.

**There is no version gate at all**, matching de1app — every version check in its `start_firmware_update` (`de1_comms.tcl:884-895`) is commented out, so its button flashes whatever is in `bootfwupdate.dat` regardless of what the machine reports. Decenza does the same and labels the action instead of blocking it:

| Remote vs installed | Button | Strip |
|---|---|---|
| Newer | "Update now" | — |
| Older | "Downgrade now" | yellow: rolls the DE1 back |
| Same | "Reflash" | yellow: already on this version |

`FirmwareUpdater::isDowngrade` and `::isReflash` expose this to QML; the button's only requirement is `availableVersion > 0` (a check has succeeded). The three-phase flash is direction-agnostic — it writes whatever's in the cached `.dat`.

Re-flashing the same build used to short-circuit to `Succeeded` without writing anything. That made the one case that most needs a flash unreachable: a bank that verified but did not take, leaving the DE1 running the old image while reporting the new build. It is also safe for the same reason a failed update is — the write lands in the inactive bank.

There is no production download cache or sidecar metadata any more; the selected resource is validated from the application bundle before flashing. Unit tests can still point `FirmwareAssetCache::setCacheRoot()` at a synthetic `bootfwupdate.dat` to exercise the flash state machine without bundling a real firmware image into every test fixture.

## What's validated client-side

`validateFile()` checks `BoardMarker == 0xDE100001` (offset 4), a file-size floor of `ByteCount + 64`, and a set of structural invariants adopted from reaprime/decaid's `FirmwareValidator`: `ByteCount` non-zero, `0 < CpuBytes <= ByteCount`, the reserved word at offset 20 zero, `CheckSum`/`DCSum`/`HeaderChecksum` all non-zero, and the IV not all-zero. Failing any of the latter gives `Validation::MalformedHeader`.

Bundled images then get a second validation pass against Decaid's manifest: exact file byte length, SHA-256 digest, header `Version`, `BoardMarker`, `ByteCount`, and `CpuBytes` must all match the selected manifest entry. A mismatch is a packaging error and is non-retryable from the UI.

**Why the structural checks are not busywork:** BoardMarker is identical in every DE1 image ever published and sits in the first 16 bytes, so BoardMarker-plus-a-size-floor accepts a file with a valid-looking header but wrong body. The manifest digest and exact-size check are what prove the bundled bytes are the pinned Decaid artifact.

The `CheckSum` / `DCSum` / `HeaderChecksum` **algorithms** remain undocumented, so we can only assert they are populated, not recompute them; the DE1's own verify-phase response is still the authoritative correctness check. Kal Freese's working Python updater skips them too. A `TODO(firmware-crc)` marker in `FirmwareAssetCache` documents where real checksum validation would plug in once Decent confirms the algorithms.

Note that synthetic test blobs must populate these fields — `makeFirmwareBlob()` / `makeValidHeader()` in the tests do, and a header built with bare zeros is *supposed* to be rejected.

## What gets logged

Every state transition, upload-progress heartbeat (every 5 %), and failure goes through `qCDebug`/`qCWarning` with the `decenza.firmware` Qt logging category and the `[firmware]` prefix.

Three lines exist specifically because the failure they diagnose is invisible without them:

- `[firmware] payload: <n> bytes sha256=… version=… byteCount=… cpuBytes=…` — logged in `loadCachedPayload()`, immediately before the first chunk. Compare against the selected Decaid manifest entry.
- `A009 parsed: windowIncrement=… erase=… map=… firstError=…` (`[Firmware]` tag, `de1device.cpp`) — `WindowIncrement` is decoded but not carried on the `fwMapResponse` signal. It is what separates a terminal verify verdict from an in-progress notification: reaprime/decaid accepts a verify response only when `WindowIncrement == 0`, while we accept the first notification of any shape. A non-zero value on the notification that produced "Verification failed at block A.B.C" means that address is a cursor, not a corrupt block. Milestone lines carry a `[+MM:SS.ms]` elapsed prefix from the moment `startUpdate` was tapped, so the log trail tells you exactly how long each phase took.

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

- `checkForUpdate` and `onCheckFinished` run normally against the bundled manifest, so the Available / Installed version and release-note surfaces populate.
- `MainController`'s `installedVersionProvider` returns `1` while in simulation mode, so both the Stable and Early access channels register as "update available" (exercising the channel toggle + the downgrade path).
- `FirmwareUpdater::isSimulated` is exposed as a Q_PROPERTY; the QML gates the "Update now" button on `!fw.isSimulated` and shows a grey "Simulator connected — flashing is disabled" strip on the tab.
- `FirmwareUpdater::startUpdate` refuses unconditionally when `DE1Device::simulationMode()` is true, as a hard safety net against direct invocation from MCP/tests/remote-control paths that bypass the UI gate.

## Cross-platform notes

- **All platforms** Decenza supports get the same flow: Windows, macOS, Linux, Android, iOS.
- **The post-erase wait is 10 s on every platform.** It used to be 10 s on Android and 1 s elsewhere, and this file used to justify that as "`de1app`'s historical workaround for an Android-specific race" — a rationale nothing in de1app supports. de1app branches on `$::has_bluetooth`, not on platform (`de1_comms.tcl:940-950`); its 1 s arm is the no-BLE dry run, sitting a few lines below the block that fakes the connection handles because no machine is attached. Every real flash it performs waits 10 s. We read `has_bluetooth` as "is Android" and gave iOS, macOS, Windows and Linux the no-machine timing. A captured DE1+/PCB 1.3 flash reported erase-complete at **1.31 s**, so the 1 s arm was also simply too short — the chunk pump would have started mid-erase.
- **Android BLE throughput** is the main user-visible bottleneck. Typical ~30 ms per write-with-response ACK means 28,992 chunks ≈ 15 minutes. Not a bug, just the OS's GATT per-connection-event budget.
- **iOS** has the strictest BLE pacing of any platform; if you see chunk-pump stalls on iOS, the chunk-pump interval (`setChunkPumpIntervalMs`) is tunable via the `FirmwareUpdater` constructor injection.
- **Computer sleep doesn't affect Android tablet uploads.** If you're debugging via `adb logcat` from a laptop and the laptop sleeps, the logcat connection drops but the tablet and the BLE session keep running. We confirmed this by accident during real-hardware testing — upload completed and DE1 rebooted to the new firmware while the PC was asleep.

## Testing without a real DE1

Five suites cover the firmware module. **No counts here on purpose** — the numbers this section used to carry were hand-maintained, drifted every time a test was added, and were twice internally inconsistent (a stated total that did not match the sum of its own per-suite figures). A data-driven test also contributes one slot and N reported cases, so there is no single number that is right for both readings. `ctest -R firmware` is the source of truth.

- `tst_firmwarepackets` — packet builder byte layouts (FWMapRequest, firmware chunk, parser)
- `tst_firmwareheader` — `.dat` header parser and on-disk validator: BoardMarker, the size floor and ceiling, and the malformed-header shapes (`_data()` table)
- `tst_firmwareassetcachehelpers` — Decaid manifest parsing, bundled resource validation, channel classification, release-note metadata
- `tst_de1device_firmware` — `DE1Device::writeFWMapRequest` / `writeFirmwareChunk` (bypasses MMR dedupe cache), `fwMapResponse` signal including `WindowIncrement`
- `tst_firmwareupdater` — full state-machine flows: happy path; sleep precedes the erase request; the erase-complete notification starts the upload ahead of the fallback timer; "still erasing" does not, and neither does one arriving before the erase request's own write ACK; a non-terminal verify notification is ignored rather than reported as failure; erase timeout; disconnect during upload; verify failure; precondition refused; same-version re-flash still flashes; dismiss; retry restart; verify-disconnect retroactive success and grace timeout

Build with `-DBUILD_TESTS=ON` and run individual binaries from `build/<config>/tests/Debug/`.

## Pointers into the code

| File | What lives there |
|---|---|
| `src/ble/protocol/firmwarepackets.h` | Byte-layout helpers (FWMapRequest, firmware chunk, notification parser) |
| `src/core/firmwareheader.h` | `.dat` header parser + `validateFile()` |
| `src/core/firmwareassetcache.{h,cpp}` | Decaid manifest parsing, bundled resource selection, manifest/header/digest validation |
| `src/ble/de1device.{h,cpp}` | `writeFWMapRequest`, `writeFirmwareChunk`, `subscribeFirmwareNotifications`, `fwMapResponse` signal |
| `src/controllers/firmwareupdater.{h,cpp}` | The state machine and the QML-facing `Q_PROPERTY` surface |
| `qml/pages/settings/SettingsFirmwareTab.qml` | The UI |
| `openspec/changes/add-firmware-update/` | Original proposal, design notes, and OpenSpec scenarios |
| `docs/plans/2026-04-20-firmware-update-design.md` | Narrative design doc with the full sequence diagrams and error matrix |

## What we learned validating on real hardware

Eight non-obvious bugs surfaced only after trying a real flash. Worth reading before making changes to this code:

1. **Byte 0 of a chunk packet is a length field (`16`/`0x10`), not a magic "firmware opcode"**. Same field as MMR writes (`0x04` for 4-byte values). Fooled the initial research agent because `16 == 0x10` coincidentally.
2. **Chunk address is big-endian.** Little-endian chunks land at byte-swapped addresses, get rejected by the bootloader, and some of those swapped addresses hit peripheral registers — we saw the DE1's pumps fire mid-upload as a result. Matches `make_U24P0` in `de1app/binary.tcl` and Kal Freese's `struct.pack(">BBH", …)`.
3. **Modern firmware sends one erase notification, not two.** `de1app`'s spec mentions a "while erasing" notification followed by "erase complete", but this firmware (v1333+) skips the first. Don't gate progression on the two-notification *dance* — but do gate it on the single completion notification, with the fixed wait as fallback. `fwToErase=1` means "still erasing" and must not be mistaken for completion.

   **Don't test `FirstError` on the erase-complete notification either — the DE1 echoes back whatever you sent.** We send `0,0,0` on the erase request and the completion notification carries `0,0,0`; decaid sends `0xFF,0xFF,0xFF` and asserts `0xFF,0xFF,0xFF` comes back. Both are reading their own outbound bytes. Only `fwToErase`/`fwToMap` carry machine state here. (Verify is different: we send `FF FF FF` and success comes back as `FF FF FD`, so that one is a real response.)
4. **A009 notifications must be re-subscribed right before verify.** The heavy upload-write burst invalidates the CCCD state on Android. Match `de1app/de1_comms.tcl:991`'s ordering exactly.
5. **`writeComplete` subscription must be deferred past `BleTransport` attach.** At `MainController` construction time, `DE1Device::transport()` is still null. A constructor-time subscribe silently no-ops. Defer the `connect(transport, writeComplete, ...)` call to the first point we know the transport is alive (e.g. `beginUploadPhase`).
6. **Queue depth ≠ wire depth.** `BleTransport::queueCommand` enqueues immediately but BLE drains at ACK speed. Progress-from-queued-count jumps to 90 % in seconds and hangs; progress-from-ACK-count tracks the wire and is what the user should see.
7. **Verify timeouts need to be generous.** 10 s is too tight; 60 s works. Bootloader verification of the entire 453 KB image plus signature/checksum takes real time.
8. **The QML name is case-sensitive: `MainController`, capital M, not `mainController`.** Getting it wrong resolves the object to undefined silently — the tab rendered with `fw = null` for ages before we caught it. *(`MainController` is no longer a context property; it is a compile-time QML singleton, so a file using it needs `import Decenza`. The lesson survives the mechanism change, and qmllint now catches the misspelling instead of the user finding it.)*
