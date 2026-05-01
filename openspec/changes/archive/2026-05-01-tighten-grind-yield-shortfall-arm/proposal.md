# Change: Decouple grind yield-shortfall arm from the 15s pressurized-duration gate

## Why

The 500-shot audit (#963) found that the moderate-yield arm of the grind
detector was hidden by a shared duration gate. Today's
`analyzeFlowVsGoal` runs both the flow-choked arm and the yield-shortfall
arm under one shared precondition:

```cpp
if (flowSamples >= 5 && pressurizedDuration >= CHOKED_DURATION_MIN_SEC) {
    // ... both arms inside
}
```

That 15s pressurized gate makes sense for the **flow-choked** arm — it
needs sustained pressure to compute a meaningful mean flow. It does NOT
make sense for the **yield-shortfall** arm, which is yield-ratio-based
and doesn't read pressurized flow at all.

Concrete miss: shot 745 (Adaptive v2): 35s pour, **yield 23.1g of 36g
target = 0.64 ratio**, well below the 0.85 moderate-choke threshold.
Pressurized duration was 8.76s, under the 15s gate — so the entire arm
was silenced. Verdict reads "Clean shot. Puck held well." The user
makes no change, repeats the underextraction next shot.

Simulation against the 17 currently-silent under-target shots in the
audit:

| Approach | New fires |
|---|---|
| Drop duration gate from yield arm; keep ratio at 0.85 | 9 (includes borderline Adaptive v2 at 71-76%, likely FPs) |
| Drop duration gate from yield arm; **tighten ratio to 0.70** | **5** (745, 752, 753, 754, 735 — all genuinely choked-shaped) |
| Drop duration gate from yield arm; tighten ratio to 0.65 | 5 (same) |
| Drop duration gate from yield arm; tighten ratio to 0.50 | 3 |

The 0.70 threshold is the empirical sweet spot: catches the 5 genuinely
choked shots, excludes the 4 borderline Adaptive v2 shots (71-76%
yield, high flow rates) where flagging "puck choked" would likely be
a false positive — those profiles may simply deliver less than target
by design.

The flow-choked arm keeps its existing 15s gate unchanged. The
verified-clean signal also continues to require both gates (we can
only strongly verify when the flow arm could speak).

## What Changes

- **Yield-shortfall arm decouples from the 15s pressurized-duration
  gate.** It runs whenever `flowSamples >= 5` (i.e. the puck saw
  meaningful pressure even briefly) AND the standard target/yield
  preconditions hold. Eliminates shot 745's silent miss and the
  similar Q3 short-pour-low-yield population.
- **`CHOKED_YIELD_RATIO_MAX` tightens from 0.85 to 0.70.** Empirical:
  the 0.85 threshold over-flagged Adaptive v2 fast-pour shots that
  delivered 71-76% of target by design. 0.70 catches the genuine
  choke shapes without those false positives.
- **`hasData=true` semantics expand.** Previously set only when the
  shared 15s+5-sample gate passed. Now also set when the yield arm
  fires standalone. The verified-clean path still requires the strong
  flow-arm gates.
- **Flow-choked arm unchanged.** Still requires `flowSamples >= 5`,
  `pressurizedDuration >= 15s`, mean pressurized flow `< 0.5 mL/s`.
- **Badge projection unchanged.** `grindIssueDetected` still requires
  `chokedPuck || yieldOvershoot || |delta| > FLOW_DEVIATION_THRESHOLD`.
  The relaxed yield-arm trigger projects through cleanly because
  yield-shortfall already sets `chokedPuck = true`.
- **No new MCP fields.** Coverage signal continues to be `"verified"`
  whenever `hasData=true`. (Future work in #964 to expose the
  individual gate values.)

## Impact

- Affected specs: `shot-analysis-pipeline` (modify the existing grind
  detector requirement to document the split gate semantics).
- Affected code:
  - `src/ai/shotanalysis.h` — change `CHOKED_YIELD_RATIO_MAX` from
    0.85 to 0.70.
  - `src/ai/shotanalysis.cpp` — restructure `analyzeFlowVsGoal`'s
    outer gate into a flow-arm gate and a yield-arm gate.
  - `tests/tst_shotanalysis.cpp` — 3 new tests (yield arm fires
    without 15s pressurized; doesn't fire on 0.75 ratio; flow arm
    still requires 15s).
  - `tests/data/shots/manifest.json` + fixture — verify
    `80s_choked_moderate.json` still passes (ratio is near 0.7
    boundary).
  - `docs/SHOT_REVIEW.md` §2.2 — update the moderate-yield threshold
    and rationale.
- No QML changes. No DB migration. No badge UI changes.

Estimated population impact going forward: ~1% of espresso shots
(based on 5/500 historical). Every future shot of any espresso
profile that lands in the 50-70% yield ratio range with brief
pressurized window now gets the actionable "grind too fine, coarsen"
signal instead of a misleading "Clean shot."
