# Tasks

## 1. Upgrade and active setup

- [x] 1.1 Remove bag equipment/grind/RPM columns in one database migration that records notice eligibility, never writes settings or creates equipment, and on failure rolls back, logs the step and retries next launch while the app runs; test eligibility rows, fresh installs, rollback and retry.
- [x] 1.2 Present the pending notice once when the UI is ready; acknowledging deletes the pending row and always closes the dialog; verify acknowledgment and an interrupted startup.

## 2. Bag and equipment model

- [x] 2.1 Remove bag equipment/grind/RPM fields and columns, preserve all other bag and shot data, and test upgrades, new databases, restores and retries.
- [x] 2.2 Remove bag writes and reads from selection, live dial and equipment changes, shot saves, restock and shot-review carry-forward while preserving dose/yield behavior; verify active bag changes, recipe edits, equipment switching and accepted review corrections.
- [x] 2.3 Update equipment delete, merge, identity fork/enrichment and reference handling for the removed bag column; verify operations on packages referenced by shots or recipes and unreferenced packages.

## 3. Recipes and history

- [x] 3.1 Remove bag equipment fallback during recipe activation and bag grind defaults across creation surfaces; test equipment-less recipes, recipe-owned grind, explicit blank, finished bags and relinking.
- [x] 3.2 Implement latest same-bean non-empty shot grind lookup with canonical-ID conflict handling, conservative name fallback, deterministic ties, same-shot RPM and available context; test linked/unlinked, ambiguous and missing identities.
- [x] 3.3 Make the wizard history query the sole grind default and prevent delayed replies or bean/profile callbacks from overwriting user edits; test both callback orders, explicit clears, bean changes and missing history.
- [x] 3.4 Recover inherited legacy recipe grind from source backup data before discarding old bag fields, including pre-bag-ID identity resolution; test old/current schemas, missing matches, merge deduplication and explicitly empty current recipes.

## 4. User surfaces and compatibility

- [x] 4.1 Remove bag equipment/grind controls and restock defaults in native and shared remote UI; verify existing and new bags, restock/restore and no-recipe equipment switching.
- [x] 4.2 Update web and MCP bag/recipe surfaces, descriptions, validation and older-client responses; verify obsolete non-empty bag fields are refused, empty compatibility fields are handled, and explicit recipe grind survives.
- [x] 4.3 Update the user manual and relevant internal documentation for bag selection, recipe defaults, the upgrade notice and manual equipment switching; verify the published manual wording matches the implemented behavior.

## 5. Integration validation

- [x] 5.1 Run the relevant storage, recipe, migration, UI/service and remote API tests through Qt Creator; verify old shot snapshots and other bag fields remain unchanged and mixed-version transfer succeeds.
- [x] 5.2 Build through Qt Creator and verify the QML diagnostics gate and cross-surface integration pass; document any platform or downgrade limitations.
