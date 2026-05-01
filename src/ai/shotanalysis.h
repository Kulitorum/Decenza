#pragma once

#include <QVector>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVariantList>

struct HistoryPhaseMarker;

// Shared shot quality analysis helpers.
// Used by ShotSummarizer (AI prompts), ShotHistoryStorage (save-time flags),
// and ShotAnalysisDialog.qml (user-facing summary via Q_INVOKABLE).
//
// Single source of truth for channeling detection, temperature stability,
// and shot summary generation. Threshold constants are defined here so
// tuning happens in one place.
class ShotAnalysis {
public:
    // --- Thresholds (tune here, applies everywhere) ---
    // Channeling detection via conductance derivative (dC/dt). This is the most
    // diagnostic puck integrity signal — it catches events invisible to flow or
    // pressure alone, and works regardless of frame mode (pressure/flow/advanced).
    static constexpr double CHANNELING_DC_ELEVATED = 3.0;         // |dC/dt| above this counts as elevated
    static constexpr double CHANNELING_DC_TRANSIENT_PEAK = 5.0;   // single-sample peak flagged as transient
    static constexpr int    CHANNELING_DC_SUSTAINED_COUNT = 10;   // >this many elevated samples = sustained
    static constexpr double CHANNELING_DC_POUR_SKIP_SEC = 2.0;    // skip first N seconds of pour (transition spike)
    static constexpr double CHANNELING_DC_POUR_SKIP_END_SEC = 1.5; // skip last N seconds of pour (natural tail acceleration)
    static constexpr double CHANNELING_MAX_AVG_FLOW = 3.0;        // mL/s — skip turbo/filter shots
    static constexpr double TEMP_UNSTABLE_THRESHOLD = 2.0;        // °C avg deviation from goal
    static constexpr double TEMP_STEPPING_RANGE = 5.0;            // °C goal range = intentional stepping
    static constexpr double TEMP_MIN_EXTRACTION_SEC = 1.0;        // min seconds of extraction past frame 1 to score temp

    // Mode-aware detection window tuning. A sample counts toward channeling
    // detection only inside a window where the active control goal (pressure
    // goal for pressure-mode phases, flow goal for flow-mode phases) is
    // roughly stationary AND the actual value has converged onto it.
    static constexpr double WINDOW_STATIONARY_REL = 0.15;   // goal change ≤ 15% across ±WINDOW_HALF_SEC
    static constexpr double WINDOW_CONVERGED_REL  = 0.15;   // |actual - goal| / goal ≤ 15%
    static constexpr double WINDOW_HALF_SEC       = 0.75;   // stationarity half-width
    static constexpr double WINDOW_MIN_GOAL       = 0.1;    // treat goal below this (or sentinel values) as "no goal"
    static constexpr double WINDOW_GAP_MERGE_SEC  = 0.3;    // merge window fragments separated by ≤ this gap

    // Puck-failure / truncated-pour detection (detectPourTruncated). Any
    // legitimate espresso extraction develops pressure above ~3 bar at some
    // point. If a shot's peak pressure stays below this floor, the puck
    // never built resistance — massive channel, missing puck, or grind
    // radically too coarse. Needed because conductance saturates at its
    // clamp when pressure stays low, making dC/dt flat and the channeling
    // detector silent; flow tracking preinfusion goal also masks the grind
    // direction signal. Looking at the pressure curve directly is the only
    // way to catch this failure mode.
    static constexpr double PRESSURE_FLOOR_BAR = 2.5;

    // --- Channeling detection ---

    // Severity levels reported by detectChannelingFromDerivative().
    enum class ChannelingSeverity {
        None,        // dC/dt stays below the transient threshold — clean puck
        Transient,   // a single spike > CHANNELING_DC_TRANSIENT_PEAK (self-healed channel)
        Sustained    // >CHANNELING_DC_SUSTAINED_COUNT samples above CHANNELING_DC_ELEVATED
    };

    // Contiguous time range to include in dC/dt channeling analysis.
    struct DetectionWindow {
        double start = 0;
        double end = 0;
    };

