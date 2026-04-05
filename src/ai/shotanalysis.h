#pragma once

#include <QVector>
#include <QPointF>
#include <QString>
#include <QStringList>

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
    static constexpr double CHANNELING_FLOW_SPIKE_RATIO = 1.5;   // 50% increase = spike
    static constexpr double CHANNELING_MIN_PREV_FLOW = 0.5;      // mL/s - ignore near-zero
    static constexpr int    CHANNELING_SAMPLE_DISTANCE = 5;       // ~1s at 5Hz
    static constexpr double CHANNELING_MIN_PHASE_DURATION = 3.0;  // seconds
    static constexpr double CHANNELING_MAX_AVG_FLOW = 3.0;        // mL/s - skip turbo/filter
    static constexpr double TEMP_UNSTABLE_THRESHOLD = 2.0;        // °C avg deviation from goal
    static constexpr double TEMP_STEPPING_RANGE = 5.0;            // °C goal range = intentional stepping

    // --- Channeling detection ---

    // Check for flow spikes in a time range. Returns true if a >50% flow spike
    // is found (at ~1s resolution).
    static bool detectChannelingInRange(const QVector<QPointF>& flowData,
                                        double startTime, double endTime);

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

    // Generate a concise shot summary from curve data. Returns a list of
    // noteworthy observations + a verdict. Used by ShotAnalysisDialog.qml.
    static QVariantList generateSummary(const QVector<QPointF>& pressure,
                                         const QVector<QPointF>& flow,
                                         const QVector<QPointF>& weight,
                                         const QVector<QPointF>& temperature,
                                         const QVector<QPointF>& temperatureGoal,
                                         const QVector<QPointF>& conductanceDerivative,
                                         const QList<HistoryPhaseMarker>& phases,
                                         const QString& beverageType,
                                         double duration);
};
