# History data, backup files, diagnostics and updates — Mac acceptance

## Scope and decision

Task group 23 uses the same application QML and Qt Remote Objects services in
native and Wasm. ZIP is the shared backup export format; restore accepts ZIP,
legacy DCBK, and older database files. The user accepted this format and removal
of fixed backup size quotas on 2026-10-23. Numeric precision, offsets, archive
structure and real I/O failures are still validated. Legacy HTML and API/MCP
routes remain available.

## Verified builds and tests

- Qt Creator combined build **1792763852523** passed, including the **359/359**
  QML diagnostic gate. Build **1792763852530** also passed after exposing backup
  result text to accessibility. Both used the original checkout's macOS Debug
  project and its dependent single-thread Wasm Release build.
- Full Qt Creator run **1792763853149** passed **206/206** suites, with zero skips
  or test warnings, in 37.250 seconds. This includes source-duplication and QML
  inventory checks. The separate QtRO source-generation library and narrow local
  network test library avoid repeated generated commands/production compiles.
- Existing focused suites cover client-bound backup export, cancellation before
  and after copy completion, worker drain admission, archive validation before
  mutation, settings/profile/media payloads, source destruction during settings
  signals, exact offsets above 2 GiB, strict upload metadata, stale begin replies,
  and one-use completed-upload handoff.
- Migration fixtures exercise real HTTP and QtRO: manifest and AI persistence,
  HTTP/malformed-payload failures, progress, retry, cancellation, disconnect and
  no replay. Disposable SQLite workers verify unique input leases and admission
  until physical completion. MainController refreshes imported profiles when
  migration drains, including partial failure/cancellation.
- Update fixtures exercise the canonical updater's version, guards, download
  progress/failure, immediate refusal, QtRO detach/reconnect and no replay.
  Shipping QML handler tests cover APK confirmation and request correlation;
  local APK validation and non-Android refusal are tested without installing.

## Actual Mac file operations

Package **4df250f92034861c8079195e995022e8a5d4823a3b0049957c5e2e91c869d081**,
native PID 45155, host epoch `7a0b78b2-19ec-4bc5-8cb2-e54bc036a3d4`:

- The Wasm client's Save Backup control downloaded a **167,772,308-byte**
  disposable ZIP, larger than the former 128 MiB cap. Download and source SHA-256
  both equal `3083782568b2d2362f56c62bec7fc9df93524c07669145c7ff941d24c8b71dd4`.
- Microsoft Edge's actual macOS file picker selected an empty DCBK fixture.
  The existing shared dialog displayed its basename, categories and merge mode;
  Cancel exited before upload. Selecting it again and confirming succeeded.
  A malformed DCBK then displayed a corruption error; a subsequent valid empty
  DCBK succeeded, proving recovery.
- Native Decenza selected the same empty fixture through its file picker and
  shared confirmation, then completed restore.
  Live checks used merge mode and disposable no-op/corrupt archives, never a
  restore of the user's real backup. The displayed history stayed at 1,307 shots.
- Browser diagnostic downloads produced a 2,069,403-byte `debug.log` and a
  325,299-byte ZIP containing one readable `debug.log` member (2,070,177 bytes).
  Both contained application markers and a memory summary. Native Save Log
  produced a 2,069,456-byte log in the temporary acceptance directory. These
  capture different moments, so byte equality between logs is not expected.
- The earlier native ZIP save and browser save on package `b417a464` both matched
  the real host archive: 13,133,863 bytes, SHA-256
  `14bc0b84f134b41d4b66cb36750deabb7b1e0364fa74fac009f8b2aaee1d6f7d`.
- The in-app browser's File System Access chooser was not exposed to its
  automation event; actual upload acceptance used Edge's native picker instead.
  A native log-save action also caused a computer-control timeout; reconnecting
  found the correct open Save Log sheet, which was completed successfully. The
  host remained responsive throughout. These are tool observations, not app
  success claims or confirmed product defects.

## Restart and platform checks

