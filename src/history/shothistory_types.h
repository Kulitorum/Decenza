#pragma once

#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QPointF>
#include <QVector>
#include <QList>
#include <optional>

#include "ai/shotanalysis.h"
#include "history/bagid.h"

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
    // Why the PRECEDING frame exited. Confirmed ground truth: "weight",
    // "pressure", "flow". Likely-but-unconfirmed sensor exit (threshold not
    // seen in the BLE sample at transition): "pressure_unconfirmed",
    // "flow_unconfirmed". Time-based: "time". "" = unknown/old data.
    QString transitionReason;
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
    // Basket identity resolved via equipment_id (add-basket-equipment); empty when
    // the package has no basket.
    QString basketBrand;
    QString basketModel;
    // Puck-prep canonical flag string resolved via equipment_id
    // (add-puckprep-equipment); empty when the package has no puck prep.
    QString puckPrep;
    QString grinderSetting;
    qint64 equipmentId = 0;  // FK -> equipment_packages.id (add-equipment-packages); 0 = none
    qint64 rpm = 0;          // grinder rpm dial-in; 0 = unset
    // Lineage state of the shot's equipment package, resolved at load (add-
    // equipment-packages 4b.7): "" = current or no equipment, "older" = a newer
    // package superseded this one (copy-on-write fork), "retired" = removed from
    // inventory with no successor. Rendered as a muted qualifier in history.
    QString equipmentState;
    // Package display name, resolved at load (defaults to "{brand} {model}").
    QString equipmentName;
    double drinkTds = 0;
    double drinkEy = 0;
    QString espressoNotes;
    QString beanNotes;
    QString barista;
    QString profileNotes;
    QString visualizerId;
    QString visualizerUrl;

    // Compact-JSON linked-bean snapshot at shot time ("" = unlinked /
    // pre-migration-18 shot) — Visualizer canonical or Bean Base sourced;
    // see docs/CLAUDE_MD/BEAN_BASE.md and ShotSaveData::beanBaseJson.
    QString beanBaseJson;

    // Coffee bag snapshot (bean-bag-inventory, migration 19): the bag this
    // shot was pulled with and its freeze lifecycle at shot time. "No bag /
    // pre-bag shot" sentinel is bagId <= 0 (default -1; the DB read maps NULL
    // to -1) — see bagIdIsSet() in bagid.h.
    qint64 bagId = -1;
    QString frozenDate;
    QString defrostDate;

    // Recipe provenance (add-recipes, migration 25): the recipe active at
    // shot start (<= 0 = none / pre-recipe shot) and the steam-spec snapshot
    // in effect, so promote-from-shot round-trips the whole drink.
    qint64 recipeId = -1;
    QString steamJson;

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

    // Brew overrides. Populated at save time by MainController:
    //   - temperatureOverride: user override OR profile espresso_temperature
    //   - targetWeight: user brew-by-ratio override OR profile target_weight,
    //     falling back to finalWeight for volume/timer profiles where neither
    //     is set (so the favorites system always has something to restore).
    // For shots imported from external formats (de1app, visualizer.coffee)
    // these fields may stay at 0 — importers don't populate them. Analysis
    // code that relies on targetWeight as the SAW target (e.g. the grind
    // detector's yield-ratio arm) treats 0 as "unknown" and disables itself.
    //
    // Persisted in the `shots.yield_override` DB column — the column name
    // predates the rename. JSON projection emits this as `targetWeightG`
    // (units-suffixed per MCP convention).
    double temperatureOverride = 0.0;
    double targetWeight = 0.0;

    // Why the shot ended (#1161). One of: "weight" (stop-at-weight / SAW),
    // "volume" (stop-at-volume / SAV), "manual" (user tapped Stop in the
    // app), "profileEnd" (profile ran its course OR the DE1's own button —
    // the BLE protocol does not distinguish these), or "" (unknown:
    // pre-migration-17 shots, imported shots, fake/dev shots). Persisted in
    // the `shots.stopped_by` column. A "manual" stop — or any non-"weight"/
    // non-"volume" stop whose finalWeight fell well short of targetWeight —
    // means the yield was user-chosen, not an extraction outcome, so it is
    // not dial-in diagnostic.
    QString stoppedBy;

    // Profile snapshot
    QString profileJson;

    // AI knowledge base ID (e.g. "d-flow", "blooming espresso") for profile-aware analysis
    QString profileKbId;

    // Quality flags (computed at save time, recomputed on-the-fly for legacy shots).
    //
    // pourTruncatedDetected is the dominant flag — when it fires, the puck never
    // built pressure, so channeling / grind signals are unreliable readings
    // off curves the failed puck didn't produce. The save and load paths force
    // those two (channelingDetected, grindIssueDetected) to false in that case
    // so the UI doesn't show a contradictory "Clean extraction" chip on top
    // of a puck failure. skipFirstFrameDetected is NOT suppressed — it's a
    // machine/profile issue orthogonal to puck integrity and can co-fire
    // with pourTruncatedDetected.
    // See ShotAnalysis::detectPourTruncated for the underlying detector.
    bool channelingDetected = false;
    bool grindIssueDetected = false;
    bool skipFirstFrameDetected = false;
    bool pourTruncatedDetected = false;

    // Phase summaries JSON (per-phase metrics: duration, avgPressure, avgFlow, weightGained, etc.)
    QString phaseSummariesJson;

    // Cached output of ShotAnalysis::analyzeShot, populated by
    // ShotHistoryStorage::loadShotRecordStatic after the badge projection
    // runs. ShotHistoryStorage::convertShotRecord reads from this when
    // present so the detector pipeline runs exactly once per detail-load
    // (load + convert), not twice. When absent — direct construction in
    // tests, or any path that bypasses loadShotRecordStatic —
    // convertShotRecord falls back to running analyzeShot inline.
    //
    // Invalidation rule: if any input curve on this ShotRecord
    // (pressure/flow/temperature/etc.) is mutated after load, callers
    // MUST reset cachedAnalysis to std::nullopt. Today no caller mutates
    // ShotRecord between loadShotRecordStatic and convertShotRecord, so
    // this is structurally safe — but the rule is documented here so
    // future callers don't introduce a stale-cache bug.
    std::optional<ShotAnalysis::AnalysisResult> cachedAnalysis;
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
    double targetWeight = -1;    // filters yield_override (saved target) — exact match
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
    bool filterGrindIssue = false;
    bool filterSkipFirstFrame = false;
    bool filterPourTruncated = false;
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
    double targetWeight = 0;

    // Why the shot ended (#1161): "weight" | "volume" | "manual" |
    // "profileEnd" | "" (unknown). Classified in MainController::onShotEnded
    // from SAW/SAV state + the user-stop flag. See ShotRecord::stoppedBy.
    QString stoppedBy;

    // Metadata
    QString beanBrand;
    QString beanType;
    QString roastDate;
    QString roastLevel;
    QString grinderBrand;
    QString grinderModel;
    QString grinderBurrs;
    QString grinderSetting;
    qint64 equipmentId = 0;  // FK -> equipment_packages.id (add-equipment-packages); 0 = none
    qint64 rpm = 0;          // grinder rpm dial-in; 0 = unset
    double drinkTds = 0;
    double drinkEy = 0;
    int espressoEnjoyment = 0;
    QString espressoNotes;
    QString barista;
    QString profileNotes;
    QString debugLog;

    // Compact-JSON linked-bean snapshot at shot time ("" = unlinked, the
    // common free-text case) — Visualizer canonical or Bean Base sourced.
    // Snapshotted per shot so history stays accurate after the preset is
    // edited or deleted.
    QString beanBaseJson;

    // Coffee bag snapshot (bean-bag-inventory): see ShotRecord. "No bag" when
    // !bagIdIsSet(bagId) (bagId <= 0). Default -1.
    qint64 bagId = -1;
    QString frozenDate;
    QString defrostDate;

    // Recipe provenance (add-recipes): see ShotRecord. <= 0 = none.
    qint64 recipeId = -1;
    QString steamJson;

    // AI knowledge base ID (e.g. "d-flow", "blooming espresso") — computed at save time
    QString profileKbId;

    // Quality flags (computed at save time using ShotAnalysis helpers). When
    // pourTruncatedDetected fires, channelingDetected / grindIssueDetected
    // are forced to false; skipFirstFrameDetected is not. See the matching
    // comment on ShotRecord.
    bool channelingDetected = false;
    bool grindIssueDetected = false;
    bool skipFirstFrameDetected = false;
    bool pourTruncatedDetected = false;

    // Phase summaries JSON (per-phase metrics for UI display)
    QString phaseSummariesJson;

    // Pre-compressed sample data blob
    QByteArray compressedSamples;
    int sampleCount = 0;

    // Phase markers (pre-extracted from QVariantList)
    QList<HistoryPhaseMarker> phaseMarkers;
};
