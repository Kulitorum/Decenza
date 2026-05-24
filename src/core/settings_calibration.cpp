#include "settings_calibration.h"
#include "settings.h"
#include "../machine/sawprediction.h"
#include "../ble/scales/scaletypeids.h"  // ScaleTypeIds::normalizeScaleTypeId (dependency-free)

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QtMath>
#include <algorithm>

namespace {

// Minimum committed batch-medians before a (profile, scale) pair graduates from
// the global fallbacks (globalBootstrap / globalPool / scaleDefault) to its own
// per-pair model. Each median represents 3 SAW shots that survived the IQR
// dispersion gate, so 1 median = 3 shots minimum. Unlike flow calibration,
// SAW sends only a stop command and creates no feedback loop, so the model
// update does not alter conditions for the next shot — a single confirmed
// batch is sufficient signal. See docs/CLAUDE_MD/SAW_LEARNING.md.
constexpr qsizetype kSawMinMediansForGraduation = 1;

constexpr qsizetype kBatchSize = 3;
constexpr qsizetype kMaxPairHistory = 10;
constexpr double kBatchMaxDeviation = 1.5;    // seconds — single lag from batch median

QJsonObject parseFlowCalBatch(const QSettings& settings) {
    QJsonParseError parseError;
    QJsonObject map = QJsonDocument::fromJson(
        settings.value("calibration/flowCalBatch", "{}").toByteArray(),
        &parseError).object();
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "SettingsCalibration: corrupt flowCalBatch JSON:" << parseError.errorString();
        const_cast<QSettings&>(settings).setValue("calibration/flowCalBatch", "{}");
        return QJsonObject();
    }
    return map;
}

}  // namespace

SettingsCalibration::SettingsCalibration(Settings* owner, QObject* parent)
    : QObject(parent)
    , m_owner(owner)
    , m_settings("DecentEspresso", "DE1Qt")
{
}

QString SettingsCalibration::currentScaleType() const {
    // Normalize defensively — scaleType is stored as a canonical id, but this keeps
    // SAW keying correct even if a legacy display name slips through pre-migration.
    return ScaleTypeIds::normalizeScaleTypeId(m_owner ? m_owner->scaleType() : QStringLiteral("decent"));
}

void SettingsCalibration::invalidateCache() {
    m_sawHistoryCacheDirty = true;
    m_sawConvergedCache = -1;
    m_sawConvergedScaleType.clear();
    m_perProfileFlowCalCacheValid = false;
    m_perProfileSawHistoryCacheValid = false;
    m_perProfileSawBatchCacheValid = false;

    // Called after Settings::factoryReset() wipes the underlying QSettings
    // store — every cached value is now stale and the next read will return
    // the type's default. Fire all NOTIFY signals so any open QML binding
    // (e.g. SettingsCalibrationTab live during a reset) re-reads the new
    // baseline instead of showing the pre-reset value until the next change.
    emit flowCalibrationMultiplierChanged();
    emit autoFlowCalibrationChanged();
    emit perProfileFlowCalibrationChanged();
    emit sawLearnedLagChanged();
}

// Flow calibration

double SettingsCalibration::flowCalibrationMultiplier() const {
    return m_settings.value("calibration/flowMultiplier", 1.0).toDouble();
}

void SettingsCalibration::setFlowCalibrationMultiplier(double multiplier) {
    // Upper bound bumped 2.0 → 3.0 to match DE1 firmware v1337 (de1app parity).
    multiplier = qBound(0.35, multiplier, 3.0);
    if (qAbs(flowCalibrationMultiplier() - multiplier) > 0.001) {
        m_settings.setValue("calibration/flowMultiplier", multiplier);
        emit flowCalibrationMultiplierChanged();
    }
}

bool SettingsCalibration::autoFlowCalibration() const {
    return m_settings.value("calibration/autoFlowCalibration", true).toBool();
}

void SettingsCalibration::setAutoFlowCalibration(bool enabled) {
    if (autoFlowCalibration() != enabled) {
        m_settings.setValue("calibration/autoFlowCalibration", enabled);
        emit autoFlowCalibrationChanged();
    }
}

double SettingsCalibration::profileFlowCalibration(const QString& profileFilename) const {
    QJsonObject map = allProfileFlowCalibrations();
    if (map.contains(profileFilename)) {
        return map[profileFilename].toDouble();
    }
    return 0.0;
}

bool SettingsCalibration::setProfileFlowCalibration(const QString& profileFilename, double multiplier) {
    if (profileFilename.isEmpty()) {
        qWarning() << "SettingsCalibration: setProfileFlowCalibration called with empty profile filename";
        return false;
    }
    // Sanity bounds — persistence accepts [0.5, 2.7] to match the highest value the
    // runtime auto-cal algorithm can produce (kCalibrationMax on v1337+ firmware).
    // MainController::computeAutoFlowCalibration applies a tighter firmware-version-
    // dependent ceiling (1.8 on older firmware, 2.7 on v1337+). Persistence just
    // prevents obviously-corrupt values.
    if (multiplier < 0.5 || multiplier > 2.7) {
        qWarning() << "SettingsCalibration: rejecting per-profile flow calibration"
                   << multiplier << "for" << profileFilename << "(outside [0.5, 2.7])";
        return false;
    }
    QJsonObject map = allProfileFlowCalibrations();
    map[profileFilename] = multiplier;
    savePerProfileFlowCalMap(map);
    return true;
}

void SettingsCalibration::clearProfileFlowCalibration(const QString& profileFilename) {
    if (profileFilename.isEmpty()) {
        qWarning() << "SettingsCalibration: clearProfileFlowCalibration called with empty profile filename";
        return;
    }
    QJsonObject map = allProfileFlowCalibrations();
    map.remove(profileFilename);
    savePerProfileFlowCalMap(map);
    // Clear any pending batch ideals — they were computed at the old C value
    clearFlowCalPendingIdeals(profileFilename);
}

