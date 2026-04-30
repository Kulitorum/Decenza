# Tasks

## 1. QML binding

- [x] 1.1 In `qml/components/ShotAnalysisDialog.qml`, change the `analysisLines` binding to prefer `shotData.summaryLines` when present, fall back to `generateShotSummary(shotData)` otherwise. Pattern uses `Array.isArray(pre) && pre.length > 0` to handle both missing and empty cases.
- [x] 1.2 Audited other call sites: `ShotDetailPage.qml` line 261 and `PostShotReviewPage.qml` line 409 both instantiate `ShotAnalysisDialog` with `shotData` from the page-level binding. The fix is inside the dialog component itself, so both consumers get the dedup automatically — no per-page edits needed.

## 2. Verify

- [x] 2.1 Build clean (Qt Creator MCP, 0 errors / 0 warnings).
- [x] 2.2 1778 existing tests pass; the QML binding change has no C++ test surface to break (analyzeShot and convertShotRecord are already locked in by tst_shotanalysis tests added in PR #933).

## 3. Tests

- [x] 3.1 No new C++ tests needed. The C++ side of the contract — that `convertShotRecord` populates `summaryLines` — is structurally guaranteed by the explicit assignment in `convertShotRecord` (PR #933) and exercised by every existing test that runs through the conversion. The QML-side prefer-pre-computed logic is a 5-line property binding; lockable only via QML test harness which the project doesn't have.
- [x] 3.2 Manual smoke test path (for a future verifier): open the Shot Detail page on (a) a clean shot, (b) a chokedPuck shot, (c) a pourTruncated shot. Lines must render identically to pre-change behavior. The fast path should fire on all three (since they all flow through `convertShotRecord`); the fallback only triggers for legacy callers without `summaryLines`.

## 4. Docs

- [x] 4.1 Updated `docs/SHOT_REVIEW.md` §3 Pipeline → "In-app dialog path" with the new prefer-pre-computed wording.
