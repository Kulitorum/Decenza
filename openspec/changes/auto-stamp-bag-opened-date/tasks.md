## 1. Implementation

- [x] 1.1 Add `CoffeeBag::openedDateForShot` and stamp `openedDate` in the post-shot bag update, including the shot snapshot
- [x] 1.2 Remove "Mark Opened" from `BagCard.qml` and the web `/beans` page; keep Thaw on thawed frozen bags on the web
- [x] 1.3 Add `openedDateForShot` cases to `tst_coffeebags`
- [x] 1.4 Send the shared `beanFreshness` block from the MCP current-context resource
- [x] 1.5 Refuse future lifecycle dates (`CoffeeBag::lifecycleFieldError`) in storage, MCP and web; cap the app pickers and date fields at today; web Thaw uses the local date
- [x] 1.6 AI freshness: only freezing pauses aging, `referenceDate`, null for unrecorded storage dates in sessions
- [x] 1.7 Validate storage-date format and storage type at the storage boundary; unfreezing clears the thaw date
- [x] 1.8 MCP `bag`: freeze fields on create, reject dead grinder keys, shared kind-only lists; bump McpSurfaceVersion
- [x] 1.9 Centralize the app/web bag rules in C++ (storage options, lifecycle line, card actions, restock, coffee-only fields, detail merge, edit diff)
- [x] 1.10 Bean workflow review: opened date alone no longer marks storage known; restAgeDays when known; roastDate in history and the anchor (with sameBagAsCurrent); current_context built by the shared bean block; Freeze card action; Restock carries freeze and storage; dates must be in order; post-shot summary shows storage dates; finishing the active bag moves to its successor; a new frozen bag doesn't take over; "Thawed"/"Stored frozen"/"Storage" wording
- [x] 1.11 StyledComboBox applies picks through the C++ setter (ComboBoxSelection) so caller bindings survive; removed the bag editor's local re-binds

## 2. Verification

- [x] 2.1 Build and run the full test suite in Qt Creator
- [ ] 2.2 HELD for the next beta (platform-independent code, already exercised on the Mac build): first shot from an unopened bag shows "Opened <today>" on its card; a second shot leaves it alone
- [ ] 2.3 HELD for the next beta: thaw a frozen bag, pull a shot, and confirm the opened date moves to today
- [x] 2.4 Web /beans: edit a bag (details, dates, storage type), restock, thaw, finish and delete; confirm each matches the app (unchanged save writes nothing; storage and detail edits write only themselves; future thaw refused)
- [x] 2.6 App Beans page: lifecycle line and buttons from the shared rules; Thaw picker blocks future days; typed future date reverts; a storage edit writes only storageHint
- [x] 2.7 App, after a restart: pick a storage type in Edit, then open Restock — the combo shows "Not specified"
- [x] 2.8 App: Freeze on an unfrozen bag opens a headed picker that stops at the roast month; Restock of a frozen vacuum-sealed bag opens frozen today and vacuum-sealed; the post-shot review shows the bean's dates
- [ ] 2.5 Fix the shot snapshots from Hometown portion 1 (frozen 2026-09-03, thawed 2026-09-09, opened 2026-09-10) once shots can be corrected

## 3. Docs

- [ ] 3.1 Update the wiki manual's Beans section: the opened date fills in on the first shot and is editable from Edit