double SettingsCalibration::effectiveFlowCalibration(const QString& profileFilename) const {
    if (autoFlowCalibration()) {
        double perProfile = profileFlowCalibration(profileFilename);
        if (perProfile > 0.0) {
            return perProfile;
        }
    }
    return flowCalibrationMultiplier();
}

bool SettingsCalibration::hasProfileFlowCalibration(const QString& profileFilename) const {
    if (!autoFlowCalibration()) return false;
    QJsonObject map = allProfileFlowCalibrations();
    return map.contains(profileFilename);
}

QJsonObject SettingsCalibration::allProfileFlowCalibrations() const {
    if (m_perProfileFlowCalCacheValid)
        return m_perProfileFlowCalCache;

    QJsonParseError parseError;
    QJsonObject map = QJsonDocument::fromJson(
        m_settings.value("calibration/perProfileFlow", "{}").toByteArray(),
        &parseError).object();
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "SettingsCalibration: corrupt perProfileFlow JSON:" << parseError.errorString()
                   << "- raw data:" << m_settings.value("calibration/perProfileFlow").toByteArray().left(200)
                   << "- per-profile flow calibrations lost";
        // Clear the corrupt data so it doesn't persist and cause repeated warnings
        const_cast<QSettings&>(m_settings).setValue("calibration/perProfileFlow", "{}");
        map = QJsonObject();
    }
    // INVARIANT: All modifications to "calibration/perProfileFlow" in QSettings
    // MUST go through savePerProfileFlowCalMap() to maintain cache consistency.
    m_perProfileFlowCalCache = map;
    m_perProfileFlowCalCacheValid = true;
    return m_perProfileFlowCalCache;
}

void SettingsCalibration::savePerProfileFlowCalMap(const QJsonObject& map) {
    m_settings.setValue("calibration/perProfileFlow", QJsonDocument(map).toJson(QJsonDocument::Compact));
    m_perProfileFlowCalCache = map;
    m_perProfileFlowCalCacheValid = true;
    m_perProfileFlowCalVersion++;
    emit perProfileFlowCalibrationChanged();
}

void SettingsCalibration::resetAllProfileFlowCalibrations() {
    savePerProfileFlowCalMap(QJsonObject());
}

// Auto flow calibration batch accumulator

QVector<double> SettingsCalibration::flowCalPendingIdeals(const QString& profileFilename) const {
    QJsonObject map = parseFlowCalBatch(m_settings);
    QVector<double> result;
    QJsonArray arr = map.value(profileFilename).toArray();
    for (const auto& v : arr)
        result.append(v.toDouble());
    return result;
}

void SettingsCalibration::appendFlowCalPendingIdeal(const QString& profileFilename, double ideal) {
    QJsonObject map = parseFlowCalBatch(m_settings);
    QJsonArray arr = map.value(profileFilename).toArray();
    arr.append(ideal);
    map[profileFilename] = arr;
    m_settings.setValue("calibration/flowCalBatch", QJsonDocument(map).toJson(QJsonDocument::Compact));
}

void SettingsCalibration::clearFlowCalPendingIdeals(const QString& profileFilename) {
    QJsonObject map = parseFlowCalBatch(m_settings);
    map.remove(profileFilename);
    m_settings.setValue("calibration/flowCalBatch", QJsonDocument(map).toJson(QJsonDocument::Compact));
}

// SAW (Stop-at-Weight) learning

// Returns average lag for display in QML settings (calculated from stored drip/flow)
double SettingsCalibration::sawLearnedLag() const {
    ensureSawCacheLoaded();

    const QJsonArray& arr = m_sawHistoryCache;
    if (arr.isEmpty()) {
        return 1.5;  // Default
    }

    QString currentScale = currentScaleType();
    double sumLag = 0;
    int count = 0;

    for (qsizetype i = arr.size() - 1; i >= 0 && count < 3; --i) {
        QJsonObject obj = arr[i].toObject();
        if (obj["scale"].toString() == currentScale) {
            if (obj.contains("drip") && obj.contains("flow")) {
                double drip = obj["drip"].toDouble();
                double flow = obj["flow"].toDouble();
                if (flow > 0.5) {
                    sumLag += drip / flow;
                    count++;
                }
            } else if (obj.contains("lag")) {
                // Old format
                sumLag += obj["lag"].toDouble();
                count++;
            }
        }
    }

    return count > 0 ? sumLag / count : 1.5;
}

void SettingsCalibration::ensureSawCacheLoaded() const {
    if (!m_sawHistoryCacheDirty) return;
    QByteArray data = m_settings.value("saw/learningHistory").toByteArray();
    if (data.isEmpty()) {
        m_sawHistoryCache = QJsonArray();
    } else {
        QJsonDocument doc = QJsonDocument::fromJson(data);
        m_sawHistoryCache = doc.array();
    }
    m_sawHistoryCacheDirty = false;
    m_sawConvergedCache = -1;  // Invalidate convergence cache too
}