    // Build inclusion windows for mode-aware channeling detection. For each
    // phase in `phases`, consults the per-phase `isFlowMode` flag and walks
    // the active goal curve (flow goal for flow-mode phases, pressure goal
    // for pressure-mode phases). A time is included when:
    //   * The active goal is stationary (|goal(t±WINDOW_HALF_SEC) - goal(t)|
    //     / goal(t) ≤ WINDOW_STATIONARY_REL), AND
    //   * Actual is converged onto goal (|actual - goal| / goal ≤
    //     WINDOW_CONVERGED_REL), AND
    //   * Actual pressure is not rising rapidly in the next WINDOW_HALF_SEC
    //     — applied in both flow-mode (lever pressure-rise pattern) and
    //     pressure-mode (rise-and-hold final leg toward goal), since both
    //     produce the same conductance-drop signature that would otherwise
    //     read as channeling. Falling pressure is allowed.
    // Contiguous included times collapse into DetectionWindow spans. Short
    // gaps (≤ WINDOW_GAP_MERGE_SEC) are merged.
    //
    // Returns empty when the necessary goal data is absent — callers should
    // treat this as "cannot evaluate" rather than "detector silent": see
    // detectChannelingFromDerivative for behavior when windows are empty.
    static QVector<DetectionWindow> buildChannelingWindows(
        const QVector<QPointF>& pressure,
        const QVector<QPointF>& flow,
        const QVector<QPointF>& pressureGoal,
        const QVector<QPointF>& flowGoal,
        const QList<HistoryPhaseMarker>& phases,
        double pourStart, double pourEnd);

    // Analyze the conductance derivative (dC/dt) to classify puck integrity.
    // `conductanceDerivative` is expected to be the Gaussian-smoothed dC/dt
    // series produced by ShotDataModel::computeConductanceDerivative() or
    // ShotHistoryStorage::computeDerivedCurves(). Samples in the first
    // CHANNELING_DC_POUR_SKIP_SEC of pour are skipped to avoid the transition
    // spike that happens when pressure ramps and flow catches up.
    //
    // `windows`: mode-aware inclusion mask from buildChannelingWindows().
    // Only samples inside one of the windows count toward the elevated-
    // sample tally. An empty vector returns None immediately — it means
    // phase data was present but no stationary+converged range qualified
    // (all-ramp shot, nothing reliable to analyze). Callers that want
    // unrestricted legacy behavior must pass a single whole-pour window
    // explicitly; buildChannelingWindows emits that automatically when
    // `phases` is empty.
    //
    // outMaxSpikeTime (optional) receives the timestamp of the largest spike.
    static ChannelingSeverity detectChannelingFromDerivative(
        const QVector<QPointF>& conductanceDerivative,
        double pourStart, double pourEnd,
        const QVector<DetectionWindow>& windows = {},
        double* outMaxSpikeTime = nullptr);

    // Check if channeling analysis should be skipped for this shot.
    // Returns true for filter beverages and turbo shots (avg flow > 3 mL/s).
    static bool shouldSkipChannelingCheck(const QString& beverageType,
                                           const QVector<QPointF>& flowData,
                                           double pourStart, double pourEnd);

    // --- Temperature stability ---

    // Check if temperature goal range indicates intentional stepping (e.g. D-Flow 84→94°C).
    static bool hasIntentionalTempStepping(const QVector<QPointF>& tempGoalData);

    // Check if temperature goal range indicates intentional stepping within a time range.
    static bool hasIntentionalTempStepping(const QVector<QPointF>& tempGoalData,
                                            double startTime, double endTime);

    // Calculate average absolute deviation from goal in a time range.
    // Returns 0 if no data or no goal data.
    static double avgTempDeviation(const QVector<QPointF>& tempData,
                                    const QVector<QPointF>& tempGoalData,
                                    double startTime, double endTime);

    // Returns true when the shot reached actual extraction — at least one phase
    // marker with frameNumber >= 1 sits more than TEMP_MIN_EXTRACTION_SEC before
    // the shot's last sample. Used to gate the temperature-stability detector
    // so that aborted shots which died during preinfusion-start don't get
    // flagged for temp drift caused by the machine still preheating. Frame 1 is
    // sometimes recorded in firmware in the final ms of an aborted shot
    // (the marker exists but no samples land inside it), so checking marker
    // presence alone is not sufficient — we require the phase to have lasted.
    static bool reachedExtractionPhase(const QList<HistoryPhaseMarker>& phases,
                                        double shotDuration);

    // --- Helpers ---

