## Context

See `proposal.md` — Why. The three upstream fixes and their evidence are tabulated there and are
not repeated here.

Two facts about the machine this is executed on shape the plan, and both were discovered while
writing it rather than assumed:

1. **`~/Qt/6.11.1` no longer exists.** Only `~/Qt/6.11.2` is installed (`macos`, `ios`, `Src`). So
   the upgrade is not "install the new Qt"; it is "make the tree describe the Qt that is already
   there". Every `~/Qt/6.11.1/...` path in the docs is already dangling, including the `~/Qt/6.11.1/Src`
   that `CLAUDE.md` names as the reference source tree.
2. **Both Decenza build directories are already broken.** `Qt_6_11_1_for_macOS_Debug` in each clone
   caches `Qt6_DIR=/Users/jeffreyh/Qt/6.11.1/macos/lib/cmake/Qt6` and cannot reconfigure. They are
   5.8 GB and 5.3 GB of stale objects. This is why the build-tree deletion belongs in this change
   rather than being tidy-up: nothing builds locally until it is done.

`android/qt-overrides/` carries three patches, of which only the Android `qFatal` one is still
needed upstream-wise, and the decision (proposal, What Changes) is to drop it and ship stock Qt.
The patch source must survive that deletion.

## Goals / Non-Goals

**Goals:**

- The tree names exactly one Qt version, 6.11.2, everywhere it names one.
- Zero self-built Qt binaries in the repository, and zero build steps that write into an installed
  Qt tree.
- The QML diagnostics gate reaches every file with the stock tool, on every platform, with no
  skip mode and no CI/local asymmetry.
- The `QTBUG-140490/144207` fix is recoverable as source, from more than one place, with enough
  provenance to rebuild it without reconstructing the reasoning.
- Local disk returns to a state where a build works.

**Non-Goals:**

- Anything requiring Qt 6.12. `charts-qt-6-12-polish` stays blocked; `upgrade-qt-6-12` is untouched
  except for one note (below).
- Chasing the 6.11.2 fixes listed in the proposal's reason 3. They are why the bump is worth taking,
  not work items. No Decenza-side change is expected from any of them; where one removes the need
  for a local workaround, that is a separate change with its own evidence.
- Re-timing or re-scoping the a11y work in `#1300`. The same two fixes arrive from a different
  place; behaviour is unchanged.
- `~/Development/GitHub/qt-creator-master` (4.3 GB). It is a Qt Creator checkout, not a Qt 6.11.1
  artifact, and the MCP build path depends on Qt Creator. Left alone.

## Decisions

### Preserve the crash patch as source, in two places, before anything is deleted

`android/qt-overrides/arm64-v8a/*.so` is a *build output*. The input is qtbase commit **`358540b2`**
("Android: don't abort when the deadlock protector is contended"), 16 insertions in one file,
`src/plugins/platforms/android/qandroidplatformopenglwindow.cpp`, on top of `bb680db3`.

- **Copy 1 — already exists and was verified, not assumed:** the commit is on
  `origin/a11y/android-talkback-fixes` at `github.com/skialpine/qtbase`, i.e. off this machine.
- **Copy 2 — added by this change:** `git format-patch -1 358540b2` committed into the Decenza repo
  under `docs/qt-patches/`, with a short README recording the QTBUG numbers, Gerrit **735089**, the
  base commit, and the two-line rebuild recipe.

A text patch is not a shipped Qt runtime artifact, so this does not contradict the
`Decenza Ships Stock Qt Runtime Binaries` requirement — that requirement is about what gets
*packaged*, and is deliberately worded as plugin/jar/framework/library. Keeping the patch in-repo
is what makes the re-entry condition in that requirement actionable instead of aspirational.

*Alternative rejected:* rely on the GitHub fork alone. A contribution branch is exactly the kind of
thing that gets rebased or pruned, and the recovery path would then depend on remembering that it
ever existed. The patch is ~2 KB.

### Delete the whole skip mechanism, not just the bundle

