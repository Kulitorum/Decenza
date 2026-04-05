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
    static constexpr double CHANNELING_MAX_AVG_FLOW = 3.0;        // mL/s — skip turbo/filter shots
    static constexpr double TEMP_UNSTABLE_THRESHOLD = 2.0;        // °C avg deviation from goal
    static constexpr double TEMP_STEPPING_RANGE = 5.0;            // °C goal range = intentional stepping

    // --- Channeling detection ---

    // Severity levels reported by detectChannelingFromDerivative().
    enum class ChannelingSeverity {
        None,        // dC/dt stays below the transient threshold — clean puck
        Transient,   // a single spike > CHANNELING_DC_TRANSIENT_PEAK (self-healed channel)
        Sustained    // >CHANNELING_DC_SUSTAINED_COUNT samples above CHANNELING_DC_ELEVATED
    };

    // Analyze the conductance derivative (dC/dt) to classify puck integrity.
    // `conductanceDerivative` is expected to be the Gaussian-smoothed dC/dt
    // series produced by ShotDataModel::computeConductanceDerivative() or
    // ShotHistoryStorage::computeDerivedCurves(). Samples in the first
    // CHANNELING_DC_POUR_SKIP_SEC of pour are skipped to avoid the transition
    // spike that happens when pressure ramps and flow catches up.
    // outMaxSpikeTime (optional) receives the timestamp of the largest spike.
    static ChannelingSeverity detectChannelingFromDerivative(
        const QVector<QPointF>& conductanceDerivative,
        double pourStart, double pourEnd,
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