    // Find Y value at a given time using linear search (data assumed sorted by X).
    static double findValueAtTime(const QVector<QPointF>& data, double time);

    // --- User-facing shot summary ---

    struct SummaryLine {
        QString text;
        QString type;  // "good", "caution", "warning", "observation", "verdict"
    };

    // Thresholds for flow-vs-goal grind detection
    static constexpr double FLOW_GOAL_MIN_AVG = 0.3;    // ml/s — ignore goal periods with very low target (preinfusion)
    static constexpr double FLOW_DEVIATION_THRESHOLD = 0.4;  // ml/s avg deviation to flag grind issue

    // Thresholds for the pressure-mode "choked puck" check inside the grind
    // detector. Runs in addition to the flow-vs-goal path, restricted to
    // pressure-mode portions of the pour. Two arms feed the same chokedPuck
    // flag:
    //   - Flow arm: mean pressurized flow < CHOKED_FLOW_MAX_MLPS — catches
    //     severe chokes like 80's Espresso shot 890 (1.1 g yield, ~0.3 ml/s
    //     mean during the pressure-mode tail). Requires flowSamples ≥ 5
    //     AND pressurizedDuration ≥ CHOKED_DURATION_MIN_SEC because mean
    //     flow is only meaningful when the puck sustained pressure.
    //   - Yield arm: yield/target < CHOKED_YIELD_RATIO_MAX — catches moderate
    //     chokes like shot 745 (Adaptive v2, 23 g of 36 g target = 0.64
    //     ratio, ~8.8 s pressurized window). Requires only flowSamples
    //     ≥ 5 (any pressurized samples seen) — does NOT require sustained
    //     pressurized duration because its diagnosis is yield-based and
    //     does not read mean pressurized flow. Decoupled from the flow
    //     arm's gate by the 500-shot audit (see #963 / openspec change
    //     tighten-grind-yield-shortfall-arm).
    static constexpr double CHOKED_PRESSURE_MIN_BAR = 4.0;
    static constexpr double CHOKED_FLOW_MAX_MLPS = 0.5;
    static constexpr double CHOKED_DURATION_MIN_SEC = 15.0;
    // Tightened from 0.85 by the 500-shot audit: 0.85 over-flagged
    // Adaptive v2 fast-pour profiles delivering 71-76% of target by
    // design. 0.70 is the empirical sweet spot — catches the genuinely
    // choked shapes (yields under ~70%) without false-positives on
    // profiles whose normal yield is intentionally below target.
    static constexpr double CHOKED_YIELD_RATIO_MAX = 0.70;

    // Yield-overshoot ("gusher") arm — the inverse of the moderate choked-puck
    // yield arm. Fires when yield/target exceeds this ratio: the puck offered
    // too little resistance, water blew through, the shot finished much heavier
    // than intended (e.g. 18 g → 40 g target, 56 g actual at grind 11). The
    // existing detectors are silent on this failure mode: flow tracks goal in
    // flow-mode phases (controller pulls back), pressure-mode choke arms are
    // gated on pressurizedDuration ≥ CHOKED_DURATION_MIN_SEC which a gusher
    // never reaches, detectPourTruncated needs peak < 2.5 bar which a gusher
    // can briefly exceed, and the channeling derivative looks normal because
    // the puck never built resistance to channel through.
    //
    // The arm has no pressurized-window gate (a gusher can't sustain pressure
    // long enough to satisfy CHOKED_DURATION_MIN_SEC, even when peak briefly
    // exceeds CHOKED_PRESSURE_MIN_BAR). Preconditions: targetWeightG > 0,
    // finalWeightG > 0, beverageType not in the filter/pourover/tea/steam/
    // cleaning skip list (espresso and unknown/empty beverage types both pass).
    // See analyzeFlowVsGoal() for placement.
    static constexpr double YIELD_OVERSHOOT_RATIO_MIN = 1.20;

    // Trim two boundary windows where flow naturally diverges from goal even
    // on well-extracted shots, otherwise the grind detector false-flags every
    // lever-style preinfusion:
    //   - Leading window of the first flow-mode phase: pump ramp-up from
    //     idle, mechanical lag rather than a grind signal.
    //   - Trailing window of any flow-mode phase that exits via the
    //     "pressure" transition: once the firmware's pressure ceiling
    //     engages, the controller stops tracking flow goal and switches to
    //     limiting pressure, so the trailing flow undershoot is by design.
    static constexpr double GRIND_PUMP_RAMP_SKIP_SEC = 0.5;
    static constexpr double GRIND_LIMITER_TAIL_SKIP_SEC = 1.5;