The final tested restart used package
**fdcb1e6c5d27a8d68a7174503fa2a860d191a8d56ed6bcba96063dcb9fbc2ec3**,
PID 58462, epoch `19a56b59-e6ce-4a11-aa4a-b3954a17857b`. Each previous process
was stopped through Qt Creator; process/port disappearance was verified before
Run. The stale browser disconnected, and a refreshed matching bundle connected.
The synthetic dated backup was hash-verified and removed before this restart;
all original backups were preserved.

`/`, `/wasm/`, `/legacy/` and `/api/session` returned HTTP 200. A separate MCP
session on port 8888 passed initialize, initialized, ping, tools/list (67 tools),
read-only machine_get_state, and session close; server version was 1.20.0.
The simulator was connected/Ready with no flow and preparation mode Espresso.
Native and browser About showed 2.0.9/build 3631. Their firmware dialogs both
showed installed simulator version 1 and disabled Update Now. APK upload was
absent on the Mac host, as required by the host capability. Channel preferences
were not changed.

Real Android PackageInstaller completion, other platform installers and a real
DE1 firmware flash remain unperformed platform/hardware checks, not Mac passes.

## Final ZIP regression — passed

An additional uploaded-native-ZIP round-trip fixture exposed an existing media
merge defect: copying new files while keeping the old catalog made new media
invisible. The fix stages bytes on a worker and uses the same canonical media
adoption helper as DCBK restore, preserving existing items and uploaded metadata.
The native ZIP fixture now includes its catalog and a preexisting media item;
a disposable SQLite file also exercises the older raw-database restore path.
ZIP extraction's empty-parent-directory warning was fixed as well.

The user released Qt Creator for this work. Combined build **1792763852540**
passed in 91.471 seconds with zero errors and three local linker
warnings: two Homebrew OpenSSL deployment-target warnings and the existing
large unwind-table warning. Focused run **1792763853154** passed both backup and
media suites; full run **1792763853155** then passed **206/206** suites in 34.880
seconds, with zero skips or test warnings. These results include the final media
merge correction; run 1792763853150 preserves its pre-fix failing reproduction.
The ZIP fixture verifies actual settings/profile/media payloads and preserves an
existing media catalog item. Its raw-database fixture uses an initialized empty
SQLite database; it does not establish a nonempty shot-history round trip.

The user authorized replacing the other checkout. Its process had already
exited when rechecked, and port 8888 was free. Qt Creator Run launched only the
original checkout's executable, PID **90504**, package
**7f92b4fdbbe11c293982b404c2a490f02d49a9ad80f5486fd848976938569a7c**,
epoch `346a32d8-beb5-4a6c-aedc-ba96d0968a24`. The browser reloaded and connected
to that matching package. Root, Wasm alias, legacy HTML and session returned
200; a fresh MCP session again passed initialize/initialized/ping/tools-list,
read-only machine state and close (67 tools). Native's actual picker selected a
disposable empty ZIP; the shared merge confirmation completed with “Backup
restored successfully.” Edge then selected the same ZIP through its actual macOS picker and completed
the matching merge dialog successfully. Native and browser retained 1,307 shots. This live check
exercises the ZIP upload/restore path without restoring the user's data; actual
media payload/catalog merging is covered by the isolated regression above.

The short wiki manual edits are prepared in `/tmp/decenza-wasm-followup-wiki`;
`wiki-manual-task23.patch` records them for publication with the feature. They
cover ZIP save, file restore, diagnostic log downloads and host-only Android
APK installation, without claiming cross-device/hardware acceptance.

## PR #2079 restore review — 2026-10-24

Confirmed and corrected four Codex findings in the existing DCBK restore path:
validate media names/types before clearing the library; clear old conversations
on Replace with shots (including an empty conversation archive); remove omitted
root profiles while preserving internal `_*.json` files; reject a selected shot
replacement without `shots.db` before applying any category. Media preflight and
adoption share one file-type check, and both conversation paths share cleanup.

Original-workspace Qt Creator combined native/Wasm build **1792763852618**
passed in 194.142 seconds. Its three linker warnings are the existing Homebrew
OpenSSL deployment-target pair and large unwind-table warning. Focused backup
run **1792763853189** passed; full run **1792763853190** passed **206/206** suites
in 44.400 seconds, with no skips or test warnings. Regression cases use disposable
media, settings, profile directories and SQLite databases, not the user's data.