bool SettingsCalibration::isSawConverged(QString scaleType) const {
    scaleType = ScaleTypeIds::normalizeScaleTypeId(scaleType);
    ensureSawCacheLoaded();

    // Return cached result if available and for the same scale
    if (m_sawConvergedCache >= 0 && m_sawConvergedScaleType == scaleType) {
        return m_sawConvergedCache == 1;
    }

    const QJsonArray& arr = m_sawHistoryCache;
    if (arr.isEmpty()) {
        m_sawConvergedCache = 0;
        m_sawConvergedScaleType = scaleType;
        return false;
    }

    // Collect |overshoot| from last 5 entries for this scale that have overshoot data
    QVector<double> overshoots;
    for (qsizetype i = arr.size() - 1; i >= 0 && overshoots.size() < 5; --i) {
        QJsonObject obj = arr[i].toObject();
        if (obj["scale"].toString() == scaleType && obj.contains("overshoot")) {
            overshoots.append(qAbs(obj["overshoot"].toDouble()));
        }
    }

    // Converged = at least 3 entries with avg |overshoot| < 1.5g
    if (overshoots.size() < 3) {
        m_sawConvergedCache = 0;
        m_sawConvergedScaleType = scaleType;
        return false;
    }

    double sum = 0;
    for (double v : overshoots) sum += v;
    bool converged = (sum / overshoots.size()) < 1.5;

    // Divergence detector: if last 3 signed overshoots are all >1g in the same
    // direction, the prediction is systematically off (bean/grind change) — force
    // adaptation mode without requiring manual reset.
    if (converged && overshoots.size() >= 3) {
        QVector<double> signedOvershoots;
        for (qsizetype i = arr.size() - 1; i >= 0 && signedOvershoots.size() < 3; --i) {
            QJsonObject obj = arr[i].toObject();
            if (obj["scale"].toString() == scaleType && obj.contains("overshoot")) {
                signedOvershoots.append(obj["overshoot"].toDouble());
            }
        }
        if (signedOvershoots.size() >= 3) {
            bool allPositive = true, allNegative = true;
            for (double v : signedOvershoots) {
                if (v <= 1.0) allPositive = false;
                if (v >= -1.0) allNegative = false;
            }
            if (allPositive || allNegative) {
                qDebug() << "[SAW] Divergence detected: last 3 overshoots all"
                         << (allPositive ? "positive" : "negative") << "- forcing adaptation mode";
                converged = false;
            }
        }
    }

    m_sawConvergedCache = converged ? 1 : 0;
    m_sawConvergedScaleType = scaleType;
    return converged;
}

double SettingsCalibration::getExpectedDrip(double currentFlowRate) const {
    ensureSawCacheLoaded();

    // Read scale type once — consistent across all fallback paths.
    const QString currentScale = currentScaleType();

    const QJsonArray& arr = m_sawHistoryCache;
    if (arr.isEmpty()) {
        // No history at all — use scale-specific sensor lag as first-shot default.
        // Formula: flow × (sensor_lag + 0.1s DE1 machine lag), capped at 8g.
        // Matches de1app's first-shot behaviour (lag_time_estimation=0 before learning).
        return qMin(currentFlowRate * (sensorLag(currentScale) + 0.1), 8.0);
    }

    // Check convergence state to determine adaptive parameters
    bool converged = isSawConverged(currentScale);
    qsizetype maxEntries = converged ? 12 : 8;
    double recencyMax = 10.0;
    double recencyMin = converged ? 3.0 : 1.0;  // Steeper recency = faster adaptation

    // Filter to current scale type and collect recent entries
    struct Entry { double drip; double flow; };
    QVector<Entry> entries;

    for (qsizetype i = arr.size() - 1; i >= 0 && entries.size() < maxEntries; --i) {
        QJsonObject obj = arr[i].toObject();
        if (obj["scale"].toString() == currentScale) {
            // Support both old format (lag) and new format (drip, flow)
            if (obj.contains("drip")) {
                entries.append({obj["drip"].toDouble(), obj["flow"].toDouble()});
            } else if (obj.contains("lag")) {
                // Convert old lag format: drip = lag * flow (approximate)
                double lag = obj["lag"].toDouble();
                double flow = 4.0;  // Assume average flow for old entries
                entries.append({lag * flow, flow});
            }
        }
    }

    if (entries.isEmpty()) {
        return qMin(currentFlowRate * (sensorLag(currentScale) + 0.1), 8.0);  // No entries for this scale type
    }

    // Math is shared with WeightProcessor and getExpectedDripFor via
    // SawPrediction::weightedDripPrediction so σ stays in lockstep.
    QVector<double> drips, flows;
    drips.reserve(entries.size());
    flows.reserve(entries.size());
    for (const Entry& e : std::as_const(entries)) {
        drips.append(e.drip);
        flows.append(e.flow);
    }

    const double prediction = SawPrediction::weightedDripPrediction(
        drips, flows, currentFlowRate, recencyMax, recencyMin);

    if (qIsNaN(prediction)) {
        // All entries have very different flow rates — fall back to sensor-lag default.
        return qMin(currentFlowRate * (sensorLag(currentScale) + 0.1), 8.0);
    }
    return prediction;
}

QList<QPair<double, double>> SettingsCalibration::sawLearningEntries(QString scaleType, int maxEntries) const {
    scaleType = ScaleTypeIds::normalizeScaleTypeId(scaleType);
    ensureSawCacheLoaded();
    QList<QPair<double, double>> result;
    for (qsizetype i = m_sawHistoryCache.size() - 1; i >= 0 && result.size() < maxEntries; --i) {
        QJsonObject obj = m_sawHistoryCache[i].toObject();
        if (obj["scale"].toString() == scaleType) {
            if (obj.contains("drip")) {
                result.append({obj["drip"].toDouble(), obj["flow"].toDouble()});
            } else if (obj.contains("lag")) {
                // Convert old lag format: drip ≈ lag * typical_flow
                double lag = obj["lag"].toDouble();
                result.append({lag * 4.0, 4.0});
            }
        }
    }
    return result;
}