    // Result of the grind direction check.
    struct GrindCheck {
        // (avg actual flow) - (avg goal flow) on the flow-vs-goal path. Positive
        // = coarse, negative = fine. Meaningful only when both chokedPuck ==
        // false AND yieldOvershoot == false — both paths leave delta at its
        // default (0.0) when their own arm fires without the flow-vs-goal
        // averaging path producing data. Read chokedPuck and yieldOvershoot
        // first; consumers that read delta unconditionally would mistake
        // "yield arm fired without flow data" for "flow matched goal."
        double delta = 0.0;
        // Number of qualifying samples averaged (flow-vs-goal path) or the
        // count of pressurized samples observed (choked-puck path).
        qsizetype sampleCount = 0;
        bool hasData = false;        // true when at least one arm produced a result (flow-vs-goal averaging, choked-puck check, yield-overshoot, or the choked-puck loop's gates passed without firing a choke — the verified-clean path)
        bool skipped = false;        // true when suppressed by a flag or beverage type
        bool chokedPuck = false;     // true when the pressure-mode choke check fired — either mean pressurized flow below CHOKED_FLOW_MAX_MLPS (severe) or yield/target below CHOKED_YIELD_RATIO_MAX (moderate)
        bool yieldOvershoot = false; // true when yield/target > YIELD_OVERSHOOT_RATIO_MIN — gusher; mutually exclusive with chokedPuck only on the yield-ratio sub-arm (one < 0.85, the other > 1.20). chokedPuck's flow sub-arm could in principle co-fire, but a gusher cannot satisfy its 15s × 4 bar gate in practice.
        bool verifiedClean = false;  // true when the choked-puck loop saw enough pressurized samples to speak (≥ 5 samples, ≥ 15s ≥ 4 bar) AND neither chokedPuck nor yieldOvershoot fired AND |delta| ≤ FLOW_DEVIATION_THRESHOLD. Distinguishes "verified clean" from "no data" so the summary can emit a positive [good] line on profiles where today the detector silently passes.
    };

    // Grind direction check — the canonical implementation shared by the
    // badge path (detectGrindIssue) and the summary path (analyzeShot).
    //
    // Two paths feed the same GrindCheck result:
    //   1. Flow-vs-goal averaging across flow-controlled phases (the primary
    //      path; sets delta and hasData when ≥ 5 qualifying samples land
    //      inside flow-mode windows).
    //   2. Choked-puck check, restricted to pressure-mode portions of the
    //      pour. Runs in addition to path 1 (not as a fallback) because
    //      shots like 80's Espresso have a healthy flow-mode preinfusion
    //      that pins delta near zero; the choke happens entirely in the
    //      pressure-mode tail and is invisible to flow-vs-goal averaging.
    //      Two arms: severe (mean pressurized flow < CHOKED_FLOW_MAX_MLPS)
    //      and moderate (yield/target < CHOKED_YIELD_RATIO_MAX). Both
    //      gated on pressurizedDuration ≥ CHOKED_DURATION_MIN_SEC. Either
    //      sets chokedPuck=true. Consumers branch on chokedPuck before
    //      reading delta.
    //
    // analysisFlags honored: "grind_check_skip" forces skipped=true. Filter/
    // pourover beverage types also short-circuit to skipped=true. Pressure
    // is optional; when omitted, only the flow-vs-goal path is available.
    // targetWeightG and finalWeightG drive both the yield-ratio choked arm
    // (yield/target < CHOKED_YIELD_RATIO_MAX) and the yield-overshoot arm
    // (yield/target > YIELD_OVERSHOOT_RATIO_MIN); either being 0 disables
    // both (both default to 0). finalWeightG can come from either a real
    // BLE scale or Decenza's FlowScale virtual scale (dose-compensated flow
    // integration), so the arms work headless. targetWeightG is the shot's
    // effective SAW target (see ShotRecord::yieldOverride); imported shots
    // without target metadata pass 0 here and both arms correctly stay
    // silent. The yield-overshoot arm has no pressurized-window gate — a
    // gusher by definition can't sustain pressure — so it can fire even
    // when pressure or flow data is empty.
    static GrindCheck analyzeFlowVsGoal(const QVector<QPointF>& flow,
                                         const QVector<QPointF>& flowGoal,
                                         const QList<HistoryPhaseMarker>& phases,
                                         double pourStart, double pourEnd,
                                         const QString& beverageType = {},
                                         const QStringList& analysisFlags = {},
                                         const QVector<QPointF>& pressure = {},
                                         double targetWeightG = 0.0,
                                         double finalWeightG = 0.0);