`tools/qmllint-macos/` could be deleted on its own, leaving `QMLLINT_SKIP_UNLINTABLE`,
`UNLINTABLE_BY_TOOL_BUG` and `--skip-unlintable` as a dormant safety net. That is the wrong shape.
The mechanism's whole cost was never the code — it was that the gate ran in two modes with two sets
of numbers, and `scripts/qmllint_report.py` carries real arithmetic (the complete-run vs
partial-run ceiling reconciliation, the "counts fell, lock it in" suppression, the
`--update-baseline` refusal) that exists only to keep the two honest against each other. Keeping the
mechanism keeps that arithmetic and the reader's obligation to know which mode produced a number.

Delete both, and the gate has one mode. If a future Qt reintroduces an unanalysable file, the
correct response is a stated, *failing* gap — which is now the spec's position — not a revived skip.

*Alternative rejected:* keep `--skip-unlintable` as an escape hatch. An escape hatch on a gate is a
way to make the gate pass, and this one had the specific property that a skipped file emits no
warnings and therefore reads as clean.

### Re-verify the diagnostics baseline; do not assume it is unchanged

6.11.2 changes qmllint's import and singleton rules (QTBUG-144377, QTBUG-146759, QTBUG-146688) and
the memoization fix makes a 613-line file analysable that has never once been analysed by a stock
tool. Both directions of movement are plausible and neither is a regression by itself.

The procedure is the one `CLAUDE.local.md` and `feedback_verify_by_refusal_not_plausible_number`
were written from: run against a **fresh** build directory (a stale one describes the last build),
use Qt's generated `.rcc/qmllint/Decenza.rsp` rather than hand-assembled flags, check the exit
status, and diff the per-file and per-category sets — never the totals alone. A count going up after
removing a workaround is as likely to be the tool now reaching expressions it previously abandoned
as it is to be a defect.

If diagnostics appear, they are fixed in this change, not baselined. `CustomItem.qml` had 122
warnings the last time anyone could see them; the tree reached zero afterwards, so the expectation
is zero — but the expectation is not the measurement.

### Delete and recreate the two macOS build directories, keeping their paths

Both are unusable and must go. Recreating them raises one non-obvious cost, from `CLAUDE.local.md`:

- `DECENZA_MACOS_CODESIGN_IDENTITY` lives in the **cache**, and a cache wipe drops it. It has to
  come back from the Qt Creator kit's **Initial Configuration**, which is the only place that
  reapplies after a wipe. If it does not, the rebuilt app is ad-hoc-signed again and macOS silently
  drops its Local Network traffic — presenting as a WiFi-scale bug, not a signing one.
- The macOS Local Network grant is keyed to bundle id + certificate + **path**. Keeping the
  directory name `Qt_6_11_1_for_macOS_Debug` would preserve the grant but leave a directory named
  for a Qt that is gone. **Rename to `Qt_6_11_2_for_macOS_Debug`** and accept one re-approval per
  clone; the documented remedy if the grant does not take is a reboot, not toggling the row.

*Alternative rejected:* keep the old paths to dodge the re-approval. A directory whose name asserts
the wrong Qt version is precisely the kind of stale label that later gets believed — and the two
clones already drift enough (`reference_two_decenza_clones_qtcreator`) without that.

### Every Qt 6.11.1 tree in `~/Development/GitHub` goes, after the unpushed-work check

Three trees, 1.0 GB together, all of them 6.11.1-era and all now without a consumer:

| Tree | Size | What it was for |
|---|---|---|
| `qtbase-android-build/` | 260 MB | built `android/qt-overrides/…/*.so` |
| `qtbase/` | 435 MB | Gerrit source tree for the a11y + crash patches |
| `qtdeclarative/` | 318 MB | Gerrit source tree for the qmllint memoize fix, and the tree the patched `tools/qmllint-macos/` binaries were built in |

All three fed artifacts this change deletes, and every patch they carry is either shipped in 6.11.2
or preserved as source by the step above.

**Checked before proposing this, not asserted:** both git clones have clean working trees and no
stashes. `qtbase` reports 81 commits on `a11y-echo-upstream` / `a11y-talkback-upstream` that are on
no remote, which looks alarming and is not: they are overwhelmingly upstream Qt commits by Qt
developers (Thiago Macieira, Tor Arne Vestbø, Zbigniew Chyla and others) — a stale local snapshot of
`dev` from an old fetch. The only Decenza-authored commit among them is `83c181e2`, the a11y typing
echo, which is *in 6.11.2*. The crash patch `358540b2` sits on `a11y/android-talkback-fixes`, which
is fully pushed to `origin`. `qtdeclarative`'s four local branches each carry a copy of the memoize
fix, which is merged upstream and shipped in 6.11.2.