After restarting through Qt Creator, the original-checkout simulator served root,
Wasm alias, legacy HTML and session successfully. A fresh MCP session passed
initialize/initialized/ping/list/read/close with 67 tools. The earlier all-platform
CI run 40106307189 covers commit `d3eb5e3f4`; it does not cover these later restore
fixes. No additional CI build was dispatched for them.

The independent review then found that ordinary DCBK exports include
`media/catalog.json`. Parsing now excludes that metadata (and the already-ignored
`media/index.json`) from media validation and adoption. The existing round-trip
fixture includes the actual catalog shape and verifies the image still imports.
Combined build **1792763852622** passed; focused backup run **1792763853191**
passed without test warnings. The prior full-suite result above predates this
small correction; no new full-suite run or CI build was dispatched.

## Raw database and ZIP review — 2026-10-24

Raw `.db` restores now disable non-shot categories in the shared backup manager,
protecting native and browser callers. The existing ZIP/raw-database fixture
reproduced an unrelated sibling-settings import in Replace mode before the fix
(run **1792763853192**). It now also verifies that profiles and the complete
personal-media catalog survive. Combined build **1792763852625** passed; focused
backup run **1792763853193** passed without skips or test warnings.

The ZIP memory concern is confirmed in stock Qt 6.12: `QZipReader::fileData()`
reads the compressed entry and allocates its expanded bytes
(`qtbase/src/corelib/io/qzip.cpp:937–963`); `extractAll()` also calls `fileData()`
(:1045). This behavior predates this PR. Extraction finishes in a temporary
directory before live categories are changed, but a large expanded entry can
exhaust memory. No low entry-size cap was added: it would prevent restoring
some backups the app can create. The maintainer subsequently approved QuaZip for both reading and writing,
with platform verification. The implementation and current results follow.


## Streaming ZIP and build reuse — 2026-10-24

Native ZIP consumers now share the chunked QuaZip reader/writer; private Qt ZIP
APIs and the ZIP-only GuiPrivate dependency are removed. Existing backup tests
cover 160 MiB entries at STORE/DEFLATE 6/9 with reads no larger than 64 KiB,
independently produced ordinary/ZIP64 archives, CRC corruption, and a complete
native ZIP export/replace-restore round trip. That round trip also exposed and
fixed lost personal-media metadata: catalog paths are absolute, so restore now
matches them by filename. Raw database restore still preserves non-shot data.

Combined Qt Creator build **1792763852638** passed. Focused run
**1792763853197** passed 4/4 suites; full run **1792763853198** passed **206/206**
in 48.240 seconds, with no skips or test warnings. The synthetic repetitive
160 MiB payload compressed to 163,221 bytes at both levels 6 and 9, versus
167,772,290 bytes stored; this is not a real-history compression benchmark.
Default level 6 remains; already-compressed media uses STORE.

Completed CI run **40106307189** showed three iOS compile passes, including all
359 QML units each time. The workflow now uses one final archive compile. Other
native jobs showed one redundant HTTP-reader compile during late embedding;
runtime resource detection removes it. Every Ninja platform now reuses the
existing QML-object timestamp/size check around embedding. Local builds compile
the changing package identity in one small unit, skip duplicate Wasm CMake
configuration, use the normal parallel budget, and stamp lint/package outputs.
The unchanged combined build before these optimizations took 0.636 seconds
(**1792763852636**). Updated all-platform timing and packaging verification are
pending the newly authorized build-only CI run; the earlier green run does not
cover QuaZip or these build changes.


The rebuilt original-workspace simulator (PID 32842) passed live root, Wasm alias,
legacy HTML, session and MCP checks (67 tools). The Wasm UI connected and rendered
Settings; legacy debug ZIP returned HTTP 200 and passed independent CRC validation.
All-platform build-only run **40115084631** covers `1bfcc7b86`, with caches enabled
and publishing disabled. Source invariants passed for that commit.

