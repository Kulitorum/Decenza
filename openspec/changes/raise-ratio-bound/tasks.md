## 1. Implementation

- [x] 1.1 `YieldSpec::kMinRatio` / `kMaxRatio` = 0.5 / 100; `clampRatio` uses them.
- [x] 1.2 `Settings.brew.minRatio` / `maxRatio` (CONSTANT); the wizard, bag editor and preset dialog clamp with them.
- [x] 1.3 Ratio preset buttons step by 1 from 1:10.
- [x] 1.4 MCP property descriptions and `resources/ai/tools/bag.md` state 0.5-100.

## 2. Tests

- [x] 2.1 `tst_coffeebags`: a 1:16 bag ratio is stored as 16; 1:150 clamps to 100.
- [x] 2.2 `tst_profilemanager`: a 1:16 session anchor on an 18 g dose resolves to 288 g.
- [x] 2.3 Break the bound and watch both go red; full suite via Qt Creator.
- [x] 2.4 Live check over the `decenza` MCP on V60 15g in, 250g out with an 18 g dose: `yieldRatio 16` stored as 16 (288 g); `yieldRatio 150` clamped to 100 with a note.

## 3. Docs

- [x] 3.1 Wiki manual: ratios run to 1:100 (local wiki edit, pushed after merge).