    // Returns true if the grind direction check flags a meaningful deviation
    // (|delta| > FLOW_DEVIATION_THRESHOLD, chokedPuck fired, or yieldOvershoot
    // fired). Returns false when the check is skipped, there is insufficient
    // data, or deviation is within tolerance.
    static bool detectGrindIssue(const QVector<QPointF>& flow,
                                  const QVector<QPointF>& flowGoal,
                                  const QList<HistoryPhaseMarker>& phases,
                                  double pourStart, double pourEnd,
                                  const QString& beverageType = {},
                                  const QStringList& analysisFlags = {},
                                  const QVector<QPointF>& pressure = {},
                                  double targetWeightG = 0.0,
                                  double finalWeightG = 0.0);

    // Returns true when the pour never pressurized — peak pressure inside
    // the pour window stayed below PRESSURE_FLOOR_BAR. Diagnoses puck
    // failures that the dC/dt channeling detector and the flow-vs-goal
    // grind direction check cannot see: when the puck offers near-zero
    // resistance, conductance saturates, the derivative is zero, and flow
    // tracks the preinfusion goal perfectly — all three signals that would
    // normally scream "bad shot" are silent. Looking directly at the
    // pressure curve catches those cases.
    //
    // Skipped for non-espresso beverage types (filter/pourover/tea/steam/
    // cleaning) where low pressure is expected.
    static bool detectPourTruncated(const QVector<QPointF>& pressure,
                                     double pourStart, double pourEnd,
                                     const QString& beverageType = {});

    // Returns true if the shot appears to have skipped profile frame 0. Two
    // distinct branches:
    //   - FW-bug: frame 0 was never observed before a non-zero frame. Always
    //     uses the de1app plugin's hard 2 s window (parity with the polling
    //     Tcl plugin); firstFrameConfiguredSeconds is ignored here because the
    //     machine never executed frame 0 at all, so the configured duration
    //     is irrelevant.
    //   - Short-first-step: frame 0 was observed but ended early. Cutoff is
    //     min(2.0, 0.5 * firstFrameConfiguredSeconds) when configured > 0, else
    //     a hard 2 s window. The configured-aware path avoids false-positives
    //     on profiles with frame[0].seconds == 2 where normal sub-frame BLE
    //     jitter routinely lands the frame-1 marker just under 2 s.
    // expectedFrameCount is optional; when known, values < 2 suppress detection
    // (no second frame to skip to) and out-of-range frame numbers are treated
    // as malformed data. The FW-bug case requires a full power-cycle to fix.
    // Returns false when phases is empty.
    static bool detectSkipFirstFrame(const QList<HistoryPhaseMarker>& phases,
                                     int expectedFrameCount = -1,
                                     double firstFrameConfiguredSeconds = -1.0);

    // Structured detector outputs — the typed values behind the in-app
    // Shot Summary dialog's detector evaluations. Exposed so external
    // consumers (MCP `shots_get_detail`, regression tests) can read the
    // same signals without parsing prose. Every observation line in
    // `summaryLines` is formatted from one of these fields, but the
    // struct is a *superset* of what `summaryLines` renders: clean signals
    // (e.g. `flowTrend = "stable"`, `grindDirection = "onTarget"`,
    // `tempUnstable = false`) are captured here even when no prose line
    // is emitted — silence in the dialog is not silence in the struct.
    // Kept in sync with `summaryLines` by construction: `analyzeShot`
    // populates this struct as it walks the detectors and formats lines
    // from the same intermediates, so a detector flip moves both
    // outputs together.
    //
    // Field semantics — most detectors follow a "checked / not-checked"
    // pattern. `*Checked == false` means the detector was suppressed
    // (pour truncated cascade, beverage-type skip, profile analysisFlag,
    // or insufficient input data); the rest of that detector's fields are
    // unset / default. Distinguishing "not checked" from "checked, no
    // signal" matters to consumers — silence on a skipped detector is
    // not the same as silence on a clean shot.
    struct DetectorResults {
        // === Pour-truncated (puck failure) — runs first; dominates the cascade ===
        // detectPourTruncated() is gated only by beverage type, so for
        // espresso it always evaluates and `pourTruncated` is meaningful
        // on every shot. peakPressureBar is set only when the detector
        // fires (used in the warning line).
        bool pourTruncated = false;
        double peakPressureBar = 0.0;

