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
    //     WINDOW_CONVERGED_REL).
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

    // Result of the flow-vs-goal grind direction check.
    struct GrindCheck {
        double delta = 0.0;          // (avg actual flow) - (avg goal flow). Positive = coarse, negative = fine.
        qsizetype sampleCount = 0;   // number of qualifying samples included in the average
        bool hasData = false;        // true when sampleCount ≥ 5 and the check ran
        bool skipped = false;        // true when suppressed by a flag or beverage type
    };

    // Flow-vs-goal grind direction check — the canonical implementation
    // shared by the badge path (detectGrindIssue) and the summary path
    // (generateSummary). Only averages samples that fall inside a
    // flow-controlled phase (HistoryPhaseMarker::isFlowMode == true). If no
    // flow-controlled pour phase exists (e.g. 80's Espresso, Cremina,
    // Londinium — entire pour is pressure-controlled), the check returns
    // hasData=false and callers treat the shot as "grind direction not
    // evaluable from flow goal."
    //
    // analysisFlags honored: "grind_check_skip" forces skipped=true. Filter/
    // pourover beverage types also short-circuit to skipped=true.
    static GrindCheck analyzeFlowVsGoal(const QVector<QPointF>& flow,
                                         const QVector<QPointF>& flowGoal,
                                         const QList<HistoryPhaseMarker>& phases,
                                         double pourStart, double pourEnd,
                                         const QString& beverageType = {},
                                         const QStringList& analysisFlags = {});

    // Returns true if the flow-vs-goal check flags a meaningful deviation
    // (|delta| > FLOW_DEVIATION_THRESHOLD). Returns false when the check is
    // skipped, there is insufficient data, or deviation is within tolerance.
    // Phase-mode aware: restricts averaging to flow-controlled phases, so
    // pressure-mode profiles (lever, D-Flow pour, etc.) no longer trigger on
    // flow that is naturally below a profile's flow-limiter ceiling.
    static bool detectGrindIssue(const QVector<QPointF>& flow,
                                  const QVector<QPointF>& flowGoal,
                                  const QList<HistoryPhaseMarker>& phases,
                                  double pourStart, double pourEnd,
                                  const QString& beverageType = {},
                                  const QStringList& analysisFlags = {});

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

    // Returns true if the shot appears to have skipped profile frame 0, indicating
    // either a known DE1 firmware bug (machine started at frame 1+) or a profile
    // whose first step is so short (< 2 s) it was never meaningfully executed.
    // Mirrors the de1app plugin semantics against Decenza's saved phase markers:
    // ignore the synthetic "Start" marker, then inspect real frame markers only
    // within the first 2 seconds of extraction. expectedFrameCount is optional;
    // when known, values less than 2 suppress detection (there is no second frame
    // to skip to), and out-of-range frame numbers are ignored as malformed data.
    // The firmware bug requires a full power-cycle of the machine to fix.
    // Returns false when phases is empty (insufficient data).
    static bool detectSkipFirstFrame(const QList<HistoryPhaseMarker>& phases,
                                     int expectedFrameCount = -1);

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
                                         const QStringList& analysisFlags = {});
};
