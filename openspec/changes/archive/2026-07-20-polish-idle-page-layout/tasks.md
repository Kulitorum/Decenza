## 1. Ground the fix

- [x] 1.1 In `qml/pages/IdlePage.qml`, map exactly which elements can collide with (a) the expanded inline carousel and (b) each bottom-nav picker, on both the default layout and a deliberately over-populated one; note the smallest subtree that would need to carry a transient offset in each case.
- [x] 1.2 Confirm the seven pickers sharing the float-above block (`EquipmentItem`, `EspressoItem`, `SteamItem`, `FlushItem`, `RecipesItem`, `BeansItem`, `HotWaterItem`) and that History is navigation-only.
- [x] 1.3 Build a test layout that fully populates the idle zones — this is the case the fix exists for and the one all verification must exercise.

## 2. Shared open / slide / restore rule

- [~] 2.1 The **clearance rule** (free-space test, directional transient offset, bound, restore) is centralized in `IdlePage` (`requestPanelClearance`/`releasePanelClearance` + the transforms); each picker only reports open/close via a 4-line snippet. The pre-existing duplicated float-above **y-positioning** was left in place — refactoring that is a separate cleanup not required to fix the overlap, and folding it in would touch the 7 files more invasively for no behavior change.
- [x] 2.2 Implement the free-space test against **unoffset** geometry (so the test can't feed back into the offset it produces), returning the exact offset required to vacate the panel's region — zero when the region is already free.
- [x] 2.3 Apply the offset to the smallest subtree that resolves the collision, animated with the existing 200 ms easing; bind it to panel open/close state (event/binding-driven, no timers).
- [x] 2.4 Bound the offset so essential content is never pushed off-screen; when the full offset can't be satisfied, apply the maximum useful amount and keep the panel fully usable.
- [x] 2.5 Guarantee exact restore on close by making the offset a single reversible bound property, not a sequence of imperative nudges.
- [x] 2.6 Point all seven pickers at the shared rule; confirm none retains a divergent copy.

## 3. Inline carousel uses the same rule

- [x] 3.1 Apply the same open/slide/restore behavior to the inline quick-pick carousel (`IdlePage.qml:779-`) so expanding a category tile slides the bottom-anchored content only when the carousel's region is occupied.
- [x] 3.2 Keep the carousel at its natural size and anchor — do not bound, shrink, or paginate it to fit (explicitly reversing the earlier clearance-based approach).
- [x] 3.3 Leave the existing `lowerMidBarFits` visibility heuristic alone unless it actively conflicts; the slide, not the heuristic, is what prevents overlap now.

## 4. Focus semantics (no visual change)

- [x] 4.1 Ensure each modal picker moves focus into its content on open and restores focus to the invoking control on close; verify Esc/back and tap-outside dismiss still work.
- [x] 4.2 Confirm `Accessible` roles/names on the panel content are correct so a screen reader lands somewhere meaningful.
- [x] 4.3 **Do not** enable `dim`, add a backdrop/scrim, or introduce dialog chrome — verify by diff that the panels' surface, radius, and chrome are byte-for-byte unchanged.

## 5. Verify

- [x] 5.1 Quick compile check via Qt Creator MCP (0 errors, 0 warnings; touched QML AOT-compiles).
- [x] 5.2 **Roomy layout:** open each picker and expand each category tile — confirm **nothing moves** and the page is visually identical to today apart from the panel.
- [x] 5.3 **Fully populated layout (from 1.3):** open each picker and expand each tile — confirm the panel opens at its anchor at full size, content slides just enough, nothing overlaps, and nothing essential is pushed off-screen.
- [x] 5.4 Close each panel and confirm content returns to its **exact** prior position with no residual offset, at several window heights.
- [x] 5.5 Confirm stored zone `offsets`/`scales`/`alignment` are unchanged after opening/closing panels (the slide never writes back to config).
- [x] 5.6 Confirm no new QML warnings and **no binding-loop warnings** in the running app log.
- [ ] 5.7 `openspec validate polish-idle-page-layout --strict`.

## 6. Docs

- [ ] 6.1 If any `docs/CLAUDE_MD/*` describes the idle-page zones or quick-pick panels, note the open/slide/restore invariant; otherwise confirm no doc update is needed (no user-facing feature change).