        // === Pour window (seconds) ===
        // The phase-boundary range analyzeShot computed internally to gate
        // every other detector. Exposed so MCP `shots_get_detail` consumers
        // and regression tests can read the same window the cascade used
        // instead of re-deriving it from phase markers. Removing that
        // duplication is what let the previous `ShotSummarizer::computePourWindow`
        // drift from analyzeShot's logic; see PR #944.
        //
        // pourStartSec is 0.0 when no "preinfusion"/"pour" markers are
        // present (the boundary loop didn't fire); pourEndSec defaults to
        // shot duration when no "end" marker is present. Both fields stay
        // at 0.0 only when analyzeShot took the insufficient-data early
        // return (pressure.size() < 10) — distinguishable from the no-marker
        // case by pourEndSec being 0.0 instead of the shot duration.
        double pourStartSec = 0.0;
        double pourEndSec = 0.0;

        // === Channeling (dC/dt) ===
        bool channelingChecked = false;
        QString channelingSeverity;       // "" if !checked; else "none" | "transient" | "sustained"
        double channelingSpikeTimeSec = 0.0;  // 0 unless transient/sustained

        // === Flow trend during extraction ===
        // delta = (avg flow in last 30% of pour) − (avg in first 30%).
        bool flowTrendChecked = false;
        QString flowTrend;                // "" if !checked; "stable" | "rising" | "falling"
        double flowTrendDeltaMlPerSec = 0.0;

        // === Preinfusion drip ===
        // Observation only — fires when preinfusion weight > 0.5 g and
        // duration > 1 s. Not part of the warning/caution cascade.
        bool preinfusionObserved = false;
        double preinfusionDripWeightG = 0.0;
        double preinfusionDripDurationSec = 0.0;

        // === Temperature stability ===
        bool tempStabilityChecked = false;
        bool tempIntentionalStepping = false;
        double tempAvgDeviationC = 0.0;
        bool tempUnstable = false;

        // === Grind direction (mirrors GrindCheck plus a derived label) ===
        bool grindChecked = false;
        bool grindHasData = false;
        bool grindChokedPuck = false;
        bool grindYieldOvershoot = false;
        bool grindVerifiedClean = false;
        double grindFlowDeltaMlPerSec = 0.0;
        qsizetype grindSampleCount = 0;
        // "" if !hasData. Otherwise one of:
        //   "yieldOvershoot" — gusher (precedes chokedPuck/delta in the cascade)
        //   "chokedPuck"     — pressurized but flow ~0
        //   "tooFine"        — flow averaged below goal beyond threshold
        //   "tooCoarse"      — flow averaged above goal beyond threshold
        //   "onTarget"       — flow tracked goal within tolerance OR Arm 2
        //                      verified a healthy pour without Arm 1 data.
        //                      Read grindSampleCount before assuming
        //                      "onTarget" implies Arm 1 averaged ≥ 5
        //                      qualifying samples — sampleCount can be 0
        //                      on the verified-clean path when Arm 1's
        //                      flow-mode window produced no data.
        QString grindDirection;
        // Coverage signal for the grind detector. Populated for espresso
        // shots where the pourTruncated cascade is NOT active. Possible
        // values:
        //   "verified"      — grindHasData == true. The detector ran with
        //                     enough data to produce a result. Set whether
        //                     or not the result is healthy: a verified-clean
        //                     pour AND a chokedPuck/yieldOvershoot/large-delta
        //                     pour BOTH carry "verified". Coverage signals
        //                     data availability, not health outcome — read
        //                     grindVerifiedClean / grindDirection / verdict
        //                     for the diagnosis.
        //   "notAnalyzable" — espresso shot, non-degenerate window, but
        //                     neither arm produced data (e.g. pure flow-mode
        //                     two-frame profile, or pressurized < 15s)
        //   "skipped"       — non-espresso beverage or grind_check_skip flag
        //   ""              — pourTruncated cascade suppressed grind block,
        //                     OR pour window degenerate (pourEnd <= pourStart)
        QString grindCoverage;