double SettingsCalibration::sensorLag(const QString& scaleType)
{
    // Per-scale sensor lag, keyed by canonical type-id (from de1app device_scale.tcl).
    // Used as the first-shot SAW default before adaptive learning has data. The +0.1s
    // added at call sites is the DE1 machine-side stop-command lag (separate from BLE
    // round-trip), keeping this value scale-specific only. Normalize first so a legacy
    // display name (e.g. "Decent Scale") still resolves.
    const QString id = ScaleTypeIds::normalizeScaleTypeId(scaleType);
    if (id == "bookoo")           return 0.50;
    if (id == "acaia")            return 0.69;
    if (id == "acaiapyxis")       return 0.69;  // Same Acaia BLE protocol
    if (id == "felicita")         return 0.50;
    if (id == "atomheart_eclair") return 0.50;
    if (id == "hiroiajimmy")      return 0.25;
    if (id == "decent")           return 0.38;  // also the QSettings default before pairing
    if (id == "skale")            return 0.38;
    if (id == "decent-wifi")      return 0.38;  // WiFi transport of the Half Decent Scale
    if (id == "decent-usb")       return 0.38;  // USB transport of the Half Decent Scale
    qWarning() << "[SAW] Unknown scale type for sensorLag:" << scaleType << "- using default 0.38s";
    return 0.38;  // de1app default for unknown/unlisted scales
}

void SettingsCalibration::addSawLearningPoint(double drip, double flowRate, QString scaleType,
                                              double overshoot, const QString& profileFilename) {
    // Key SAW learning on the canonical type-id (rename-stable). Covers both the
    // per-pair path (addSawPerPairEntry) and the legacy global-pool path below.
    scaleType = ScaleTypeIds::normalizeScaleTypeId(scaleType);

    // Validate physical constraints (scale glitches can produce negative values)
    if (drip < 0 || flowRate < 0) {
        qWarning() << "[SAW] Invalid learning point rejected: drip=" << drip << "flow=" << flowRate;
        return;
    }

    // Reject physically implausible entries: implied lag > 4s is beyond any real BLE
    // scale (BLE round-trip + machine stop + final drip ≈ 3.5s worst case).
    // The flowRate > 0.5 guard prevents division-by-near-zero making the ratio meaningless
    // at very low flow (e.g. 0.1 g/s would flag a 0.39g drip as "too high").
    if (flowRate > 0.5 && drip / flowRate > 4.0) {
        qWarning() << "[SAW] Implied lag too high (" << drip / flowRate
                   << "s), skipping learning: drip=" << drip << "flow=" << flowRate;
        return;
    }

    // Outlier rejection: when converged, skip learning points that deviate too far.
    // Skipped when overshoot < -6g (auto-reset candidate): the model may be systematically
    // wrong and must accept the new baseline rather than defending the stale converged model.
    bool isAutoResetCandidate = (overshoot < -6.0);
    if (!isAutoResetCandidate && isSawConverged(scaleType)) {
        double expectedDrip = getExpectedDripFor(profileFilename, scaleType, flowRate);
        double threshold = qMax(3.0, expectedDrip);  // Reject if deviation exceeds expected drip (or 3g floor)
        if (qAbs(drip - expectedDrip) > threshold) {
            qWarning() << "[SAW] Outlier rejected: drip=" << drip
                       << "g expected=" << expectedDrip
                       << "g threshold=" << threshold
                       << "(converged, deviation too high)";
            return;
        }
    }

    // When called with a profile filename, route through the per-(profile, scale) batch
    // accumulator. The pending batch holds 3 shots before committing the median to the
    // per-pair history AND the global pool — this reduces churn from individual shots
    // and provides outlier rejection via the median + per-element deviation check. See
    // AUTO_FLOW_CALIBRATION for the same pattern applied to flow cal.
    if (!profileFilename.isEmpty()) {
        addSawPerPairEntry(drip, flowRate, scaleType, overshoot, profileFilename);
        return;
    }

    // Legacy path (profile unknown): append directly to the global pool. Preserves
    // existing behaviour for callers that have not been updated to pass a profile.
    QByteArray data = m_settings.value("saw/learningHistory").toByteArray();
    QJsonArray arr;
    if (!data.isEmpty()) {
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (doc.isArray()) {
            arr = doc.array();
        } else {
            qWarning() << "[SAW] Learning history corrupted, starting fresh:" << parseError.errorString();
        }
    }

    // Auto-reset: if this shot stopped 6g+ early (current) AND the most recent
    // prior entry for this scale type also stopped 6g+ early, the learning is
    // stuck producing too-aggressive thresholds. Clear history and start fresh
    // so the new entry becomes the sole baseline. Entries from other scale types
    // are skipped when searching backwards — "consecutive" means consecutive for
    // this scale type only.
    // NOTE: execution always falls through to the append+save below — do NOT add
    // an early return here, or the reset will wipe history without saving anything.
    if (isAutoResetCandidate) {
        bool prevAlsoEarly = false;
        for (qsizetype i = arr.size() - 1; i >= 0; --i) {
            QJsonObject obj = arr[i].toObject();
            if (obj["scale"].toString() == scaleType) {
                prevAlsoEarly = (obj["overshoot"].toDouble() < -6.0);
                break;  // Only check the most recent entry for this scale type
            }
        }
        if (prevAlsoEarly) {
            qWarning() << "[SAW] 2nd consecutive early stop for" << scaleType
                       << "- resetting learning (both shots overshoot <-6g)";
            // Remove all entries for this scale type, preserving other scales.
            // The new entry will be appended below and becomes the fresh baseline.
            QJsonArray filtered;
            for (int i = 0; i < arr.size(); ++i) {
                if (arr[i].toObject()["scale"].toString() != scaleType)
                    filtered.append(arr[i]);
            }
            arr = filtered;
        }
    }

    // Create new entry with drip, flow, and overshoot
    QJsonObject entry;
    entry["drip"] = drip;          // grams that came after stop command
    entry["flow"] = flowRate;      // flow rate when stop was triggered
    entry["scale"] = scaleType;
    entry["overshoot"] = overshoot; // grams over/under target (for convergence detection)
    entry["ts"] = QDateTime::currentSecsSinceEpoch();
    arr.append(entry);

    // Trim to max 50 entries (converged mode uses up to 20, keep extra history)
    while (arr.size() > 50) {
        arr.removeFirst();
    }

    m_settings.setValue("saw/learningHistory", QJsonDocument(arr).toJson());
    m_sawHistoryCacheDirty = true;
    m_sawConvergedCache = -1;
    emit sawLearnedLagChanged();
}