Re-run that check at execution time rather than trusting this paragraph — it describes the trees as
of 2026-08-18, and a tree can acquire work between proposal and apply.

*Alternative rejected:* keep the clones "in case". They are one `git clone` away, the branches that
matter are on GitHub, and a stale Qt source tree on disk is an active hazard — `CLAUDE.md` points
`~/Qt/<version>/Src` at the *matching* sources precisely so that claims about Qt behaviour are
checked against the version being built.

### Leave dated source comments alone

Comments reading "Verified against Qt 6.11.1" (`qml/Theme.qml:314`, `:421`,
`src/core/markdownrenderer.h:14`, `DecenzaActivity.java:83`) record an observation made on a date
against a version. Rewriting the version without re-observing converts a true statement into a
false one. They stay as-is; only paths, pins and instructions move to 6.11.2.

## Risks / Trade-offs

- **The Android `qFatal` abort returns to the field.** → Accepted, with the rate reasoning in the
  proposal. Mitigated by keeping the patch recoverable (above) and by the option of getting Gerrit
  735089 picked to 6.11 for 6.11.3 (September 2026). Watch Android crash reports for the
  `AndroidDeadlockProtector` signature after the first 6.11.2 release.

- **The a11y fixes arrive from a different binary and may not be byte-identical to ours.** Upstream
  reworked them through review; behaviour should match but has not been observed on device against
  the stock plugin. → `#1300` is a TalkBack issue with a reporter using a third-party screen reader
  (`reference_1300_reporter_uses_talkman`), so this needs a real Android check, and Android-only
  behaviour is validated in the next beta rather than a local sideload
  (`feedback_no_local_sideloads`). Task the beta check explicitly; do not assume parity.

- **A qmllint count that looks right may not be.** → Covered by the re-verification procedure above.
  The specific trap here is that the run must reach all 222 files and exit zero; a truncated run is
  the historical failure mode and it reports *fewer* problems, not more.

- **A 6.11.2 fix silently removes the need for a Decenza-side workaround, and the workaround now
  does something subtly wrong.** The candidates are `QNetworkRequest::setTransferTimeout()` after a
  redirect (QTBUG-147039) and the qtgraphs `DateTimeAxis` min==max crash (QTBUG-147262), both of
  which Decenza may be guarding around. → Out of scope by decision, but note it: if a guard's
  comment cites the upstream bug, it now has an expiry date, which is the class of stale
  justification `CLAUDE.md` warns about.

- **Deleting ~12 GB of build and source trees is irreversible and blocks all local building until
  the first reconfigure completes.** → Do it as one deliberate step with the codesign-identity check
  immediately after, not interleaved with source edits. Confirm with the user before running the
  deletions; nothing else in this change is destructive.

## Migration Plan

Order matters in exactly one place: **the patch is extracted and committed before
`android/qt-overrides/` is deleted.** Everything else can proceed in any order.

1. Preserve the crash patch (`docs/qt-patches/`), verify the fork branch still carries it.
2. Source and workflow edits: version bump, override deletion, qmllint-hack deletion, docs.
3. Local disk: delete the two stale Decenza build dirs and the three Qt 6.11.1 trees in
   `~/Development/GitHub`, reconfigure against 6.11.2, restore the codesign identity via the kit's
   Initial Configuration.
4. Verify: full build, full test suite, qmllint gate at 222/222, then the Android beta check for the
   a11y fixes.

**Rollback:** the branch is revertible as a unit. The Qt install itself is not part of the change —
6.11.2 is already the only Qt on the machine, so reverting the repo does not restore a working
6.11.1 build. That asymmetry is worth knowing before starting, not after: there is no way back to
6.11.1 locally without reinstalling it.

## Open Questions

- Whether to ask for Gerrit **735089** to be picked to `6.11` so the crash fix lands in 6.11.3. Not
  a blocker and does not change any spec, approach or task here — it is an upstream request that can
  be made at any time, and it only ever improves the outcome.
