# Proposal

## Why

The app's UGS positions and guide link have drifted from the UGS chart at https://videoblurb.com/UGS/:
- Blooming Allongé sits at UGS −0.75 on the chart; the app has −0.5.
- Extractamundo Dos! sits at UGS 2.5 on the chart; the app has 1.5. A full UGS is one conversion key's worth of grind on every suggestion for that profile.
- The chart no longer lists the Blooming Allongé "hybrid" at UGS 7.
- The guide button opens `https://videoblurb.com/UGS/ugsinfo.html`, which now returns 404; the guide and calculator live at `https://videoblurb.com/UGS/`.

## What Changes

- `resources/ai/profile_knowledge.json`: Blooming Allongé −0.5 → −0.75 and Extractamundo Dos 1.5 → 2.5, each with the chart's own note.
- `docs/PROFILE_KNOWLEDGE_BASE.md`: the canonical UGS table matches the chart's 15 rows (both values, the notes, the hybrid row removed, the count 16 → 15), along with the two profile sections and the grind-pattern sentence.
- The calibration section's UGS guide button opens `https://videoblurb.com/UGS/`.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `grinder-ugs-calibration`: the UGS guide requirement names the guide's current address.

## Impact

- Grind suggestions and AI grind guidance for Blooming Allongé and Extractamundo Dos! move to the chart's positions.
- `src/ai/dialing_blocks.h` (`kUgsGuideUrl`). No schema, setting or UI change.
