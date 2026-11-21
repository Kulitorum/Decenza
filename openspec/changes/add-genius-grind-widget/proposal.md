# Proposal

## Why

Someone who moves a bag between profiles (say from Cremina to a Rao Allongé) has to work out the new grind by hand, from the UGS chart. The app already calculates that number from the grinder calibration, but it only shows it inside the recipe wizard. Mark Palmos, the UGS chart's author, built a "Genius Grind" home-screen widget for it in his fork (Kulitorum/Decenza#2211). This change brings that widget into Decenza as an opt-in widget, so his testers can try it on the beta without installing a fork.

## What Changes

- **New opt-in `geniusGrind` layout widget**, from Mark's fork.
  - It shows the suggested grind for the loaded profile and the active bag on the active equipment.
  - In a bar it is a label over the number. In the centre zone it is an action tile, with the number where the icon sits.
  - Nobody sees it unless they add it from the layout editor.
- **Tapping it opens a popup** with:
  - the number, large;
  - one line naming the shot it starts from: profile, setting, date and time, and rating when there is one. The line opens that shot;
  - Bean and Profile choices that open in place and recalculate as a preview;
  - **Apply**, which activates the bag, loads the profile and sets the grind (and, on a grinder that takes an RPM, the starting shot's RPM);
  - **Cancel**, which changes nothing.
  - When there is no number, the popup says why instead.
- **The grind calibration's starting shot follows your ratings.**
  - Within the current bag, it is the highest-rated shot at 50 or above (so one rated 75 beats one rated 60), else the bag's latest good shot, as today. Badges and target weight are ignored for rated shots, and the most recent shot wins a tie.
  - **A shot rated under 50 counts as a bad shot** and never seeds a suggestion, even if it hit its weight. Unrated shots are unaffected.
  - It is chosen once, inside the calibration, so the widget, the recipe wizard and the AI advisor all start from the same shot and give the same number.
- **History alone can move a grind between profiles.** With 10 or more bag pairs, history publishes a conversion key even when its spread fails today's consistency check. No anchors are needed. On Mark's two older grinders, whose histories fail that check today and so get no number at all, the numbers it gives are no worse than keeping the bag's latest grind in a time-ordered replay, and better in a held-out-bag test (DF83V 2.40 vs 2.75 steps, Mignon 0.52 vs 0.56). On his current setup they are much better.
- **A profile gets the bag's own setting whenever there is one**, with or without a key: its best shot rated 50+, else its **latest** good shot (not the median), else the best-rated shot at the same UGS on another profile. An independent replay of Mark's database and Jeff's tablet found no case where the prototype gave a number and this rule set did not, apart from rated cleaning shots and bags with no coffee identity.
- **With no key, the move is still shown in UGS** ("8 UGS coarser than your Cremina at 21.5"), as the UGS calculator does.
- **The suggestion reports its starting shot** (id, time, rating, RPM), so the popup and wizard can name the shot and open it.
- **Removal of the unused `AIManager` grind methods and signals.** They duplicate the `EquipmentService` path this change reworks, and nothing calls them.

Not taken from the fork, each tested on Mark's database against both of his grinders:
- **Anchors replacing history.** Worse on both grinders.
- **The "Use as bean reference" button and its table.** A rating already says a shot was good. A reference kept current did no better than the latest shot, and one left stale did worse. The table also bypassed schema migrations.
- **Building on a shot at any rating, and the `*` marker.** Replaced by the rating rule; shots rated under 50 never seed a suggestion.
- **Metrics-based fallbacks** (closest to target weight). More cases worse than better on all three setups.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `layout-brew-widgets`: adds the `geniusGrind` widget and its popup.
- `grinder-ugs-calibration`: the calibration's starting shot follows ratings; a bag's own setting needs no key; history publishes a key with 10+ pairs; the suggestion covers every case the prototype did and reports its starting shot; the recipe wizard names it.
- `dialing-context-payload`: the per-coffee anchor is that starting shot, a profile's number comes from its rated shot (`source: "rated"`), its latest good shot (`history`, no longer the median) or a same-UGS shot, and 10+ mined pairs publish a key whatever their spread.

## Impact

- `src/ai/dialing_blocks.cpp` / `.h`:
  - `buildGrinderCalibrationBlock` chooses the starting shot and the per-profile rated setting;
  - `GrindCalibrationStatus` carries the starting shot;
  - `grindSuggestionFor` returns it.
- The AI payload changes: `coffeeAnchor` may be a rated shot and carries its rating, and a profile's `history` number is its latest good shot rather than the median. MCP output fields only, so the tool surface version is unchanged.
- `src/ai/aimanager.cpp` / `.h`, `src/main.cpp`: the dead grind methods, signals and their forwarding `connect` are deleted.
- New QML: `qml/components/GeniusGrindDialog.qml`, `qml/components/layout/items/GeniusGrindItem.qml`. These are registered in `CMakeLists.txt`, `src/webclient/CMakeLists.txt`, `LayoutItemDelegate.qml`, `widgetCatalogTable()` and the QML inventory in the wasm design doc.
- `qml/pages/RecipeWizardPage.qml`: reads the renamed start fields and names the starting shot in the popup's words.
- New shared JS: `GrindSuggestionText.js` (the sentence the popup and wizard share), `BagName.js` (a bag's short and full name, replacing the copies in the idle page, Beans widget, bag card, Recipes page, recipe card text and wizard), and `DateUtils.shotTimeLabel` (replacing two hand-written copies).
- `EquipmentService.requestGrindSuggestion` returns a request id, echoed in its reply.
- Uses the existing `BagService`, `ProfileService`, `HistoryService` and `RecipeContext`. No new service operation, schema change or setting.
- Tests in `tests/tst_dialing_blocks.cpp`; wiki manual section for the widget.
- Credit: the widget and popup are Mark's work; the squash commit carries his `Co-authored-by:` trailer.
