# Tasks: Grind Visibility on Shot Review Surfaces

## 1. Shot detail page — Grind metric cell

- [x] 1.1 Create the feature branch (`git checkout -b improve-grind-visibility-shot-review`)
- [x] 1.2 Add a Grind `ColumnLayout` cell to the metrics row in `qml/pages/ShotDetailPage.qml`, positioned between the Dose and Output cells, following the existing cell pattern (caption `Tr` label over `subtitleFont` value)
- [x] 1.3 Render the RPM suffix (`· 340 rpm`) in caption font, baseline-aligned, when `shotData.rpm > 0`, reusing the Output cell's `Row` + `anchors.baseline` suffix pattern and matching ShotPlanText's format/translation for the rpm text
- [x] 1.4 Gate the cell with `visible: !!shotData.grinderSetting` so shots without a recorded grind reflow to five metrics
- [x] 1.5 Cap the value width (`Layout.maximumWidth` + `elide: Text.ElideRight`) so long free-text settings cannot crowd the row
- [x] 1.6 Add accessibility: `Accessible.role: Accessible.StaticText`, combined `Accessible.name` ("Grind: <setting>, <rpm> rpm" when rpm present), inner items `Accessible.ignored: true`
- [x] 1.7 Add the `shotdetail.grind` translation key with fallback "Grind" (verify against existing key conventions; reuse the existing rpm wording rather than adding a duplicate key)

## 2. Post-shot review page — equipment card reorder

- [x] 2.1 Move the `equipmentCard` Rectangle (with `Layout.columnSpan: 3` and its Change Equipment button row) from the top of the 3-column `GridLayout` in `qml/pages/PostShotReviewPage.qml` to the very bottom, after the Preset and Shot date items
- [x] 2.2 Rewrite the card's stale comment ("edited in the fields just below") to reflect the new order — dial-in fields above, card as trailing hardware context
- [x] 2.3 Grep for other references to `equipmentCard` / child-index assumptions in the file to confirm nothing depends on its previous position

## 3. Verification

- [x] 3.1 Compile check via Qt Creator MCP (list_projects → match worktree → build)
- [x] 3.2 Launch the app and open a shot with grind + RPM recorded: confirm the Grind cell renders between Dose and Output as `<setting> · <rpm> rpm`; confirm a shot without grind shows five metrics with no gap
- [x] 3.3 Narrow-window check: shrink the window (or phone-sized preview) and confirm a long free-text grind setting elides instead of overflowing the row
- [x] 3.4 On the post-shot review page, confirm the field grid order is Grind setting → RPM → Barista → Preset → Shot date → equipment card, and that Change Equipment, autosave, and undo still work
- [x] 3.5 Check the app log for new QML warnings/TypeErrors (must stay clean)

## 4. Documentation & wrap-up

- [x] 4.1 Update Decenza.wiki `Manual.md` (local clone at `~/Development/GitHub/Decenza.wiki`) wherever it describes the shot detail metrics row or post-shot review field order; commit separately in the wiki repo
- [x] 4.2 Open a PR referencing issue #1413 (requests 1 & 2; note request 1 shipped in #1396 and request 3 is deferred), then run `/opsx:archive` as the last commit on the branch before merge
