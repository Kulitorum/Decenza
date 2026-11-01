# Proposal

## Why

An Americano or long black includes added water, but the saved shot currently describes its espresso and a planned water-vessel snapshot. Reviewing or comparing those drinks needs the water actually dispensed and the configured temperature used for that pour.

## What Changes

- Capture the added-water result for each sequenced Americano and long black, in either step order: net measured grams with a physical scale, or flow-derived millilitres explicitly identified as an estimate.
- Snapshot the configured water temperature when the water pour starts. Present it as **Water temp (set)**, without claiming a measured outlet or drink temperature.
- Persist the result against the correct espresso shot even when water finishes after that shot has saved. Preserve skipped, interrupted and unavailable results without substituting the configured amount.
- Show a compact **Added water** result in shot review/history detail and include water amount and set temperature in shot comparison, using the same native/Wasm pages and canonical comparison data.
- Keep espresso yield, brew ratio, TDS/EY and extraction analysis based on the espresso component. Keep the existing water-vessel specification for recipe promotion; do not replace it with the measured result.
- Carry the new result through backup/device transfer and existing shot-detail/comparison data surfaces; document it briefly in the wiki manual.

## Capabilities

### New Capabilities

- `shot-water-results`: Capture, associate and persist added-water outcomes for espresso-based drink sequences, with explicit measurement source and missing/interrupted semantics.

### Modified Capabilities

- `shot-recipe-card`: Present the shot's added-water outcome and set temperature without presenting the planned vessel amount as water dispensed.
- `shot-comparison`: Include dispensed-water results and configured water temperature with correct units, missing values and base-relative differences.

## Impact

- Host capture: `MachineState`, `DrinkSequenceController`, and MainController's operation-correlated shot-save path. Builds on the implemented `add-one-tap-americano-long-black` sequence; that change still has real-device acceptance work open.
- Storage: a nullable shot-only water-result field, forward migration, save/load/projection, import/transfer, and a narrow asynchronous result update for water-after-espresso.
- Presentation: `PostShotReviewPage`, canonical `ShotComparison`/comparison wording, and shared review/history/comparison service snapshots. Existing MCP shot detail/compare outputs receive additive result data; no new tool is required.
- No new external dependencies. Configured water amount remains recipe intent, separate from the result; measured water temperature, external-kettle entry and unrelated standalone water pours are outside this change.
