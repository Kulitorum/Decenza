# Shared media validation — task 25.3

## Implementation

Native and Wasm use the same SettingsScreensaverTab Upload Media picker, progress,
Cancel and result controls. LocalFileTransfer uses ImportService’s completed,
single-use personalMediaFile lease; ScreensaverService owns host finalization.
The host prepares media off-thread, then the existing manager adopts it and
refreshes the personal category and opaque background catalog revision.
Uploading does not select a category or change a background.

PersonalMediaPreparation extracts the existing image/video transform and date
rules from ShotServer. Legacy uploads delegate to it; no HTTP domain adapter or
new media-library page was added. Images decode once. Existing formats and the
500 MiB per-file media allowance remain, with actual image/container validation
and Qt’s decoder allocation cap. Backup transfers retain their separate unlimited
application-quota policy.

Cancellation retires the requesting operation while its physical worker remains
admitted until completion drains. Client close, source/manager destruction,
catalog changes and duplicate names cannot adopt stale or overwrite existing
media. Preparation leases/stages survive only until worker/callback cleanup.
Native synchronous cancellation detaches pending requests before dispatch, so a
listener can delete/rebind its facade without losing newly admitted work.

## Verification — 2026-10-24

Combined Qt Creator build **1792763852572** passed native/Wasm packaging with
three known local linker warnings. Focused run **1792763853170** passed all four
affected lifecycle/shipping-handler suites after correcting two test-harness
errors; full run **1792763853171** passed **206/206**, with no skips or test
warnings. Earlier focused suites also covered import source, theme, BagLink and
QML inventory.

Fixtures cover complete/typed/single-use leases, size/count/file content bounds,
duplicate/no-overwrite behavior, cancel/worker admission, client/source/manager
lifetime, stale catalog refusal, detach/no replay, in-process reentrant deletion
and rebind, actual shared QML admission/cancel/correlated completion handlers,
and prepared media -> opaque catalog revision -> guarded background selection.

Actual native and Edge file pickers each imported a disposable PNG on package
**eb67f204**. Both clients displayed Personal (2), and both background pickers
rendered the two new thumbnails beside existing stock media. Local previews
rendered the selected image; Cancel preserved the existing background. No
category, screensaver or background preference was applied. Evidence:

- [Native import](evidence/media-import-native-eb67f204.png)
- [Edge import](evidence/media-import-edge-eb67f204.png)
- [Native catalog](evidence/media-background-native-eb67f204.png)
- [Native preview](evidence/media-preview-native-eb67f204.png)
- [Edge preview](evidence/media-preview-edge-eb67f204.png)

The final facade correction restarted through Qt Creator as original-checkout
PID 73417, matching package **3b2f64d8**, epoch
190c0875-2e56-4357-b92a-582c4e46c9b1. Root, /wasm/, /legacy/ and session returned
200; read-only MCP initialization/list/read/ping/close passed (67 tools, surface
1.20.0). Existing MP4 playback/range/missing-ticket evidence on af2ede12 remains
under task 25.3. No video import encoding, remote/mobile decoder or hardware
performance claim is made from these PNG checks.

## Outstanding

The maintainer authorized removal of the two disposable test PNGs. Before the
shared Clear Personal Media action, the host catalogue was checked to contain
exactly those two filenames. The action completed and the catalogue is empty;
the original Espresso category and screensaver preferences were restored.
Shared screensaver cycling/resize checks under 25.4 are in progress with the
maintainer's settings permission; real devices remain separate platform gates.
The short local wiki patch describes Upload Media and the retained /legacy/
entry; it has not been published.

## Task 25.4 — current Mac checks

Package 48c93612 rendered Flip Clock (3D on/off), growing Pipes, Strange
Attractors, populated flat Shot Map and its globe in native and Edge. Window
zoom/restore exercised resize; captured native views range from 1920×1264 to
5120×1954, and Edge from 3474×1688 to 5120×1956. Clock perspective, Pipes speed
0.5→0.6→0.5, camera period 60→61→60, flat/globe, dark/bright/satellite textures
and Day/Night all used the shared settings. Native keyboard dismissal worked.
Preferences were restored, including Videos & Images and Espresso category.

These checks found two visible defects: Disabled→Flip Clock on a retained page
stayed black, and Shot Map's profile list overlapped the Water info band. The
shared corrections and the Screensaver settings scroll correction passed the
bounded post-cleanup checks below. Earlier evidence is in
`evidence/screensaver-*-48c93612.png`; those images precede the corrections.

Read-only host observations during simultaneous native/Edge rendering were
9.5% CPU / 1,094,976 KiB RSS for Pipes and 63.4% / 1,564,880 KiB for Attractors;
Edge reported about 490 MB for its tab. These are instrumented Debug snapshots,
not frame-rate, leak or release-performance measurements. Task 25.4 remains
open for its remaining effect/media/input acceptance, including CRT. No additional
acceptance framework or benchmark suite was added.

### PR landing checks — 2026-10-24

On package **6d6f37f2**, changing Turn Screen Off → Flip Clock while both native
and Edge retained their screensaver revealed the clock without leaving the page.
Populated flat Shot Map placed Top Profiles below the Water readout in both
clients. At native 1920×1264, scrolling the right settings panel reached the
previously clipped Refresh Videos button; the compact 561×693 browser view showed
Upload Media and Refresh Videos. Original Videos & Images/Espresso preferences,
flat map, 75% dimming and overlay choices were restored; cached videos were kept.
See `evidence/screensaver-retained-clock-{native,edge}-6d6f37f2.png`,
`evidence/screensaver-map-spacing-native-6d6f37f2.png` and
`evidence/screensaver-settings-scroll-native-6d6f37f2.png`.

Final combined build **1792763852609** passed after aligning one conversation
error string with its existing translation key. Full run **1792763853183** passed
206/206; focused shipping-QML run **1792763853185** passed after that text change.
Qt Creator restarted original-checkout PID **66063**, matching package
**e178a1f4**, host epoch `1c7051db-7c5a-470c-a1c4-e2a23c716972`. The refreshed Wasm
client connected and showed the restored settings. Root, `/wasm/`, `/legacy/`
and `/api/session` returned 200; MCP initialize/list/read/ping/close passed on the
same listener (67 tools, surface 1.20.0), with the simulator connected and idle.
Browser evidence screenshots omit unrelated browser tabs and bookmarks.
