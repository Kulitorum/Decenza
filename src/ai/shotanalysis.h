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
    //   * For flow-mode phases only, actual pressure is not rising rapidly
    //     in the next WINDOW_HALF_SEC — the lever pressure-rise pattern
    //     would otherwise read as channeling. Falling pressure is allowed.
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
    //     mean during the pressure-mode tail).
    //   - Yield arm: yield/target < CHOKED_YIELD_RATIO_MAX — catches moderate
    //     chokes like shot 883 (25 g of 36 g target, ~0.6 ml/s mean — narrowly
    //     above the flow threshold but the puck still failed to deliver).
    // Both arms share the `flowSamples ≥ 5 && pressurizedDuration ≥
    // CHOKED_DURATION_MIN_SEC` gate so neither fires on aborted shots.
    static constexpr double CHOKED_PRESSURE_MIN_BAR = 4.0;
    static constexpr double CHOKED_FLOW_MAX_MLPS = 0.5;
    static constexpr double CHOKED_DURATION_MIN_SEC = 15.0;
    static constexpr double CHOKED_YIELD_RATIO_MAX = 0.85;

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
        // = coarse, negative = fine. Meaningful only when chokedPuck == false;
        // on the choked-puck path delta is left at its default and chokedPuck
        // is the sole signal — read chokedPuck first.
        double delta = 0.0;
        // Number of qualifying samples averaged (flow-vs-goal path) or the
        // count of pressurized samples observed (choked-puck path).
        qsizetype sampleCount = 0;
        bool hasData = false;        // true when the check ran (either path)
        bool skipped = false;        // true when suppressed by a flag or beverage type
        bool chokedPuck = false;     // true when the pressure-mode choke check fired — either mean pressurized flow below CHOKED_FLOW_MAX_MLPS (severe) or yield/target below CHOKED_YIELD_RATIO_MAX (moderate)
    };

    // Grind direction check — the canonical implementation shared by the
    // badge path (detectGrindIssue) and the summary path (generateSummary).
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
    // targetWeightG and finalWeightG drive the yield-ratio arm of the
    // choked-puck check; either being 0 disables it (both default to 0).
    // finalWeightG can come from either a real BLE scale or Decenza's
    // FlowScale virtual scale (dose-compensated flow integration), so the
    // arm works headless. targetWeightG is the shot's effective SAW target
    // (see ShotRecord::yieldOverride); imported shots without target
    // metadata pass 0 here and the arm correctly stays silent.
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
    // (|delta| > FLOW_DEVIATION_THRESHOLD, or chokedPuck fired). Returns
    // false when the check is skipped, there is insufficient data, or
    // deviation is within tolerance.
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

    // Generate a concise shot summary from curve data. Returns a list of
    // noteworthy observations + a verdict. Used by ShotAnalysisDialog.qml.
    // flowGoal is the profile's target flow curve and drives grind-direction analysis (may be empty).
    // pressureGoal is accepted for API symmetry but currently unused.
    // analysisFlags controls suppression of checks for profiles where certain
    // behaviors are intentional — see ProfileKnowledge::analysisFlags for values.
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
                                         double finalWeightG = 0.0);
};
