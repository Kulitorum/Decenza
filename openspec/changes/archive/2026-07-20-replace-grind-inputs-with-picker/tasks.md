## 1. Icons

- [x] 1.1 Add `resources/icons/keyboard.svg` — `hide-keyboard.svg` with its dismiss-chevron path removed (keeps stroke weight and family; no new artwork). (ViewBox shifted to `0 -3 24 24` to recentre the body after the chevron's removal; rendered and checked.)
- [x] 1.2 Add `resources/icons/picker-wheel.svg` — stacked rows with the middle row emphasised, in the same 2px line-art style so `redraw-icon-set` restyles both with the rest. (Same 16×16 grammar as `list.svg`; outer rows inset at 0.45 opacity; rendered and checked.)
- [x] 1.3 Register both in the `.qrc` / resource list and confirm they tint via `Theme.iconColor` in light and dark. (Registered in `resources.qrc`; both use the same white-fill mask pattern as every sibling icon — final in-app tint check rides with 4.9.)

## 2. Negative-capable row generation (design D2)

- [x] 2.1 Remove the `if (stepped < 0.0) return QString();` guard in `SettingsDye::stepGrinderSetting` (`src/core/settings_dye.cpp:423`) for grinders whose **registry notation** is `NumericWithSuffix`; keep it when `entry->notation == Compound`. The exemption keys on the grinder's notation, NOT the current value's written form — `"2.5"` on a Mignon still skips negatives (a negative linear position is meaningless on click-indexed hardware however it is written).
- [x] 2.2 Remove the matching `if (v < 0) return ""` in the QML pure-numeric branch (`GrindQuickSelectItem.qml:158`). Both parsers already accept a leading `-`; only the steppers refused.
- [x] 2.3 Add C++ tests for `stepGrinderSetting`: a plain-numeric grinder at `0.25` step `0.25` yields candidates through `-1`; the same grinder at `8` yields `6.75`–`9.25` with no negatives; a compound grinder still skips a negative position, including with a plain-numeric current value. **Update the existing assertions that pin the old guard** — `tests/tst_settings.cpp:1724` (DF83V below-floor `== QString()`) and the 1722–27 boundary-comment block. Read `docs/CLAUDE_MD/TESTING.md` first — no WARN lines.
- [x] 2.4 Verify: on a Niche Zero at a low setting the wheel keeps spinning finer past `0` instead of stopping. (Verified by the user on the simulator: continuous spin 9 → −1 in one picker session; first-open animation bug found and fixed along the way — Tumbler contentItem is a private wrapper, real ListView resolved by duck-typing, snap-positioned with animation disabled.)

## 3. Extract the shared layers (design D5)

- [x] 3.1 Extract row generation and stepping from `GrindQuickSelectItem.qml` (`stepGrind`, `_stepDecimals`, `_fmtNum`, `grindRows`, `_rpmRows`, `_observedFallback`, the history-derived steps, the `_distinctCacheVersion` guard) into a non-visual shared component.
- [x] 3.2 Create `GrindField.qml` with `presentation: "pill" | "field"` — both **tap-to-open** (design D5: `field` displays the value and opens the picker; it is NOT an inline text input) — host-supplied value, writer, and grinder identity, and `grindCommitted` / `rpmCommitted` signals (an empty grind commit is the explicit clear, design D9) — following `RatioPresetDialog`'s `pickOnly` + `ratioPicked` decoupling precedent. It SHALL NOT read or write `Settings.dye` itself.
- [x] 3.3 Add both new QML files to the `qt_add_qml_module` list in `CMakeLists.txt` (without this they are not found at runtime).
- [x] 3.4 Re-point `GrindQuickSelectItem.qml` at the shared components, injecting the `Settings.dye` read/write it does today. Behaviour-preserving.
- [x] 3.5 Verify the brew-bar pill is unchanged: appearance, zone colours, background-image glass scrim, ratio-width parity, and the `dyeGrinderSetting` / `dyeGrinderRpm` write-through. (Verified in use over many sessions — pill renders identically over the background image and writes through; the ONE deliberate behaviour change is that it now applies explicit clears, see 6c.10.)

## 4. Picker keyboard mode (design D3, D4)

- [x] 4.1 Add the header toggle to `GrindPickerDialog.qml`: a single button switching both wheels to two fields and back, icon swapping `keyboard.svg` ⇄ `picker-wheel.svg` to indicate destination. No hidden gesture.
- [x] 4.2 Add the two text inputs — grind accepting free text, RPM with `Qt.ImhDigitsOnly`. Typing SHALL apply nothing; Done remains the only commit path.
- [x] 4.3 Implement re-seed: switching back to the wheels regenerates candidates centred on the typed value rather than snapping to the previous lattice. Do NOT copy `ValueInput.qml:706`'s `Math.round(parsed / roundTo) * roundTo` — it would silently destroy an off-grid value.
- [x] 4.4 Implement the auto text-mode fallback as one rule with three triggers: no rows generated, unparseable current value, or a value the wheel cannot reach.
- [x] 4.5 Remove the `grind.picker.empty` string and its translation key ("Set a grind value in Brew Settings first…") — retired, not repurposed.
- [x] 4.6 Add a `HideKeyboardButton` inside the dialog (the global one in `main.qml` sits behind the modal overlay) and handle keyboard avoidance for the centred modal so the focused field and Cancel/Done stay visible.
- [x] 4.7 Accessibility: toggle gets `Accessible.role: Button`, a name reflecting its destination, and `onPressAction`; each field gets `Accessible.EditableText` and a name identifying which half it edits. Fix any pre-existing violations in the file.
- [x] 4.8 Add i18n keys with English fallbacks for the toggle names (both directions) and the two field labels.
- [x] 4.9 Verify the toggle: visible and reversible, typed value survives Done. (Verified on macOS desktop — toggle swaps both ways, typed grind `12` committed, cleared RPM committed.) **Not yet exercised on a touch device**: soft-keyboard avoidance, the in-dialog HideKeyboardButton, and Done reachability with the keyboard up are desktop-untestable — carry to the first Android/iOS run.

## 5. QML site adoption (design D5, D7)

- [x] 5.1 Build the `field` presentation against `ChangeBeansDialog.qml`'s `FieldRow` first — the most constrained host (shared label column, 20px margins). If it fights the other three, add a second variant rather than distorting the hosts.
- [x] 5.2 `ChangeBeansDialog.qml:1797/1812` → shared control, passing the **bag's linked equipment** as the grinder context (`fEquipmentBrand`/`fEquipmentModel`, already used by the gate at line ~73). Normalise `fRpm` from `string ""` to `int 0` for unset. The inline fields are replaced by the tap-to-open control, so the pre-existing `KeyboardAwareContainer` gap (line ~813 lists `grindSettingInput` only; the RPM field has no id) **dissolves** — remove `grindSettingInput` from the `textFields` list rather than adding the RPM field to it. Empty grind commit clears (bag grind is legitimately unsettable).
- [x] 5.3 `RecipeWizardPage.qml:3117/3124` → shared control, passing the **recipe's selected package** as the grinder context (`_selectedPackage`, chosen in the equipment window which deliberately precedes this one). Reconcile the gate's *mechanism* only — `grinderRpmCapable(pkg.grinderBrand, pkg.grinderModel)` in place of the `!!pkg.rpmCapable` flag — so a stale or uncatalogued flag cannot disagree with the shared function. Do NOT switch it to the active grinder. Normalise the raw `.text` read to `int 0` unset; preserve the fixed `Layout.preferredWidth: 110` if the layout still needs it.
- [x] 5.4 `PostShotReviewPage.qml:1595/1626` → shared control, passing the **shot's** grinder as context (`editGrinderBrand`/`editGrinderModel`, seeded from `editShotData` at line ~280 — already correct today and MUST survive the refactor). Re-wire `autosave` to fire on the picker's commit — a tap-to-open control has no blur; Done is the commit event. Step, candidates, notation and the RPM gate all resolve against the shot's grinder, not the active one.
- [x] 5.5 `BrewDialog.qml:1410/1430` → shared control with the **active** grinder as context (correct here — it edits the live dial-in), preserving the dual write-through (active bag + active package) required by `brew-settings-equipment`.
- [x] 5.6 Verify clearing behaves per spec — every host applies an explicit clear, including the brew-bar pill (6c.10 corrected the original "pill ignores it" wording; verified working). Candidates present and unset uniform across the sites exercised (pill, Brew Settings, bag form). **Not separately re-checked**: a recipe created with a cleared grind adopting the bag's dial on create.
- [ ] 5.7 **(OPEN — carried, not verified)** Verify grinder context specifically, with two different grinders configured — switch the active grinder, then confirm that reviewing an old shot steps/formats by the SHOT's grinder and editing a recipe steps by the RECIPE's package. A notation mismatch is the sharpest failure: a compound grinder's `"3+2"` stepped as plain numeric is wrong output, not just a wrong increment.

## 6. ShotServer (design D6)

- [x] 6.1 Add a shared `<input list>` + `<datalist>` helper to the common ShotServer style/JS layer — `<datalist>` does not exist anywhere in ShotServer today, so this is a new primitive and must not be inlined per page. It must handle both field names: `grinderSetting` (shots, bags) and `grindPinned` (recipes).
- [x] 6.2 ~~Extend the JSON payloads~~ Implemented as a dedicated `GET /api/grind-candidates?brand=&model=&current=&rpm=` endpoint instead of payload embedding — implementation revealed that the bag/recipe dialogs let the user switch equipment mid-edit, so embedded per-record candidates would go stale; the endpoint recomputes on demand and on equipment change. Same server-side C++ machinery (grind and, where the grinder is capable, RPM), produced by the existing C++ machinery (`grindStepForGrinder` + `stepGrinderSetting`) and resolved against **each record's own grinder** — the shot's, the bag's, the recipe's selected package (design D7 applies to the web too). No JS re-implementation of the stepping.
- [x] 6.3 Render the helper in the ShotServer theme and check the dropdown. (Checked on `/beans`; user confirmed "better" after the empty-candidate and RPM-gating fixes.)
- [x] 6.4 `src/network/shotserver_shots.cpp:1898-99` → helper, grind and RPM.
- [x] 6.5 `src/network/shotserver_bags.cpp:581-82` → helper (re-attaches on `fEquipment` change so candidates follow the selected package).
- [x] 6.6 `src/network/shotserver_recipes.cpp:606-07` → helper; a blank grind must still be omitted on create (blank-adopts-bag, line ~1023).
- [x] 6.7 Verify candidates offered and the RPM field gated by the record's grinder. (Verified on `/beans`: Niche Zero hides RPM, switching packages re-scopes live. Endpoint responses verified directly by curl for the Niche/Mignon/unparseable cases.) **Not separately checked**: a shot from a non-active grinder on the shot page.

## 6b. Equipment defaulting (user-directed follow-up: never start empty)

- [x] 6b.1 App `ChangeBeansDialog`: creation paths default the equipment package to the ACTIVE one (`resetForm` + `prefillFromBag` fallback); re-buy keeps the SOURCE bag's package. Kills the unknown-grinder RPM fallback on manual entry.
- [x] 6b.2 Web `/beans`: New Coffee defaults the equipment select to the active package (existing bags keep their own link).
- [x] 6b.3 App `RecipeWizardPage`: creation walk preselects the active package; exactly ONE in-inventory package → filled in and the equipment window SKIPPED (straight to numbers; still reachable via Back). Edit/clone and summary-card jumps unaffected.
- [x] 6b.4 Web `/recipes`: create links the active package (`equipmentId` in the create payload) and scopes the new-recipe grind candidates to it; updates leave the link untouched.
- [x] 6b.5 Delta specs added: `change-beans-dialog` (never-empty default, re-buy keeps source) and `recipe-wizard` (preselect active, single-package skip, web create link).
- [x] 6b.6 Verify the equipment default. (Web new coffee verified showing the active Niche with grind-only.) **Not verifiable on this setup**: the single-package wizard skip needs a one-package inventory; **not separately checked**: web new-recipe active-package linkage.

## 6c. Automated-review findings (task 8.6 fleet: code / comments / tests / silent-failure)

- [x] 6c.1 **Fabricated RPM on records (HIGH, found independently by two agents).** Wheel-mode Done emitted `rpmPicked` unconditionally, and `_centerWheels` parks an UNSET RPM on the synthetic 1000 anchor — so opening the picker to nudge grind on a past shot wrote a `1000` RPM that never happened into the shot record (then into shot analysis + AI dialing context). Harmless on `main` (the pill wrote the live dial-in only); this change propagated it to record hosts. Gated on `currentRpm > 0 || _rpmTouched` (`onMoved` / RPM text edit). The grind half needs no gate — its parked value is always real (the current value, or the user's own observed history).
- [x] 6c.2 **BrewDialog swallowed deliberate clears (regression).** The inline RPM field it replaced could be emptied to 0; the new host ignored empty commits. It edits PENDING dialog state reviewed before OK, so clears now apply — the brew-bar pill is the single documented exception. Restores the pre-change capability and makes the spec's clear rule true as written.
- [x] 6c.3 **`/api/grind-candidates` error path returned HTTP 200.** The helper's `r.ok` check passed and its empty `rpm` array was read as the NOT-capable verdict — hiding the RPM field on a capable grinder. Now 503, landing in the `.catch` where the documented fail-open applies.
- [x] 6c.4 **Web equipment-list quiet catch drove persistence.** `/recipes` create used a possibly-unresolved list for the active-package default (silently unlinked recipe); create now awaits it with a one-shot retry. `/beans` could silently UNLINK an existing bag's package when the list failed to load (select fell back to "None", save sent 0) — now preserved via a placeholder option. Pre-existing for the edit path; fixed per the fix-what-you-touch rule.
- [x] 6c.5 **`_view()` null degraded mutely** into the exact multi-minute self-spinning animation it exists to prevent. Now warns once per snap so a future Qt internals change is greppable.
- [x] 6c.6 **Extracted `GrindCandidates` (`src/network/grindcandidates.h`)** so the endpoint's hand-duplicated click-indexed rule is testable — the `tst_exifdate` precedent, whose CMake comment documents this exact file rotting once before. Six behavioural tests added (negatives on a stepless collar, positive anchor clean, click-indexed skip incl. plain-numeric form, history fallback + cap, step→decimals incl. float-dirty, RPM capability gate + no non-positive rows). The extraction also fixed a real parity bug the comment review found: the web kept a `0` RPM row (the unset sentinel) that the app excludes.
- [x] 6c.7 **Comment rot from mid-flight reversals**: superseded ±5-window prose in the tests and `GrindRowSource`, "stepping algorithm unchanged" (the semantics DID change), "the one surface using the active grinder" (there are two — Brew Settings), the clear contract framed as pill-vs-field (it is live-dial-in-vs-record), and "clearing restores the bag default" (it re-arms; the refill happens on the next bag selection). Dead `grindRows`/`rpmRows` reactive properties (superseded by the snapshot path) and the writer-less `finerHint` removed.
- [x] 6c.8 Accessibility: both Tumblers now expose `Accessible.description` with the selected value — a screen-reader user spinning the wheel previously heard only the column name. Pre-existing on `main`; fixed per the touched-files rule.
- [x] 6c.10 **Clears were still swallowed by the brew-bar pill (found in use, not by review).** The pill's `if (rpm > 0)` / `if (v.length > 0)` guards dropped a deliberate clear. The exemption was justified as "the live dial-in has never been clearable" — but that described a MISSING AFFORDANCE (the old pill had no text entry, so a clear was not expressible), not a decision, and the review-era fix only narrowed its scope instead of removing it. Every host now applies clears; the record-vs-live host taxonomy is deleted. Verified working in the app. Diagnosed via `mcp__decenza__debug_get_log` with temporary per-hop logging, which showed the picker emitting `rpmPicked('') -> 0` correctly and the host silently discarding it.
- [x] 6c.9 Re-verify in the app after the fixes: nudge grind on a shot with NO rpm → no RPM written; clear RPM in Brew Settings → it clears; `/beans` edit with the equipment list unavailable → link preserved.

## 7. Documentation

- [x] 7.1 Update the wiki manual (`Kulitorum/Decenza.wiki.git`) for the new keyboard affordance in the grind picker and the grind control's wider availability. Hold the push per the wiki-timing convention unless told otherwise.
- [x] 7.2 Note the deliberate app/web divergence (wheel vs `<datalist>`) where the ShotServer parity rule is documented, so it does not read as drift.

## 8. Validation

- [x] 8.1 Build via Qt Creator MCP (`mcp__qtcreator__build`) — 0 errors, 0 warnings.
- [x] 8.2 Run the full suite via Qt Creator MCP (`run_tests`); there is no PR CI gate, so the local run is the gate.
- [x] 8.3 Confirm no new QML TypeErrors or warnings in the running app log. (Confirmed via `mcp__decenza__debug_get_log` session 49: clean load, no QML warnings on the touched surfaces.)
- [x] 8.4 Run `openspec validate replace-grind-inputs-with-picker --strict` and resolve any issues. (Valid; re-validated after every artifact edit.)
- [x] 8.5 Open a PR (never push to `main`).
- [x] 8.6 Run the automated `/pr-review-toolkit:review-pr` on the PR and address findings. (4 agents; all findings fixed — see group 6c.)
- [x] 8.7 Archive + spec-sync as the final commit on the feature PR (not a separate archive-only PR).