void SettingsCalibration::resetSawLearning() {
    m_settings.remove("saw/learningHistory");
    m_settings.remove("saw/perProfileHistory");
    m_settings.remove("saw/perProfileBatch");
    m_settings.remove("saw/globalBootstrapLag");
    m_sawHistoryCacheDirty = true;
    m_sawConvergedCache = -1;
    m_perProfileSawHistoryCacheValid = false;
    m_perProfileSawBatchCacheValid = false;
    qDebug() << "[SAW] reset all SAW learning";
    emit sawLearnedLagChanged();

    // Cross-domain: hot-water SAW offset reset is performed by SettingsBrew
    // via the connect() wired up in Settings::Settings().
    emit sawLearningResetRequested();
}

void SettingsCalibration::resetSawLearningForProfile(const QString& profileFilename, const QString& scaleType) {
    if (profileFilename.isEmpty()) {
        qWarning() << "[SAW] resetSawLearningForProfile called with empty profile";
        return;
    }
    const QString key = sawPairKey(profileFilename, scaleType);
    QJsonObject historyMap = loadPerProfileSawHistoryMap();
    QJsonObject batchMap = loadPerProfileSawBatchMap();
    bool changed = false;
    if (historyMap.contains(key)) {
        historyMap.remove(key);
        savePerProfileSawHistoryMap(historyMap);
        changed = true;
    }
    if (batchMap.contains(key)) {
        batchMap.remove(key);
        savePerProfileSawBatchMap(batchMap);
        changed = true;
    }
    if (changed) {
        qDebug() << "[SAW] reset perProfileHistory for" << key;
        emit sawLearnedLagChanged();
    }
}

// ---- per-(profile, scale) helpers ----

QString SettingsCalibration::sawPairKey(const QString& profileFilename, const QString& scaleType) {
    // Key on the canonical type-id so per-(profile, scale) reads/writes stay in sync
    // regardless of whether the caller passed an id or a legacy display name. This is
    // the single choke point for perProfileSawHistory / sawPendingBatch /
    // resetSawLearningForProfile / addSawPerPairEntry.
    return profileFilename + QStringLiteral("::") + ScaleTypeIds::normalizeScaleTypeId(scaleType);
}

QJsonObject SettingsCalibration::loadPerProfileSawHistoryMap() const {
    if (m_perProfileSawHistoryCacheValid) return m_perProfileSawHistoryCache;
    QJsonParseError parseError;
    QJsonObject map = QJsonDocument::fromJson(
        m_settings.value("saw/perProfileHistory", "{}").toByteArray(),
        &parseError).object();
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "[SAW] corrupt perProfileHistory JSON:" << parseError.errorString()
                   << "- per-profile SAW history lost";
        const_cast<QSettings&>(m_settings).setValue("saw/perProfileHistory", "{}");
        map = QJsonObject();
    }
    m_perProfileSawHistoryCache = map;
    m_perProfileSawHistoryCacheValid = true;
    return m_perProfileSawHistoryCache;
}

void SettingsCalibration::savePerProfileSawHistoryMap(const QJsonObject& map) {
    m_settings.setValue("saw/perProfileHistory",
                        QJsonDocument(map).toJson(QJsonDocument::Compact));
    m_perProfileSawHistoryCache = map;
    m_perProfileSawHistoryCacheValid = true;
}

QJsonObject SettingsCalibration::loadPerProfileSawBatchMap() const {
    if (m_perProfileSawBatchCacheValid) return m_perProfileSawBatchCache;
    QJsonParseError parseError;
    QJsonObject map = QJsonDocument::fromJson(
        m_settings.value("saw/perProfileBatch", "{}").toByteArray(),
        &parseError).object();
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "[SAW] corrupt perProfileBatch JSON:" << parseError.errorString();
        const_cast<QSettings&>(m_settings).setValue("saw/perProfileBatch", "{}");
        map = QJsonObject();
    }
    m_perProfileSawBatchCache = map;
    m_perProfileSawBatchCacheValid = true;
    return m_perProfileSawBatchCache;
}

void SettingsCalibration::savePerProfileSawBatchMap(const QJsonObject& map) {
    m_settings.setValue("saw/perProfileBatch",
                        QJsonDocument(map).toJson(QJsonDocument::Compact));
    m_perProfileSawBatchCache = map;
    m_perProfileSawBatchCacheValid = true;
}

QJsonArray SettingsCalibration::perProfileSawHistory(const QString& profileFilename, const QString& scaleType) const {
    return loadPerProfileSawHistoryMap().value(sawPairKey(profileFilename, scaleType)).toArray();
}

QJsonObject SettingsCalibration::allPerProfileSawHistory() const {
    return loadPerProfileSawHistoryMap();
}

QJsonArray SettingsCalibration::sawPendingBatch(const QString& profileFilename, const QString& scaleType) const {
    return loadPerProfileSawBatchMap().value(sawPairKey(profileFilename, scaleType)).toArray();
}

double SettingsCalibration::globalSawBootstrapLag(const QString& scaleType) const {
    const QString key = QStringLiteral("saw/globalBootstrapLag/") + ScaleTypeIds::normalizeScaleTypeId(scaleType);
    return m_settings.value(key, 0.0).toDouble();
}

