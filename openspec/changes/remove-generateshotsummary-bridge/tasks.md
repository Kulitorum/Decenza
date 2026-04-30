# Tasks

## 1. Verify no other callers exist

- [ ] 1.1 `git grep -n "generateShotSummary" src/ qml/` — confirm the only QML caller is `ShotAnalysisDialog.qml`'s fallback branch and the only C++ references are the declaration / definition / test cases. If any other caller surfaces, surface it explicitly and reconsider.

## 2. Delete the Q_INVOKABLE

- [ ] 2.1 Remove `Q_INVOKABLE QVariantList generateShotSummary(const QVariantMap&) const;` from `src/history/shothistorystorage.h`.
- [ ] 2.2 Remove the `ShotHistoryStorage::generateShotSummary` definition from `src/history/shothistorystorage.cpp`.

## 3. Simplify dialog binding

- [ ] 3.1 In `qml/components/ShotAnalysisDialog.qml`, replace the multi-branch `analysisLines` property with the simpler:
  ```qml
  property var analysisLines: {
      if (!analysisDialog.visible) return []
      return Array.isArray(shotData?.summaryLines) ? shotData.summaryLines : []
  }
  ```
- [ ] 3.2 Remove the multi-line comment block above the property — the simplified binding is self-explanatory.

## 4. Tests

- [ ] 4.1 Remove any tests in `tst_shotanalysis.cpp` or `tst_shotsummarizer.cpp` that exercised the `QVariantMap`-roundtrip path through `generateShotSummary`. Verify nothing else implicitly depended on the bridge.
- [ ] 4.2 Confirm remaining tests still cover the canonical pipeline (`tst_shotanalysis::analyzeShot_*`, `tst_shotanalysis::badgeProjection_*`, `tst_shotsummarizer::*`).

## 5. Verify

- [ ] 5.1 Build clean (Qt Creator MCP).
- [ ] 5.2 All tests pass.
- [ ] 5.3 Manual smoke: open the Shot Summary dialog on a few shots — should render lines identically to pre-change behavior. Open one shot whose `shotData.summaryLines` happens to be empty (if such a shot exists in the test corpus) and confirm the dialog renders a "Shot Summary" header with no body, not a crash.

## 6. Documentation

- [ ] 6.1 Update `docs/SHOT_REVIEW.md` §3 to drop references to `generateShotSummary` as the bridge function. Replace with: "The dialog reads `shotData.summaryLines` directly from `convertShotRecord`'s output."
- [ ] 6.2 Remove the stale "matches `ShotHistoryStorage::generateShotSummary`'s input for the dialog" reference in `src/ai/shotsummarizer.cpp` (if still present after #935 — verify).
