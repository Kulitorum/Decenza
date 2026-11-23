# Design

## Context

See `proposal.md` for motivation. The facts that shape the approach, all read from
`~/Qt/6.12.1/Src` against `~/Qt/6.12.0/Src` on 2026-11-23:

- `android/qt-overrides/manifest.json` records stock 6.12.0 checksums, and `release.yml` runs
  `verify_artifact.py` against the installed SDK before copying the patched library over the
  stock one. A version bump alone fails that step.
- The allocator fix is in stock 6.12.1: `qtdeclarative/src/qml/memory/qv4mm.cpp:265-316` keeps
  the bits of slots allocated during sweep (`objectBitmap[i] &= ~objectsToFree`) where 6.12.0
  assigned the words wholesale. This is the change `qt-6.12.0-sweep-allocation-fix.patch`
  backports.
- The deadlock-protector abort is gone: `qtbase/src/plugins/platforms/android/
  qandroidplatformopenglwindow.cpp` no longer includes or acquires `AndroidDeadlockProtector`,
  and the string "Failed to acquire deadlock protector" appears nowhere in the Android platform
  plugin.
- qmllint keeps buffered findings: `qtdeclarative/src/qmlcompiler/qqmljslogger.cpp:419`
  truncates to a mark set at `:387` instead of discarding the buffer.
- Platform floors are unchanged: the `ANDROID`, `IOS`, `MACOS` and `CMAKE_VERSION` lines of
  `qtbase/.cmake.conf` are identical between the two versions.
- Stock Qt's cross-validation only prints: on a difference it emits
  `qDebug() << "Cross Validation Error on chunk" …` (`qv4mm.cpp:807-812`). The
  `[Memory][QmlGc] DEFERRED_ABORT` message the app's abort path waits for is produced only by
  the patched library, so without the override that path cannot run.
- Qt MQTT's `v6.12.1` tag exists, so `cmake/qtmqtt.cmake` follows the bump with no edit.

## Goals / Non-Goals

**Goals:**

- One Qt patch version, 6.12.1, on every platform, with stock binaries on all of them.
- The GC diagnostics remain recoverable by someone who has only the repository.
- Every removed workaround cites the 6.12.1 source that makes it unnecessary.

**Non-Goals:**

- Porting the GC patches or the instrumentation test to 6.12.1.
- Establishing the cause of #2179, or showing that 6.12.1 closes the ownership-change gap the
  probes isolated.
- Adopting anything new from 6.12.1. This is a version move and a removal.

## Decisions

**1. The bump and the removal are one change.** On 6.12.0 the override is the only source of the
allocator fix; removing it first would ship the bug, and bumping first fails CI at the manifest
check. Splitting them has no safe intermediate state.

**2. Remove the binary; do not port it.** The alternative was rebuilding patches 1 and 2 on
6.12.1 so a recurrence would still produce weak-history evidence. Rejected on the maintainer's
call: no #2179 report since the allocator fix shipped, and an instrumented library costs an ABI
lock and a rebuild on every Qt bump. The price is stated in the retirement record: if #2179
recurs, the first report carries no weak-history lines, and the override has to be rebuilt
before the next one does.

