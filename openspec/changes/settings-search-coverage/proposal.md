## Why

Searching settings for "fahrenheit", "celsius" or "units" found nothing. Three cards had an `objectName`, which is what search scrolls to, but no entry in `SettingsSearchIndex.js`. The spec's requirement had also drifted from the code: it named a `tabIndex` field the index no longer has, and it said nothing about `externalRoute`.

## What Changes

- Add index entries for `machine/temperatureUnit`, `calibration/sensorCalibration` and `calibration/steamHealth`.
- Add `scripts/check_settings_search_index.py`, which runs per PR in `text-invariants.yml`. It fails when:
  - a card has no entry;
  - an entry's `cardId` matches no card;
  - an entry lacks routing or a `keywords` array;
  - an entry has an unhandled `externalRoute`;
  - an entry targets a debug-only tab;
  - a non-literal `objectName` appears;
  - the dialog filters a card that is no longer indexed.
- Restate the Settings Search Index requirement to match the code and the check.

## Capabilities

### Modified Capabilities
- `settings-ui`: the Settings Search Index requirement now states the card ↔ entry rule that CI enforces, along with the `tabId`/`cardId`/`externalRoute` fields.

## Impact

- **Code:** `qml/components/SettingsSearchIndex.js`. No behaviour change beyond the three new search results.
- **CI:** one new build-free step in `text-invariants.yml`.