        // === Skip-first-frame ===
        bool skipFirstFrame = false;

        // === Verdict category (no prose) ===
        // Stable enum-like string for downstream agents. The dialog's
        // English verdict text is composed from this category but is NOT
        // exposed via this struct — verdict prose is dialog UX, not API
        // surface. Possible values:
        //   "clean"                       — no warnings or cautions; grind verified or skipped
        //   "cleanGrindNotAnalyzable"     — no warnings or cautions, but the grind detector
        //                                   could not analyze this profile shape
        //   "insufficientData"            — pressure series < 10 samples; analyzeShot
        //                                   bailed without running detectors (every
        //                                   `checked` flag stays false)
        //   "puckTruncated"               — pour never pressurized
        //   "skipFirstFrame"              — frame 0 not executed
        //   "yieldOvershoot"              — gusher
        //   "chokedPuck"                  — puck choked off
        //   "puckIntegrityGrindFine"      — channeling/warning + grind too fine
        //   "puckIntegrityGrindCoarse"    — channeling/warning + grind too coarse
        //   "puckIntegrity"               — warning, no clear grind direction
        //   "minorIssuesGrindFine"        — caution-only + grind too fine
        //   "minorIssuesGrindCoarse"      — caution-only + grind too coarse
        //   "minorIssues"                 — caution-only, no clear grind direction
        QString verdictCategory;
    };

    // Combined output of the shot-summary pipeline. `lines` is the prose
    // observation list (same as legacy `generateSummary` return value);
    // `detectors` holds the structured intermediates that lines were
    // formatted from, plus clean-signal fields the dialog leaves silent
    // (see DetectorResults). Sharing one return value guarantees both
    // outputs describe the same evaluation — no chance for them to
    // drift across consumers.
    struct AnalysisResult {
        QVariantList lines;
        DetectorResults detectors;
    };

    // Run the full shot-summary pipeline. Returns both the prose lines
    // (rendered by the in-app Shot Summary dialog and fed into the AI
    // advisor prompt) and the structured detector results (consumed by
    // MCP `shots_get_detail` so external agents can read the same
    // signals without parsing prose). All detectors run once and feed
    // both outputs.
    static AnalysisResult analyzeShot(const QVector<QPointF>& pressure,
                                       const QVector<QPointF>& flow,
                                       const QVector<QPointF>& weight,
                                       const QVector<QPointF>& temperature,
                                       const QVector<QPointF>& temperatureGoal,
                                       const QVector<QPointF>& conductanceDerivative,
                                       const QList<HistoryPhaseMarker>& phases,
                                       const QString& beverageType,
                                       double duration,
                                       const QVector<QPointF>& pressureGoal = {},
                                       const QVector<QPointF>& flowGoal = {},
                                       const QStringList& analysisFlags = {},
                                       double firstFrameConfiguredSeconds = -1.0,
                                       double targetWeightG = 0.0,
                                       double finalWeightG = 0.0,
                                       int expectedFrameCount = -1);

    // Backwards-compatible thin wrapper — equivalent to
    // analyzeShot(...).lines. Existing callers (in-app dialog, AI advisor
    // prompt builder) keep working unchanged.
    static QVariantList generateSummary(const QVector<QPointF>& pressure,
                                         const QVector<QPointF>& flow,
                                         const QVector<QPointF>& weight,
                                         const QVector<QPointF>& temperature,
                                         const QVector<QPointF>& temperatureGoal,
                                         const QVector<QPointF>& conductanceDerivative,
                                         const QList<HistoryPhaseMarker>& phases,
                                         const QString& beverageType,
                                         double duration,
                                         const QVector<QPointF>& pressureGoal = {},
                                         const QVector<QPointF>& flowGoal = {},
                                         const QStringList& analysisFlags = {},
                                         double firstFrameConfiguredSeconds = -1.0,
                                         double targetWeightG = 0.0,
                                         double finalWeightG = 0.0,
                                         int expectedFrameCount = -1);
};