void SettingsCalibration::setGlobalSawBootstrapLag(const QString& scaleType, double lag) {
    const QString key = QStringLiteral("saw/globalBootstrapLag/") + ScaleTypeIds::normalizeScaleTypeId(scaleType);
    m_settings.setValue(key, lag);
}

// ---- per-(profile, scale) read path ----

QString SettingsCalibration::sawModelSource(const QString& profileFilename, QString scaleType) const {
    scaleType = ScaleTypeIds::normalizeScaleTypeId(scaleType);
    if (!profileFilename.isEmpty()) {
        QJsonArray pairHistory = perProfileSawHistory(profileFilename, scaleType);
        if (pairHistory.size() >= kSawMinMediansForGraduation) return QStringLiteral("perProfile");
    }
    if (globalSawBootstrapLag(scaleType) > 0.0) return QStringLiteral("globalBootstrap");
    ensureSawCacheLoaded();
    for (const auto& v : std::as_const(m_sawHistoryCache)) {
        if (v.toObject().value("scale").toString() == scaleType) return QStringLiteral("globalPool");
    }
    return QStringLiteral("scaleDefault");
}

QList<QPair<double, double>> SettingsCalibration::sawLearningEntriesFor(const QString& profileFilename,
                                                                       const QString& scaleType,
                                                                       int maxEntries) const {
    QList<QPair<double, double>> result;
    if (!profileFilename.isEmpty()) {
        QJsonArray pairHistory = perProfileSawHistory(profileFilename, scaleType);
        if (pairHistory.size() >= kSawMinMediansForGraduation) {
            for (qsizetype i = pairHistory.size() - 1; i >= 0 && result.size() < maxEntries; --i) {
                QJsonObject obj = pairHistory[i].toObject();
                if (obj.contains("drip")) {
                    result.append({obj["drip"].toDouble(), obj["flow"].toDouble()});
                }
            }
            if (!result.isEmpty()) return result;
        }
    }
    return sawLearningEntries(scaleType, maxEntries);
}

double SettingsCalibration::sawLearnedLagFor(const QString& profileFilename, const QString& scaleType) const {
    if (!profileFilename.isEmpty()) {
        QJsonArray pairHistory = perProfileSawHistory(profileFilename, scaleType);
        if (pairHistory.size() >= kSawMinMediansForGraduation) {
            double sumLag = 0;
            qsizetype count = 0;
            for (qsizetype i = pairHistory.size() - 1; i >= 0 && count < 3; --i) {
                QJsonObject obj = pairHistory[i].toObject();
                double drip = obj.value("drip").toDouble();
                double flow = obj.value("flow").toDouble();
                if (flow > 0.5) {
                    sumLag += drip / flow;
                    ++count;
                }
            }
            if (count > 0) return sumLag / count;
        }
    }
    double bootstrap = globalSawBootstrapLag(scaleType);
    if (bootstrap > 0.0) return bootstrap;
    return sawLearnedLag();
}

double SettingsCalibration::getExpectedDripFor(const QString& profileFilename,
                                               const QString& scaleType,
                                               double currentFlowRate) const {
    if (!profileFilename.isEmpty()) {
        QJsonArray pairHistory = perProfileSawHistory(profileFilename, scaleType);
        if (pairHistory.size() >= kSawMinMediansForGraduation) {
            // Same flow-similarity kernel as the global getExpectedDrip(), but
            // recencyMin is fixed at 3.0 — per-pair history only kicks in after
            // graduation (≥ kSawMinMediansForGraduation committed medians).
            // Reads at most 3 recent entries, matching the per-pair read window.
            struct Entry { double drip; double flow; };
            QVector<Entry> entries;
            for (qsizetype i = pairHistory.size() - 1; i >= 0 && entries.size() < 3; --i) {
                QJsonObject obj = pairHistory[i].toObject();
                if (obj.contains("drip")) entries.append({obj["drip"].toDouble(), obj["flow"].toDouble()});
            }
            if (!entries.isEmpty()) {
                QVector<double> drips, flows;
                drips.reserve(entries.size());
                flows.reserve(entries.size());
                for (const Entry& e : std::as_const(entries)) {
                    drips.append(e.drip);
                    flows.append(e.flow);
                }
                const double prediction = SawPrediction::weightedDripPrediction(
                    drips, flows, currentFlowRate,
                    /*recencyMax=*/10.0, /*recencyMin=*/3.0);
                if (!qIsNaN(prediction)) {
                    return prediction;
                }
            }
        }
    }
    double bootstrap = globalSawBootstrapLag(scaleType);
    if (bootstrap > 0.0) {
        return qMin(currentFlowRate * bootstrap, 8.0);
    }
    return qMin(currentFlowRate * (sensorLag(scaleType) + 0.1), 8.0);
}

// ---- per-pair batch accumulator + commit ----

