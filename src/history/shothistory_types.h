#pragma once

#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QPointF>
#include <QVector>
#include <QList>

// Lightweight shot summary for list display
struct HistoryShotSummary {
    qint64 id = 0;
    QString uuid;
    qint64 timestamp = 0;
    QString profileName;
    double duration = 0;
    double finalWeight = 0;
    double doseWeight = 0;
    QString beanBrand;
    QString beanType;
    int enjoyment = 0;
    bool hasVisualizerUpload = false;
    QString beverageType;
};

// Phase marker for shot display
struct HistoryPhaseMarker {
    double time = 0;
    QString label;
    int frameNumber = 0;
    bool isFlowMode = false;
    QString transitionReason;  // "weight", "pressure", "flow", "time", or "" (unknown/old data)
};

// Full shot record for detail view / comparison
struct ShotRecord {
    HistoryShotSummary summary;

    // Full metadata
    QString roastDate;
    QString roastLevel;
    QString grinderBrand;
    QString grinderModel;
    QString grinderBurrs;
    QString grinderSetting;
    double drinkTds = 0;
    double drinkEy = 0;
    QString espressoNotes;
    QString beanNotes;
    QString barista;
    QString profileNotes;
    QString visualizerId;
    QString visualizerUrl;

    // Time-series data (lazily loaded)
    QVector<QPointF> pressure;
    QVector<QPointF> flow;
    QVector<QPointF> temperature;
    QVector<QPointF> pressureGoal;
    QVector<QPointF> flowGoal;
    QVector<QPointF> temperatureGoal;
    QVector<QPointF> temperatureMix;
    QVector<QPointF> resistance;
    QVector<QPointF> conductance;
    QVector<QPointF> darcyResistance;
    QVector<QPointF> conductanceDerivative;
    QVector<QPointF> waterDispensed;
    QVector<QPointF> weight;
    QVector<QPointF> weightFlowRate;  // Flow rate from scale (g/s) for visualizer export

    // Phase markers
    QList<HistoryPhaseMarker> phases;

    // Debug log
    QString debugLog;

    // Brew overrides (always have values - user override or profile default)
    double temperatureOverride = 0.0;
    double yieldOverride = 0.0;

    // Profile snapshot
    QString profileJson;

    // AI knowledge base ID (e.g. "d-flow", "blooming espresso") for profile-aware analysis
    QString profileKbId;

    // Quality flags (computed at save time, recomputed on-the-fly for legacy shots)
    bool channelingDetected = false;
    bool temperatureUnstable = false;
    bool grindIssueDetected = false;
    bool skipFirstFrameDetected = false;

    // Phase summaries JSON (per-phase metrics: duration, avgPressure, avgFlow, weightGained, etc.)
    QString phaseSummariesJson;
};

// Grinder settings context from shot history (shared by MCP and in-app AI)
struct GrinderContext {
    QString model;
    QString beverageType;
    QStringList settingsObserved;
    bool allNumeric = false;
    double minSetting = 0;
    double maxSetting = 0;
    double smallestStep = 0;
};

// Filter criteria for queries
struct ShotFilter {
    QString profileName;
    QString beanBrand;
    QString beanType;
    QString grinderBrand;
    QString grinderModel;
    QString grinderBurrs;
    QString grinderSetting;
    QString roastLevel;
    int minEnjoyment = -1;
    int maxEnjoyment = -1;
    double minDose = -1;
    double maxDose = -1;
    double minYield = -1;         // filters final_weight (actual pour)
    double maxYield = -1;
    double yieldOverride = -1;    // filters yield_override (saved target) — exact match
    double minDuration = -1;
    double maxDuration = -1;
    double minTds = -1;
    double maxTds = -1;
    double minEy = -1;
    double maxEy = -1;
    qint64 dateFrom = 0;       // Unix timestamp
    qint64 dateTo = 0;
    QString searchText;        // FTS search in notes
    bool onlyWithVisualizer = false;
    bool filterChanneling = false;
    bool filterTemperatureUnstable = false;
    bool filterGrindIssue = false;
    bool filterSkipFirstFrame = false;
    QString sortColumn = "timestamp";
    QString sortDirection = "DESC";
};

// Pre-extracted data for async shot saving (no QObject pointers, thread-safe by value)
struct ShotSaveData {
    QString uuid;
    qint64 timestamp = 0;
    QString profileName;
    QString profileJson;
    QString beverageType;
    double duration = 0;
    double finalWeight = 0;
    double doseWeight = 0;
    double temperatureOverride = 0;
    double yieldOverride = 0;

    // Metadata
    QString beanBrand;
    QString beanType;
    QString roastDate;
    QString roastLevel;
    QString grinderBrand;
    QString grinderModel;
    QString grinderBurrs;
    QString grinderSetting;
    double drinkTds = 0;
    double drinkEy = 0;
    int espressoEnjoyment = 0;
    QString espressoNotes;
    QString barista;
    QString profileNotes;
    QString debugLog;

    // AI knowledge base ID (e.g. "d-flow", "blooming espresso") — computed at save time
    QString profileKbId;

    // Quality flags (computed at save time using ShotAnalysis helpers)
    bool channelingDetected = false;
    bool temperatureUnstable = false;
    bool grindIssueDetected = false;
    bool skipFirstFrameDetected = false;

    // Phase summaries JSON (per-phase metrics for UI display)
    QString phaseSummariesJson;

    // Pre-compressed sample data blob
    QByteArray compressedSamples;
    int sampleCount = 0;

    // Phase markers (pre-extracted from QVariantList)
    QList<HistoryPhaseMarker> phaseMarkers;
};
