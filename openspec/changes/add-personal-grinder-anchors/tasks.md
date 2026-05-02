# Tasks: add-personal-grinder-anchors

## Implementation

- [x] **1. Parse UGS into `ProfileKnowledge` struct (`src/ai/shotsummarizer.h` + `shotsummarizer.cpp`)**
  - Add `double ugs = std::numeric_limits<double>::quiet_NaN()` and `bool ugsInferred = false` to `ProfileKnowledge`
  - In `commitSection()`, detect `UGS:` lines: strip leading `~` (set `ugsInferred = true`), strip `(…)` annotations, parse remaining token as `double`; leave `NaN` on parse failure
  - Add `static double ugsForKbId(const QString& kbId)` — looks up the parsed UGS for a given KB entry key; returns `NaN` when not found or entry has no UGS

- [x] **2. Add `buildGrinderCalibrationBlock` to `src/ai/dialing_blocks.h`**
  - Declaration: `QJsonObject buildGrinderCalibrationBlock(QSqlDatabase& db, const QString& grinderModel, const QString& grinderBurrs, const QString& beverageType, qint64 resolvedShotId)`
  - Add threading annotation (background-thread / DB-owning, same tier as `buildGrinderContextBlock`)

- [x] **3. Implement `buildGrinderCalibrationBlock` in `src/ai/dialing_blocks.cpp`**
  - Return empty object when `grinderModel` is empty or `beverageType` is filter/pourover
  - SQL: query all-time shots (no time window), same `grinder_model` + `grinder_burrs`, espresso only; select `profile_name`, `grinder_setting`
  - Filter to numeric settings; group by profile name; compute median, min, max, sample count per profile
  - For each profile with a median, look up canonical UGS via `ShotSummarizer::ugsForKbId` (match by profile name normalisation); skip inferred entries for anchor qualification
  - Require ≥ 2 profiles with canonical (non-NaN, non-inferred) UGS — return empty if not met
  - Select fine anchor (lowest canonical UGS) and coarse anchor (highest canonical UGS)
  - Compute `conversionKey` = (coarseMedian − fineMedian) / (coarseUGS − fineUGS); round to 2 dp
  - Iterate every `ProfileKnowledge` entry with a non-NaN UGS; compute RGS = fineMedian + (ugs − fineUGS) × conversionKey; format as string (trailing-zero-stripped, 1 dp)
  - Assign source: `"history"` when profile has a history median; `"derived"` when within `[fineUGS, coarseUGS]`; `"extrapolated"` otherwise or when `ugsInferred`
  - Sort `profiles` array by UGS ascending
  - Deduplicate by profile name (each alias maps to the same `ProfileKnowledge`; use `pk.name` as canonical)

- [x] **4. Wire into `dialing_get_context` (`src/mcp/mcptools_dialing.cpp`)**
  - In the background-thread DB closure (alongside `buildGrinderContextBlock`), call `buildGrinderCalibrationBlock` and store in `DialingDbResult`
  - Add `grinderCalibration QJsonObject` field to `DialingDbResult` struct
  - In the main-thread continuation, emit `result["grinderCalibration"]` when non-empty

- [x] **5. Wire into in-app advisor (`src/ai/aimanager.cpp`)**
  - In `analyzeShotWithMetadata`'s background-thread closure, call `buildGrinderCalibrationBlock` and capture result
  - Merge into the user-prompt JSON envelope on the main-thread continuation

- [x] **6. Update `resources/ai/claude_agent.md`**
  - Add guidance: when recommending a profile switch or grind change, use `grinderCalibration.profiles[].rgs` as the concrete number; qualify `"extrapolated"` entries ("this is a starting estimate — adjust based on how the shot pulls"); never derive grinder numbers from UGS arithmetic directly

- [x] **7. Update `docs/CLAUDE_MD/MCP_SERVER.md`**
  - Add `grinderCalibration` to the `dialing_get_context` tool description