void SettingsCalibration::addSawPerPairEntry(double drip, double flowRate, const QString& scaleType,
                                             double overshoot, const QString& profileFilename) {
    const QString key = sawPairKey(profileFilename, scaleType);

    // 1. Append entry to pending batch
    QJsonObject batchMap = loadPerProfileSawBatchMap();
    QJsonArray batch = batchMap.value(key).toArray();
    QJsonObject entry;
    entry["drip"] = drip;
    entry["flow"] = flowRate;
    entry["overshoot"] = overshoot;
    entry["scale"] = scaleType;
    entry["profile"] = profileFilename;
    entry["ts"] = QDateTime::currentSecsSinceEpoch();
    batch.append(entry);

    if (batch.size() < kBatchSize) {
        batchMap[key] = batch;
        savePerProfileSawBatchMap(batchMap);
        const double lag = (flowRate > 0.5) ? drip / flowRate : 0.0;
        qDebug() << "[SAW] accumulated drip=" << drip << "flow=" << flowRate
                 << "for" << key
                 << "(" << batch.size() << "/" << kBatchSize << ") lag=" << lag;
        return;
    }

    // 2. Batch full — compute medians of drip / flow / overshoot, plus IQR of lags.
    QVector<double> drips, flows, overs, lags;
    drips.reserve(batch.size()); flows.reserve(batch.size());
    overs.reserve(batch.size()); lags.reserve(batch.size());
    for (const auto& v : std::as_const(batch)) {
        QJsonObject o = v.toObject();
        drips.append(o["drip"].toDouble());
        flows.append(o["flow"].toDouble());
        overs.append(o["overshoot"].toDouble());
        if (o["flow"].toDouble() > 0.5) lags.append(o["drip"].toDouble() / o["flow"].toDouble());
    }

    auto medianOf = [](QVector<double> v) -> double {
        if (v.isEmpty()) return 0.0;
        std::sort(v.begin(), v.end());
        const qsizetype n = v.size();
        return (n % 2 == 0) ? (v[n / 2 - 1] + v[n / 2]) / 2.0 : v[n / 2];
    };
    const double medianDrip = medianOf(drips);
    const double medianFlow = medianOf(flows);
    const double medianOver = medianOf(overs);
    const double medianLag = (medianFlow > 0.5) ? medianDrip / medianFlow : 0.0;

    // 3. Outlier check: reject batch if any lag deviates too far from the median.
    //    IQR gating is not used here because kBatchSize=3 produces too few values
    //    for a meaningful IQR estimate; per-element deviation is sufficient.
    QString rejectReason;
    for (double l : std::as_const(lags)) {
        double dev = qAbs(l - medianLag);
        if (dev > kBatchMaxDeviation) {
            rejectReason = QString("outlier lag=%1 deviates %2s > %3s from median")
                               .arg(l).arg(dev).arg(kBatchMaxDeviation);
            break;
        }
    }
    if (!rejectReason.isEmpty()) {
        qWarning() << "[SAW] batch rejected —" << qPrintable(rejectReason)
                   << "median_lag=" << medianLag << "for" << key << "— dropping batch";
        batchMap.remove(key);
        savePerProfileSawBatchMap(batchMap);
        return;
    }

    // 4. Auto-reset: 2nd consecutive batch with median overshoot < -6g → wipe pair history,
    //    let the new median be the sole baseline. The legacy single-shot path triggers on
    //    2 consecutive bad shots; here, since each median represents 3 shots, the
    //    auto-reset trigger is effectively 6 consecutive bad shots — intentional
    //    debouncing for the batched update model. (Distinct from the graduation
    //    threshold defined at the top of this section.)
    QJsonObject historyMap = loadPerProfileSawHistoryMap();
    QJsonArray pairHistory = historyMap.value(key).toArray();
    if (medianOver < -6.0 && !pairHistory.isEmpty()) {
        QJsonObject lastMedian = pairHistory.last().toObject();
        if (lastMedian["overshoot"].toDouble() < -6.0) {
            qWarning() << "[SAW] 2nd consecutive overshoot<-6g for" << key
                       << "— clearing committed history";
            pairHistory = QJsonArray();
        }
    }

    // 5. Commit median to per-pair history.
    QJsonObject medianEntry;
    medianEntry["drip"] = medianDrip;
    medianEntry["flow"] = medianFlow;
    medianEntry["overshoot"] = medianOver;
    medianEntry["scale"] = scaleType;
    medianEntry["profile"] = profileFilename;
    medianEntry["ts"] = QDateTime::currentSecsSinceEpoch();
    medianEntry["batchSize"] = batch.size();
    pairHistory.append(medianEntry);
    while (pairHistory.size() > kMaxPairHistory) pairHistory.removeFirst();
    historyMap[key] = pairHistory;
    savePerProfileSawHistoryMap(historyMap);

    // 6. Mirror the median into the global pool so isSawConverged + the legacy
    //    bootstrap path keep working. Trim to 50 (existing cap).
    QByteArray data = m_settings.value("saw/learningHistory").toByteArray();
    QJsonArray pool;
    if (!data.isEmpty()) {
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (doc.isArray()) pool = doc.array();
    }
    pool.append(medianEntry);
    while (pool.size() > 50) pool.removeFirst();
    m_settings.setValue("saw/learningHistory", QJsonDocument(pool).toJson());
    m_sawHistoryCacheDirty = true;
    m_sawConvergedCache = -1;

    // 7. Clear pending batch.
    batchMap.remove(key);
    savePerProfileSawBatchMap(batchMap);

    qDebug() << "[SAW] committed median lag=" << medianLag
             << "(drip=" << medianDrip << "flow=" << medianFlow << ")"
             << "for" << key
             << "— n_medians=" << pairHistory.size();

    // 8. Recompute global bootstrap lag for this scale type so other (profile, scale)
    //    pairs with no per-pair history can use it as their first-shot default.
    recomputeGlobalSawBootstrap(scaleType);

    emit sawLearnedLagChanged();
}

