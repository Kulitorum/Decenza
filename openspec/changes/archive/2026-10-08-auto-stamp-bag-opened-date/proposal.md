## Why

"Mark Opened" stayed on a bag card after it was used, because nothing checked whether the bag already had an opened date. The button was also unnecessary: a shot cannot be pulled from a closed bag, so the first shot already marks when it was opened.

A review of the bag dates for the AI advisor then found they did not make sense to it. An opened date reset the beans' age, so a bag opened late read as fresh; days on the counter before freezing were ignored; nothing gave a date to measure from; a shot with no thaw recorded inherited another shot's; and the app, web page and MCP each validated, merged and diffed bag edits their own way.

## What Changes

- Saving a shot sets the active bag's opened date to today when the current portion has none: the bag was never opened, or its opened date is older than the latest thaw. A shot from a frozen bag with no thaw recorded is taken as one serving out of the freezer with the rest put back, so it stamps nothing.
- The shot's own snapshot carries that date.
- "Mark Opened" is removed from the app's bag card and the web `/beans` page. The opened date stays editable in the bag editor.
- Web `/beans`: "Thaw" now stays on a thawed frozen bag for the next portion, matching the app. Before, it disappeared after the first thaw. It also records the local date; it used UTC, which is tomorrow on a US evening.
- Roast, frozen, thaw and opened dates can no longer be in the future, on any surface. The app's pickers stop at today.
- The MCP `decenza://dialing/current_context` resource sends the same `beanFreshness` block as every other AI surface, replacing its own `daysOutOfFreezer` and `roastDate` fields.
- AI freshness: only freezing pauses aging. `openedDate` is air exposure, not a reset; days before freezing count; a `referenceDate` (the shot's date) is sent; a frozen bag with no thaw is ground from the freezer. In `dialInSessions`, a shot with no storage date recorded carries an explicit `null` instead of inheriting the session's.
- Bag writes from every surface: storage dates must be YYYY-MM-DD, storage type must be on the list, and unfreezing clears the thaw date.
- MCP `bag`: create accepts `frozenDate`/`storageHint` and refuses `defrostDate`/`openedDate`; the dead `grinderBrand/Model/Burrs` keys are rejected (the equipment package owns them).
- The app and web Beans editors share one C++ definition of: storage options, lifecycle line, card actions, restock, coffee-only fields, the bean-detail merge, and the edit diff. The web editor now writes only changed fields. Web cards match the app (Delete on unused finished bags, Find in Bean Base not on tea, no confirmations).

## Impact

- `src/history/coffeebagstorage.{h,cpp}`: `CoffeeBag::openedDateForShot`, the single definition of the rule.
- `src/controllers/maincontroller.cpp`: the post-shot bag stamp.
- `qml/components/BagCard.qml`, `src/network/shotserver_bags.cpp`: button removed.
- `src/mcp/mcpresources.cpp`: shared freshness block.
- `qml/components/DatePickerDialog.qml`, `ChangeBeansDialog.qml`, `DateUtils.js`: no-future pickers and fields.
- `src/ai/dialing_helpers.h`, `dialing_blocks.{h,cpp}`, `shotsummarizer.cpp`: freshness rules, reference date, hoisting.
- `src/network/beanbase_blob.h`, `beanbaseclient.{h,cpp}`: shared editor-details merge.
- `qml/components/BagLifecycleLabels.qml` (new), `BeanSummary.qml`: shared lifecycle wording.
- `src/mcp/mcptools_write.cpp`, `mcpserver.h`, `resources/ai/tools/bag.md`: MCP bag tool and surface version 1.17.0.
- `tests/tst_coffeebags.cpp`, `tst_dialing_helpers.cpp`, `tst_aimanager.cpp`: rule cases.
- Wiki manual: Beans page.