The subsequent nightly optimization keeps Wasm OFF and replaces app-executable
dependencies with Qt's actual registry/source-copy targets. The local Ninja test
closure dropped from 1,735 to 983 compile commands (43.3%), with zero app objects
or app QML units; lint likewise contains neither. Combined build
**1792763852641** passed; focused registration/package/assets/session run
**1792763853199** passed 5/5 without skips or test warnings. Linux sanitizer timing
still needs CI measurement. Leak fixes for the earlier ASan failures are being
validated separately on `fix/ci-build-warnings`; they are not included here.


Local native Debug now uses the standard Emscripten fast development link while
Release CI retains full link optimization. The measured Wasm link dropped from
52.344 to 3.043 seconds; the complete combined build **1792763852646** passed in
63.645 seconds. The larger development Wasm (100,808,600 bytes) required raising
both producer and server asset limits from 96 to 128 MiB. Package/assets/session/
registration run **1792763853200** passed 4/4. After a Qt Creator restart, the fast
bundle rendered connected Settings and root/legacy/session/MCP probes passed.
An unchanged paired build **1792763852647** took 0.518 seconds.


### Latest PR review assessment (2026-10-24)

Confirmed fixes in the final batch: `.dcbackup` profile payloads use canonical
`Profile::isValid()` before any replacement, case-colliding paths are rejected
before mutation, malformed conversation records cannot clear the existing store,
and an absent conversation entry means an empty set for Replace (Merge preserves
the destination). Legacy personal-media catalog names/dates/durations are retained,
including merge duplicate detection. ZIP shot replacement now fails before other
categories change when its database is absent or invalid. Existing focused backup
fixtures cover these behaviors. Rebased combined build `1792763852652` passed;
full run `1792763853201` passed **207/207** suites in 42.050 s with no skips or
test warnings. A subsequent temporary body-comment build measured the incremental
path (58.806 s, zero QML compiles); the probe was reverted exactly. Build
`1792763852655` and focused backup/package/assets run `1792763853202` passed 3/3.

All-platform run `40118445814` started on rebased `ccfb89942`, with publishing
OFF. Android stopped during configure because zlib.net returned bytes that did
not match the pinned release checksum. The author-owned GitHub release archive
was independently downloaded and matched the same checksum; it is now the first
URL, with zlib.net retained as fallback. The other jobs continue, so remaining
platform results and corrected-download CI validation are still pending.

The review's proposed JSON byte caps and category fanout caps are not added.
Multi-gigabyte JSON objects and 100,000 empty image entries are constructed extremes,
not demonstrated ordinary exports; adding quotas contradicts the accepted large
backup behavior. JSON parsing still requires memory proportional to JSON content,
and very large personal-media catalogs can be slow to adopt. Streaming ZIP entry
I/O addresses the ordinary large database/video case without a new size quota.

Optimized nightly ASan run `40115684359` reduced Build from 60m19s to 24m36s
(976 objects, zero app objects, 55% cache hits). The six known leak failures remain;
UBSan stopped at the GCC warning already fixed on main in #2080. Neither leg is
recorded as green. Final all-platform CI follows integration of that main fix.


The next Codex review identified the corresponding ZIP-path cases. ZIP profile
and settings/conversation preflight now runs before any category is mutated;
Replace clears an omitted/empty conversation set, while a refused shot import
preserves it. Existing disposable-data ZIP fixtures verify invalid profiles keep
the original files and a zero-conversation backup clears the destination index.
Combined Qt Creator build `1792865152465` passed (46.997 s); full run
`1792865152931` passed **207/207** in 35.000 s, without skips or test warnings.


Final review follow-up: failed settings imports now make the overall DCBK restore
fail with a partial-settings error instead of announcing success. ZIP adoption
uses the same case-insensitive `.json` rule as its preflight. Existing fixtures
now exercise an invalid pitcher selection and an uppercase `.JSON` profile.
Combined build `1792865152468` passed (50.223 s); focused backup/debug run
`1792865152932` passed 2/2 in 5.510 s without warnings. The preceding full suite
remains 207/207; the current on-demand Linux job covers the final full suite.

The proposed browser ZIP cap is not reinstated, per the accepted no-quota
requirement. Browser downloads use Blob-backed chunks and remain subject to the
browser’s storage/memory implementation; the measured 160 MiB Mac download does
not establish arbitrarily large or phone/tablet acceptance. Those device and
large-download limits remain honest validation holds, not a new application cap.