**3. The patches and their test stay under their 6.12.0 names; `ownership-repro/` moves.**
`tools/qt-gc-diagnostics/` keeps the three patches and `tests/`, with the
`find_package(Qt6 6.12.0 EXACT …)` pin in `tests/`: that test needs a QtQml built from the
patches, so re-pinning it would claim a port that has not been done. `ownership-repro/` builds
against stock Qt, so it is re-pinned to 6.12.1 and stays runnable. It was not run for this
change (maintainer's call, 2026-11-23): it is a separate CMake project outside the app's suite,
two of its probes fail by design, and its result would gate nothing, since the removal rests on
field silence. The record says it was not run on 6.12.1. `docs/qt-patches/README.md` gets a pointer, since that is where a reader looks
for Qt patches kept as source. Moving the patches there was rejected: it would separate them
from the tests that exercise them.

**4. The binary is recovered from git history, not kept in the tree.** The retirement record
names the last commit that contains it (`4229107cf`, #2181), its SHA-256 and build ID from the
manifest, and notes that the override README, manifest and `verify_artifact.py` are at the same
commit. It also names the commit this change is based on as the place to recover the app-side
code from decision 10, which changed after #2181. Keeping a 10 MB library that nothing packages was the alternative; history already
holds it.

**5. `verify_artifact.py` is deleted with the manifest it validates.** The `build-config`
"decision is revisited" scenario requires the version-lock guard to be restored before any
future artifact is packaged; the record says to restore this script from the same commit.

**6. The CMake minimum moves to 6.12.1.** `find_package(Qt6 6.12.0 REQUIRED …)` would let a
6.12.0 kit build an Android package with neither the override nor the upstream fix. The README
badge and the Raspberry Pi guide move with it.

**7. Wasm pins move; the GC tool pins do not.** `tools/qtmodules-wasm/`, the packaging scripts,
`scripts/build_webclient.py` and `cmake/webclient_bundle.cmake` are part of the shipped web
client build and must match the native version. `tools/webclient/media-repro/` moves too, so
the filed reproduction can be re-run on 6.12.1; its README and the `upstream-*.md` files keep
their "reproduced on 6.12.0" statements, which are dated observations.

**8. Obsolete workarounds are retired with evidence; outstanding ones are named.**
- `docs/qt-patches/qtbase-android-deadlock-protector-no-abort.patch` is deleted and its README
  entry reduced to a short record citing the 6.12.1 source above. The README itself said the
  upstream rewrite would make the file dead.
- `QML_GOTCHAS.md` item 5 and the matching comment in `scripts/qmllint_report.py` are rewritten
  to past tense with the `qqmljslogger.cpp` citation. The gate keeps reading `--json`; that
  needs no workaround to justify it.
- `OperationCompletionOverlay.qml` keeps its `_selectedPitcher` workaround. QTBUG-152061 is
  fixed for 6.12.2, not 6.12.1.
- The Wasm bugs Decenza filed (QTBUG-151710, 151711, 151799, 151767, 151769, 151807) are absent
  from the 6.12.1 release note; their workarounds and notes stay.

**9. Line citations into Qt source are re-checked, not bulk-rewritten.** `CLAUDE.md` and
`docs/CLAUDE_MD/` cite file and line in `~/Qt/6.12.0/Src`. The path moves to 6.12.1, and each
citation in those two places is opened in the new tree and corrected if it moved. Comments that
say "in 6.12.0" explicitly are dated and left alone.

**10. The app-side GC diagnostics are removed whole, not left dormant.** Three pieces:
- *Cross-validation* (`main.cpp:562-566`, `enableGcCrossValidation`, the batch rewriting in
  `btlogfilter`, `setGcPageStack` and its caller in `crashreporter.cpp`). Its own comment prices
  it at a full mark per GC cycle on the GUI thread and says to remove it with the fix.
- *Deferred abort* (`GcDeferredAbort::Coordinator` and its wiring in `main.cpp`,
  `gcdeferredabort.h`, the `DEFERRED_ABORT` branch and Android stack capture in `btlogfilter`,
  `setGcMismatchHandler`, `DE1Device::setGcAbortPending` and the block in `requestState`).
- *Crash-report special case* (the 900-character `MARK_PATH` clip in `crashhandler.cpp`).

Keeping the abort path "for the override's return" was rejected: it is unreachable code in the
start-operation path of the machine, and it would rot against a library nobody is building.
`btlogfilter`'s unrelated filters (the `CAP_NET_ADMIN` and "window doesn't exist" drops) stay.

**11. `DE1Device` members go only if the abort path was their last reader.**
`hasCurrentStateInfo()`, `requestCurrentStateInfo()`, `m_stateInfoReceived` and the
`stateInfoReceived` signal were read only by the coordinator and its tests when this was
written. Each is checked for other readers at removal time and deleted, with its tests, if it
has none. The fault-address mapping line in `crashhandler.cpp` that mentions #2076 is a general
crash diagnostic and stays.

**12. Tests are removed with the code, not rewritten.** The GC slots in
`tst_webdebuglogger.cpp` and `gcAbortPendingBlocksNewOperationsButAllowsStop` in
`tst_de1device_headless.cpp` assert behaviour that no longer exists. No replacement test is
owed: nothing new is added.

**13. The Qt version is read from one root file, `qt-version.txt`.** It holds the bare version
(`6.12.1`). A plain file because three kinds of reader need it: CMake, Python and workflow
shell. It sits at the root rather than beside `.github/qt-modules.txt` because CMake and local
scripts read it too, not only CI.

- *CMake*: `cmake/DecenzaQtVersion.cmake` reads the file into `DECENZA_QT_VERSION` and fails if
  it is missing or malformed. The root project, `src/webclient`, `tools/qtmodules-wasm`,
  `tools/webclient/media-repro` and `tools/qt-gc-diagnostics/ownership-repro` include it by
  relative path. Each `find_package` keeps its current strictness: the root stays a minimum,
  the Wasm and tool projects stay `EXACT`. `tools/qt-gc-diagnostics/tests` keeps its literal
  `6.12.0`: it is tied to the unported patches, not to the current version.
- *Python*: one helper, `scripts/qt_version.py`, used by `build_webclient.py`,
  `qmllint_report.py` (to build its default `qmllint` paths) and the two
  `tools/qtmodules-wasm` scripts. No script parses the file itself.
- *Workflows*: the `plan` job, which every build job already `needs`, reads the file into an
  output. Every `install-qt-action` `version:`, the `aqt install-src` line, the Wasm artifact
  names and recipe keys, and the four cache keys that name a version use that output. The
  workflow-level `QT_VERSION: '6.12.0'` goes. `nightly-sanitizers.yml` has no `plan` job, so it
  reads the file in a step. `install-qt-action`'s own caches already key on the version, so
  their `cache-key-prefix` values do not change.
- *Check*: `scripts/check_qt_version_pins.py`, run by `text-invariants.yml`, fails on the
  current version (read from the file) appearing as a literal in workflows, CMake files, and
  build scripts outside comments. Because it matches the *current* version only, dated
  references to older releases and the GC patch pins pass without an allowlist. Its `paths:`
  filter gains `qt-version.txt`, `**/CMakeLists.txt`, `cmake/**`, `scripts/**` and `tools/**`.

Not centralized, by nature: prose docs (`CLAUDE.md` and the guides point at the file and write
install paths as `~/Qt/<version>/…`), the local Qt Creator kit, and comments that cite a line in
a specific release. The `iOS` comment about the `ios_device` package layout describes 6.12.x
packaging, not a pin, and stays prose.

Alternatives rejected: a `cmake/*.cmake` file as the source, which the workflows would have to
parse; and the workflow-level `env:` block, which cannot read a file and covers only CI.

**14. Both Apple jobs move to the `xcode-27` image and Xcode 27.2.** Checked 2026-11-23:

- Apple's [developer news](https://developer.apple.com/news/?id=k1mtkt1k) (Sept 9, 2026):
  "Starting April 2027", iOS and iPadOS uploads need the "iOS 27 & iPadOS 27 SDK or later".
  macOS is not on that list.
- GitHub's `macos-26` image (20261117) has Xcode 26.0.1 to 26.6 and no 27. Xcode 27 is a
  separate image, label `xcode-27`, running macOS 27, arm64 only, GA 2026-10-15. Its readme
  (20261104) lists Xcode 27.0, 27.1 (default) and 27.2 (build 27B52) at
  `/Applications/Xcode_<version>.app`, so `setup-xcode`'s path pattern still fits.
- Qt 6.12.1's `qtbase/.cmake.conf` sets the maximum supported iOS and macOS SDK to 27.
- The local Xcode 27.2 SDKs allow deployment to iOS 15.0 and macOS 12.0, and the macOS SDK
  still lists `x86_64`, so the iOS 18.0 and macOS 14.4 floors and the universal build stand.
- The Apple jobs use no system OpenSSL, so the image's announced OpenSSL 3-to-4 default change
  does not reach them.

27.2 rather than the image default 27.1: it is the build Jeff develops with (27B52 on both), so
a local build and CI share a toolchain, which is the same reason 26.4.1 was chosen before. The
macOS job moves too, at the maintainer's request, so the two Apple jobs keep one toolchain;
staying on `macos-26` was the alternative and needs no Rosetta question answered.

The version goes in one workflow-level `XCODE_VERSION`, used by both `setup-xcode` inputs and
the iOS ccache key. `setup-xcode`'s libc++ pin comment is rewritten: its observation was about
Xcode 26.2's libc++ against Qt 6.11.1 binaries and no longer explains the choice. The shims stay:
whether the `xcode-27` image pre-exports deployment-target variables is not documented, and the
shims are harmless when it does not.

`actionlint` here does not know the `xcode-27` label, so a `.github/actionlint.yaml` declares it.

**15. Workarounds 6.12.1 makes unneeded are removed; the rest are kept with correct citations.**
An inventory of the code's Qt workarounds (152 sites) was checked against the 6.12.1 release
note, the Qt Jira status of every cited bug, and the 6.12.1 source:

- *Removed:* `ValueInput`'s two `onValueChanged` accessibility notifications. 6.12.1's
  `setRole()` connects `value` itself (`qquickaccessibleattached.cpp:479`), so each change sent
  two ValueChanged events; `tst_sharedformcontrols` counted 2 with the old code. The
  `Component.onCompleted` calls stay: Qt sends no event for the starting value and the Wasm
  slider sets its DOM value only on ValueChanged (`qwasmaccessibility.cpp:1126-1131`).
- *Removed:* `BtLogFilter`'s drop of "window doesn't exist." (QTBUG-146779, fixed in 6.12.0 RC).
  In 6.12.x the line is a `qCDebug` on `qt.graphs2d.general`, off by default, and the app sets no
  logging rules. The filter is now Linux-only, its one remaining rule.
- *Kept, citation corrected:* the Apple Bluetooth-state adapter cited QTBUG-50838, which is a
  Qt 5.8 EGL build issue. iOS has no `QBluetoothLocalDevice`, so the adapter stays; 6.12's macOS
  `hostMode()` does read the power state (`qbluetoothlocaldevice_macos.mm:447`).
- *Kept, needs a device:* blocking clipboard signals on iOS. 6.12.1's `canPaste` asks only for
  pasteboard types (`qiosclipboard.mm:94`), not text, so it may no longer raise iOS's paste
  prompt; only an iPhone shows whether `pasteboardTypes` prompts.
- *Kept, still broken upstream:* the `QFuture` continuation (`qfuture_impl.h` is identical in
  6.12.1), the Popup screen-reader path (QTBUG-140200 is still Reported), QTBUG-152061
  (fixed for 6.12.2), the Wasm text-field notification (QTBUG-151711, fixed for 6.12.2).
- *Wasm, checked against the 6.12.1 plugin and Multimedia source:* removed the web shell's
  one-shot reload on "Cannot use deleted val. handle" (QTBUG-151843, still Open): the report
  blames an animation-frame callback left scheduled with a raw output pointer, and 6.12.1 now
  stores that request and cancels it in the destructor, and guards the frame copy's callback
  (`qwasmvideooutput.cpp`). Confirmed at runtime in task 6b.6. Of the bugs Decenza filed,
  only QTBUG-151767 (video elements left behind) shipped in 6.12.1, though the note omits it;
  QTBUG-151799 (checkbox `checked`) is fixed in the source while Jira still says Reported, and
  no workaround of ours depended on it. QTBUG-151711 and QTBUG-151770 are fixed for 6.12.2,
  QTBUG-151769 for 6.13; QTBUG-151710 and QTBUG-151807 are open. Kept because 6.12.1 still has
  the defect: `playsinline`, the `srcObject` shim, pause-before-stop, the `emptied` filter, mute
  ordering, wheel line scaling, scroll phases (QTBUG-141540), async Blob reads, `inert`-only
  dialogs. Removed after a browser run: the screensaver's single reused video output and its
  pause-before-stop; the web client now recreates the player per video like native (task
  6b.6). Kept, needs an iPhone: the iOS container re-measure.
- *Kept, Android behaviour:* the screensaver's stop-on-suspend (QTBUG-118231, fixed in 6.9) and
  keep-screen-on in disabled mode (QTBUG-45019, Qt 5.5). Removing them needs a tablet; they also
  stand on their own as behaviour.

## Risks / Trade-offs

- **The `xcode-27` image has no Rosetta** → the macOS job's `arch -x86_64` smoke run of
  `decenza-verify-webclient` fails. Neither runner readme mentions Rosetta, including
  `macos-26`, where the step passes today, so only a run answers it. Rosetta still works on
  macOS 27.3 locally. If the run fails there, the choice (install Rosetta in that step, or keep
  the `lipo` check and drop the x86_64 execution) goes back to the maintainer; it is not decided
  here.
- **Qt's prebuilt iOS binaries do not link with Xcode 27.2's libc++** → the iOS CI build fails at
  link, as Xcode 26.2 once did against Qt 6.11.1. The all-platform dispatch in group 9 is the
  proof; the local macOS build already links with Xcode 27.2.

- **A job reads the version before checkout, or not at all** → `plan` does not check out the
  repository today; it gains a sparse checkout of `qt-version.txt` only. A job using an empty
  output would ask `install-qt-action` for version ``,
  so `plan` fails if the file yields nothing that matches `^\d+\.\d+\.\d+$`.
- **A cross-validation difference would no longer be logged** → Accepted. It was a diagnostic
  for #2076/#2179, it was never a fix, and stock 6.12.1 carries the allocator fix it was hunting.
- **Removing the operation block changes `DE1Device::requestState`** → The block was reachable
  only after a patched-library mismatch. The full suite covers the start paths that remain.

- **#2179 recurs with no diagnostics in the field** → The retirement record gives the rebuild
  recipe and the recovery commit. Patch 1 applies cleanly to 6.12.1 qtdeclarative; patch 2 has
  been checked to apply on top of it (`patch --dry-run`, 2026-11-23); patch 3 is superseded.
- **The ownership-change gap may still be open** → Not measured on 6.12.1: the probes were not
  run (decision 3). The removal rests on field silence, and the record says so.
- **The Android EGL surface rewrite regresses pause/resume or screensaver wake** → Android is
  validated in the next beta, with those two behaviours and #1663-style reports named as the
  things to watch.
- **`aqt` or `install-qt-action` cannot fetch 6.12.1 for some target, notably iOS** → iOS
  installs through a pinned `aqtinstall` fork and a folder layout that changed in 6.12.0.
  Availability is checked for every target before the workflow edits are trusted, and the
  first CI run is the proof.
- **A cache key is missed and a job mixes Qt versions** → A final grep for `6.12.0` across the
  tree must return only the dated references this design keeps.
- **Rollback** → Revert the change. The override binary, manifest and guard return with it.
