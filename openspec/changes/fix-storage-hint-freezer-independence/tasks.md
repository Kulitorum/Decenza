## 1. Stop the data loss (independent of any UI change)

The force-clears are unconditional loss on the *save* path: a `storageHint`/`openedDate` written via the `bag_update` MCP tool is silently wiped the next time anyone opens and saves that bag in the dialog. Do this group first — it stands alone and is the half that destroys user data.

- [x] 1.1 `qml/components/ChangeBeansDialog.qml:593` — change `"storageHint": fFreeze ? "" : fStorageHint` to always write `fStorageHint`; update the adjacent comment ("cleared to '' whenever the bag is frozen") which now states the opposite of the rule
- [x] 1.2 `qml/components/ChangeBeansDialog.qml:604` — change `fields["openedDate"] = fFreeze ? "" : (...)` to always write the parsed `fOpenedDate`; update its comment ("cleared when the bag is frozen") the same way
- [x] 1.3 Verify no other write path clears either field on account of `fFreeze`/`isFrozen` — grep `fFreeze`, `isFrozen`, `storageHint`, `openedDate` across `qml/` and `src/` and confirm the create path (`formMode !== "edit"`) does not clear them either

## 2. Always show the storage-hint and opened-date controls

- [x] 2.1 `ChangeBeansDialog.qml:1736` — remove the `visible: !root.fFreeze` binding from the storage-hint row so the dropdown is always shown; update the comment block above it (`:1733`, which explains the no-"frozen"-value hiding rationale) to state the orthogonal-axes rule instead
- [x] 2.2 `ChangeBeansDialog.qml:1763` — change the `openedDate` field's `visible: !root.fFreeze && root.formMode === "edit"` to `root.formMode === "edit"` (keep the edit-mode gate; drop the freeze gate)
- [x] 2.3 Resolve the dropdown's label wording under the plan reading (design.md Open Questions): present-tense "Storage" now reads wrong on a frozen bag. Update the `changebeans.form.storageHint` translation key and its `.accessible` variant if the visible string changes; keep the fallback text in sync
- [x] 2.4 Confirm the freeze toggle's `onCheckedChanged` handler (`:1697`) does not reset `fStorageHint`/`fOpenedDate` in the form state, mirroring the save-path fix

## 3. "Mark Opened" on thawed bags

- [x] 3.1 `qml/components/BagCard.qml:454` — change `visible: !card.isFrozen` to `card.portionOutOfFreezer`, a new `readonly property bool` next to `isFrozen` (`:28`) defined as `!isFrozen || defrostDate.length > 0`. Do NOT redefine `isFrozen` — a bag with portions still frozen IS frozen, so it is correct for the "Frozen" badge and the freeze toggle.
  **Naming correction (user, during apply):** beans are frozen in PORTIONS and pulled out one at a time, so the bag keeps portions in the freezer indefinitely and `defrostDate` tracks only the CURRENT PORTION. The original task text here said to name the predicate for "no portion currently in the freezer" (`portionInFreezer`) — that name asserts something never true of a frozen bag. Same truth table, wrong concept; renamed to `portionOutOfFreezer`. "Thaw" therefore stays available on a thawed bag for the next portion, and is a recurring action rather than a state transition.
- [x] 3.2 Leave the "Thaw" button's `visible: card.isFrozen` (`:440`) alone — re-thawing a later portion is legitimate on a thawed bag; a thawed bag intentionally shows both actions
- [x] 3.3 Update the comment above the "Mark Opened" button (`:450-452`, "Non-frozen bag: ...") to describe the new eligibility rule
- [ ] 3.4 Check the two buttons fit the card width on the tablet's layout (design.md Risks) — the both-visible case only occurs on frozen-and-thawed bags

## 4. Tests

**Correction (found during apply):** 4.1-4.3 as originally written are not implementable. The force-clears live in `ChangeBeansDialog.qml`, and this project has no QML test harness — no `QUICK_TEST_MAIN`, no `tests/*.qml`; the suite is pure C++ Qt Test. There is no way to drive the QML save path from a C++ test, so the QML-save regression cannot be automated here. The data-layer half IS covered (4.4), and the QML half moves to manual verification (5.1/5.5). Do NOT paper over this with a C++ test that re-implements the QML logic instead of exercising it — that would assert the test's own copy, not the fix.

- [x] 4.1 ~~Regression for 1.1 across a dialog save~~ — NOT AUTOMATABLE (no QML test harness); covered by manual 5.1
- [x] 4.2 ~~Regression for 1.2 across a dialog save~~ — NOT AUTOMATABLE (same reason); covered by manual 5.5
- [x] 4.3 ~~MCP-set hint survives a dialog save~~ — NOT AUTOMATABLE (crosses the QML boundary); covered by manual 5.5
- [x] 4.4 A bag with `frozenDate` + `defrostDate` + `storageHint` + `openedDate` all set round-trips through storage with all four intact (the state archived `design.md:28` names as legitimate and the old UI made unreachable)
- [x] 4.5 Freshness anchoring is unaffected: a frozen, never-thawed bag with a `storageHint` and no `defrostDate`/`openedDate` yields no aging anchor — `storageHint` contributes no date (assert against `buildBeanFreshness()`)
- [x] 4.6 Run the full suite and confirm no regressions in `tst_coffeebags`, `tst_dialing_helpers`, `tst_dialing_blocks`

## 5. Manual verification

- [ ] 5.1 Create a bag with the freeze toggle ON and a storage hint selected — confirm the hint persists on save and is still there on reopen (this is the exact case the old build silently cleared)
- [ ] 5.2 On a thawed bag (`frozenDate` + `defrostDate`), confirm both "Thaw" and "Mark Opened" appear and each sets only its own field
- [ ] 5.3 On a frozen, never-thawed bag, confirm "Mark Opened" does NOT appear
- [ ] 5.4 Confirm all four fields survive an app restart
- [ ] 5.5 Carries the regression 4.1-4.3 could not automate: set `storageHint` + `openedDate` on a FROZEN bag via the `bag_update` MCP tool, then open that bag in the Change Beans dialog and save WITHOUT touching either field. Re-read via `bag_list` and assert both values are still present — on the pre-fix build this is exactly the path that silently wiped them

## 6. Docs & housekeeping

- [x] 6.1 Update the GitHub wiki manual's bean/bag section — the storage-hint dropdown is no longer described as freeze-dependent, and "Mark Opened" is available on thawed bags
- [ ] 6.2 `/opsx:archive` as the last commit on the branch, so the archive + spec promotion land inside the PR