void SettingsCalibration::recomputeGlobalSawBootstrap(const QString& scaleType) {
    // Bootstrap is a cold-start prior for *new* pairs: a single committed batch median
    // (3 real shots) is already more informative than the static sensorLag() constant,
    // so any pair with at least one committed median contributes. The IQR fence below
    // protects against under-trained outliers if many pairs accumulate. Pairs that have
    // crossed the per-profile graduation threshold (kSawMinMediansForGraduation
    // medians) for the read path are a stricter bar handled in sawLearnedLagFor /
    // sawModelSource.
    QJsonObject map = loadPerProfileSawHistoryMap();
    QVector<double> lags;
    for (auto it = map.begin(); it != map.end(); ++it) {
        QJsonArray pairHistory = it.value().toArray();
        if (pairHistory.isEmpty()) continue;
        // Median entries record their scale; only include this scale's pairs.
        if (pairHistory.last().toObject().value("scale").toString() != scaleType) continue;
        // Use the last committed median lag as that pair's representative.
        QJsonObject last = pairHistory.last().toObject();
        double drip = last.value("drip").toDouble();
        double flow = last.value("flow").toDouble();
        if (flow > 0.5) lags.append(drip / flow);
    }
    if (lags.size() < 2) {
        // Need at least 2 contributing pairs to compute a useful bootstrap median.
        return;
    }
    std::sort(lags.begin(), lags.end());
    // IQR fence (1.5x IQR from Q1/Q3) — same outlier-removal as flow cal's global median.
    if (lags.size() >= 4) {
        const qsizetype n = lags.size();
        const double q1 = lags[n / 4];
        const double q3 = lags[3 * n / 4];
        const double iqr = q3 - q1;
        const double lower = q1 - 1.5 * iqr;
        const double upper = q3 + 1.5 * iqr;
        QVector<double> filtered;
        for (double v : std::as_const(lags)) if (v >= lower && v <= upper) filtered.append(v);
        if (filtered.size() >= 2) lags = filtered;
    }
    const qsizetype n = lags.size();
    const double median = (n % 2 == 0) ? (lags[n / 2 - 1] + lags[n / 2]) / 2.0 : lags[n / 2];
    setGlobalSawBootstrapLag(scaleType, median);
    qDebug() << "[SAW] global bootstrap lag for" << scaleType
             << "updated to" << median << "(median of" << n << "pairs with committed history)";
}

void SettingsCalibration::migrateScaleTypeIds() {
    // (A) Global pool: rewrite each entry's "scale" field to its canonical id.
    {
        const QByteArray data = m_settings.value("saw/learningHistory").toByteArray();
        if (!data.isEmpty()) {
            QJsonArray arr = QJsonDocument::fromJson(data).array();
            bool changed = false;
            for (qsizetype i = 0; i < arr.size(); ++i) {
                QJsonObject o = arr[i].toObject();
                const QString s = o.value("scale").toString();
                const QString id = ScaleTypeIds::normalizeScaleTypeId(s);
                if (id != s) { o["scale"] = id; arr[i] = o; changed = true; }
            }
            if (changed) m_settings.setValue("saw/learningHistory", QJsonDocument(arr).toJson());
        }
    }

    // Rewrite a "profile::scaleType" map: keys + per-entry "scale" -> ids, merging
    // colliding buckets (concatenate migrated-from before existing, keep newest `trim`).
    // Collisions require a pre-existing id bucket for a scale whose legacy data was
    // display-name-keyed — vanishingly rare — so exact post-merge order is unimportant;
    // the point is to lose no data.
    auto migrateMap = [](const QJsonObject& in, qsizetype trim) -> QJsonObject {
        QJsonObject out;
        for (auto it = in.begin(); it != in.end(); ++it) {
            const QString key = it.key();
            QString newKey = key;
            const qsizetype sep = key.lastIndexOf(QStringLiteral("::"));
            if (sep >= 0) {
                newKey = key.left(sep) + QStringLiteral("::")
                       + ScaleTypeIds::normalizeScaleTypeId(key.mid(sep + 2));
            }
            QJsonArray arr = it.value().toArray();
            for (qsizetype i = 0; i < arr.size(); ++i) {
                QJsonObject o = arr[i].toObject();
                const QString s = o.value("scale").toString();
                const QString id = ScaleTypeIds::normalizeScaleTypeId(s);
                if (id != s) { o["scale"] = id; arr[i] = o; }
            }
            if (!out.contains(newKey)) {
                out[newKey] = arr;
            } else {
                QJsonArray combined;
                for (const auto& v : std::as_const(arr)) combined.append(v);                  // migrated-from (older)
                const QJsonArray existing = out.value(newKey).toArray();
                for (const auto& v : std::as_const(existing)) combined.append(v);              // existing (newer)
                while (combined.size() > trim) combined.removeFirst();
                out[newKey] = combined;
            }
        }
        return out;
    };

    // (B) Per-pair committed history.
    {
        const QJsonObject map = loadPerProfileSawHistoryMap();
        const QJsonObject migrated = migrateMap(map, kMaxPairHistory);
        if (migrated != map) savePerProfileSawHistoryMap(migrated);
    }
    // (C) Per-pair pending batch.
    {
        const QJsonObject map = loadPerProfileSawBatchMap();
        const QJsonObject migrated = migrateMap(map, kBatchSize);
        if (migrated != map) savePerProfileSawBatchMap(migrated);
    }

    // (D) Global bootstrap lag sub-keys: rename "<displayName>" -> "<id>". Don't
    // clobber an existing id key (the bootstrap is recomputed on the next commit anyway).
    {
        m_settings.beginGroup("saw/globalBootstrapLag");
        const QStringList keys = m_settings.childKeys();
        QList<QPair<QString, double>> sets;
        QStringList removes;
        for (const QString& k : keys) {
            const QString id = ScaleTypeIds::normalizeScaleTypeId(k);
            if (id == k) continue;
            if (!keys.contains(id)) sets.append({id, m_settings.value(k).toDouble()});
            removes.append(k);
        }
        for (const QString& k : removes) m_settings.remove(k);
        for (const auto& pair : sets) m_settings.setValue(pair.first, pair.second);
        m_settings.endGroup();
    }

    // Invalidate caches so the next read pulls migrated data.
    m_sawHistoryCacheDirty = true;
    m_sawConvergedCache = -1;
    m_perProfileSawHistoryCacheValid = false;
    m_perProfileSawBatchCacheValid = false;

    qDebug() << "[SAW] migrated SAW storage to canonical scale type-ids";
}
