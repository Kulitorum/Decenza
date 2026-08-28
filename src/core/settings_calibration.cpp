#include "settings_calibration.h"
#include "settings.h"
#include "../machine/sawlogging.h"
#include "../machine/sawprediction.h"
#include "../ble/scales/scaletypeids.h"  // ScaleTypeIds::normalizeScaleTypeId (dependency-free)

// Aliases, not copies — see sawlogging.h. This is a settings store with no
// logMessage signal, so the STDERR forms apply. "Learning" as the source: every
// line here is about the per-(profile, scale) learning store rather than about
// an individual shot's stop.
#define SAWC_LOG(msg)  SAW_LOG_STDERR("Learning", msg)
#define SAWC_WARN(msg) SAW_WARN_STDERR("Learning", msg)
#define SAWC_INFO(msg) SAW_INFO_STDERR("Learning", msg)

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QtMath>
#include <algorithm>
#include <limits>

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

// The read window for sawLearnedLagFor and getExpectedDripFor. NOT every per-pair reader:
// sawLearningEntriesFor honours its caller's maxEntries (8 or 12), and sawLearnedLagFor may
// walk further than this to find three entries with flow > 0.5.
constexpr qsizetype kSawReadWindow = 3;

// The basket segment used when the active equipment package has no basket component.
// Unreachable by sawBasketKey() normalization: "(" and ")" are not alphanumeric, so they can
// never reach its output. (NOT because the output is [a-z0-9-] — it is not; see sawBasketKey.)
const QLatin1String kNoBasketKey("(none)");

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
{
}

void SettingsCalibration::setServingScaleTypeProvider(std::function<QString()> provider) {
    m_servingScaleType = std::move(provider);
}

QString SettingsCalibration::currentScaleType() const {
    // Prefer the scale actually serving the shot path. Keying on the SAVED type
    // instead is how BLE-served shots came to be written into the "decent-wifi"
    // pool: the WiFi scale stays the saved primary while every weight sample
    // arrives over BLE. Four consecutive BLE shots logged scale="decent-wifi"
    // on-device before this was resolved centrally.
    const QString serving = m_servingScaleType ? m_servingScaleType() : QString();
    if (!serving.isEmpty() && ScaleTypeIds::isCanonicalScaleTypeId(serving))
        return serving;

    // A non-canonical id here is EXPECTED for the virtual scale ("flow") and for test
    // doubles. For a real scale driver whose id was never added to ScaleTypeIds::kAll it
    // is a silent correctness bug: nothing downstream rejects it — the isFlowScale()
    // guard in onSettlingComplete() only catches the virtual scale — so a genuine scale
    // trains the SAVED primary's pool with its own transport latency, undetectably.
    // Logged once per distinct id: this runs on every SAW read, and the compile-time
    // guard is the test over the enum (everyScaleTypeIsInTheCanonicalVocabulary); this
    // is only so a field log can show it. qDebug not qWarning because serving a
    // non-canonical type is legitimate in tests running under failOnWarning().
    if (!serving.isEmpty() && serving != QLatin1String("flow")
        && serving != m_warnedNonCanonicalScale) {
        m_warnedNonCanonicalScale = serving;
        qDebug() << "SettingsCalibration: serving scale reports non-canonical type-id"
                 << serving << "- SAW will key on the saved primary instead."
                 << "If this is a real scale, add it to ScaleTypeIds::kAll.";
    }
    // Normalize defensively — scaleType is stored as a canonical id, but this keeps
    // SAW keying correct even if a legacy display name slips through pre-migration.
    return ScaleTypeIds::normalizeScaleTypeId(m_owner ? m_owner->scaleType() : QStringLiteral("decent"));
}

QString SettingsCalibration::resolveScaleKey(const QString& explicitKey) const {
    if (explicitKey.isEmpty())
        return currentScaleType();
    // Load-bearing, and specifically for sawModelSource(): it compares the key RAW
    // against the stored "scale" field of each global-pool entry, so an un-normalized
    // legacy display name there returns "scaleDefault" where the truth is "globalPool"
    // — a wrong model tier shown in the Calibration tab and reported to the AI advisor.
    //
    // The key-derived paths (sawPairKey, globalSawBootstrapLag, sensorLag,
    // sawLearningEntries) each normalize again, so for those it is belt-and-braces.
    // Do NOT reason from that subset that the line can go: this comment previously
    // claimed exactly that, having enumerated the normalizing consumers and missed the
    // one comparing consumer.
    return ScaleTypeIds::normalizeScaleTypeId(explicitKey);
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
    if (multiplier < kProfileFlowCalMin || multiplier > kProfileFlowCalMax) {
        qWarning() << "SettingsCalibration: rejecting per-profile flow calibration"
                   << multiplier << "for" << profileFilename
                   << "(outside [" << kProfileFlowCalMin << "," << kProfileFlowCalMax << "])";
        return false;
    }
    QJsonObject map = allProfileFlowCalibrations();
    map[profileFilename] = multiplier;
    savePerProfileFlowCalMap(map);
    // Any pending batch ideals were computed against the OLD C, so they no longer
    // describe an error relative to the value now stored — same reasoning as
    // clearProfileFlowCalibration(). Done here rather than at each writer because
    // every writer needs it: the auto-cal path clears them just before calling this
    // (so this is a no-op there), while a manual write from the MCP tool or a
    // settings import would otherwise fold stale ideals into the next batch median.
    clearFlowCalPendingIdeals(profileFilename);
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
        m_settings.setValue("calibration/perProfileFlow", "{}");
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

namespace {
// Same shape as flowCalBatch beside it: one settings key holding a
// profile -> {count, reason} map, parsed on read. Kept separate from the batch
// so clearing a batch (which every formula migration does) does not erase the
// evidence that a profile is being rejected — those are independent facts.
QJsonObject parseFlowCalRejections(const QSettings& s) {
    return QJsonDocument::fromJson(
               s.value("calibration/flowCalRejections", "{}").toString().toUtf8())
        .object();
}
}  // namespace

int SettingsCalibration::flowCalRejectedShots(const QString& profileFilename) const {
    return parseFlowCalRejections(m_settings).value(profileFilename).toObject()
        .value("count").toInt(0);
}

QString SettingsCalibration::flowCalLastRejectionReason(const QString& profileFilename) const {
    return parseFlowCalRejections(m_settings).value(profileFilename).toObject()
        .value("reason").toString();
}

void SettingsCalibration::noteFlowCalRejection(const QString& profileFilename,
                                               const QString& reason) {
    QJsonObject map = parseFlowCalRejections(m_settings);
    QJsonObject entry = map.value(profileFilename).toObject();
    entry["count"] = entry.value("count").toInt(0) + 1;
    entry["reason"] = reason;
    map[profileFilename] = entry;
    m_settings.setValue("calibration/flowCalRejections",
                        QJsonDocument(map).toJson(QJsonDocument::Compact));
}

void SettingsCalibration::clearFlowCalRejections(const QString& profileFilename) {
    QJsonObject map = parseFlowCalRejections(m_settings);
    if (!map.contains(profileFilename))
        return;  // Don't rewrite the key on every successful shot.
    map.remove(profileFilename);
    m_settings.setValue("calibration/flowCalRejections",
                        QJsonDocument(map).toJson(QJsonDocument::Compact));
}

void SettingsCalibration::clearAllFlowCalPendingIdeals() {
    // No JSON round-trip needed: unlike perProfileFlow, flowCalBatch has no
    // in-memory cache to keep in sync, and parseFlowCalBatch() already
    // defaults a missing key to "{}" — removing the key is equivalent to
    // writing an empty object, at lower cost.
    m_settings.remove("calibration/flowCalBatch");
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
                SAWC_LOG(QStringLiteral("Divergence detected: last 3 overshoots all %1 "
                                        "— forcing adaptation mode")
                             .arg(allPositive ? QStringLiteral("positive")
                                              : QStringLiteral("negative")));
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
    // Two different situations reach here and they are not equally interesting.
    //
    // A CANONICAL id with no entry above is simply a supported scale whose BLE latency
    // nobody has measured yet — six of the sixteen are in that position (difluid,
    // eureka_precisa, smartchef, solo_barista, timemore, varia_aku). The de1app default
    // is the correct answer for them and adaptive learning replaces it within a few
    // shots. Warning here fired on EVERY SAW read for those users, naming their
    // perfectly-supported scale as "unknown" — a log full of alarming lines describing
    // normal operation, which is how genuinely alarming lines get ignored.
    //
    // A non-canonical string is the one worth flagging: it means a type-id reached SAW
    // that ScaleTypeIds does not know, so it is also missing from kAll and keys its own
    // orphan pool.
    if (ScaleTypeIds::isCanonicalScaleTypeId(id)) {
        SAWC_LOG(QStringLiteral("No measured sensor lag for %1 — using the 0.38 s default "
                                "until learning has data").arg(id));
    } else {
        SAWC_WARN(QStringLiteral("Unknown scale type for sensorLag: %1 — using default 0.38 s. "
                                 "If this is a real scale, add it to ScaleTypeIds::kAll.")
                      .arg(scaleType));
    }
    return 0.38;  // de1app default for unknown/unlisted scales
}

void SettingsCalibration::addSawLearningPoint(double drip, double flowRate, QString scaleType,
                                              double overshoot, const QString& profileFilename,
                                              const QString& basketKey) {
    // Key SAW learning on the canonical type-id (rename-stable). Covers both the
    // per-pair path (addSawPerPairEntry) and the legacy global-pool path below.
    scaleType = ScaleTypeIds::normalizeScaleTypeId(scaleType);

    // Validate physical constraints (scale glitches can produce negative values)
    if (drip < 0 || flowRate < 0) {
        SAWC_WARN(QStringLiteral("Invalid learning point rejected: drip=%1 g flow=%2 g/s")
                      .arg(drip, 0, 'f', 2).arg(flowRate, 0, 'f', 2));
        return;
    }

    // Reject physically implausible entries: implied lag > 4s is beyond any real BLE
    // scale (BLE round-trip + machine stop + final drip ≈ 3.5s worst case).
    // The flowRate > 0.5 guard prevents division-by-near-zero making the ratio meaningless
    // at very low flow (e.g. 0.1 g/s would flag a 0.39g drip as "too high").
    if (flowRate > 0.5 && drip / flowRate > 4.0) {
        SAWC_WARN(QStringLiteral("Implied lag too high (%1 s), skipping learning: "
                                 "drip=%2 g flow=%3 g/s")
                      .arg(drip / flowRate, 0, 'f', 2).arg(drip, 0, 'f', 2)
                      .arg(flowRate, 0, 'f', 2));
        return;
    }

    // Outlier rejection: when converged, skip learning points that deviate too far.
    // Skipped when overshoot < -6g (auto-reset candidate): the model may be systematically
    // wrong and must accept the new baseline rather than defending the stale converged model.
    bool isAutoResetCandidate = (overshoot < -6.0);
    if (!isAutoResetCandidate && isSawConverged(scaleType)) {
        double expectedDrip = getExpectedDripFor(profileFilename, scaleType, flowRate, basketKey);
        double threshold = qMax(3.0, expectedDrip);  // Reject if deviation exceeds expected drip (or 3g floor)
        if (qAbs(drip - expectedDrip) > threshold) {
            SAWC_WARN(QStringLiteral("Outlier rejected: drip=%1 g expected=%2 g threshold=%3 g "
                                     "(converged, deviation too high)")
                          .arg(drip, 0, 'f', 2).arg(expectedDrip, 0, 'f', 2)
                          .arg(threshold, 0, 'f', 2));
            return;
        }
    }

    // When called with a profile filename, route through the per-(profile, scale) batch
    // accumulator. The pending batch holds 3 shots before committing the median to the
    // per-pair history AND the global pool — this reduces churn from individual shots
    // and provides outlier rejection via the median + per-element deviation check. See
    // AUTO_FLOW_CALIBRATION for the same pattern applied to flow cal.
    if (!profileFilename.isEmpty()) {
        addSawPerPairEntry(drip, flowRate, scaleType, overshoot, profileFilename, basketKey);
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
            SAWC_WARN(QStringLiteral("Learning history corrupted, starting fresh: %1")
                          .arg(parseError.errorString()));
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
            SAWC_WARN(QStringLiteral("2nd consecutive early stop for %1 — resetting learning "
                                     "(both shots overshoot <-6g)").arg(scaleType));
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

QJsonObject SettingsCalibration::sawLearningExport() const {
    QJsonObject o;
    const QByteArray lh = m_settings.value("saw/learningHistory").toByteArray();
    if (!lh.isEmpty()) o["learningHistory"] = QJsonDocument::fromJson(lh).array();
    const QByteArray ph = m_settings.value("saw/perProfileHistory").toByteArray();
    if (!ph.isEmpty()) o["perProfileHistory"] = QJsonDocument::fromJson(ph).object();
    const QByteArray pb = m_settings.value("saw/perProfileBatch").toByteArray();
    if (!pb.isEmpty()) o["perProfileBatch"] = QJsonDocument::fromJson(pb).object();
    if (m_settings.contains("saw/globalBootstrapLag"))
        o["globalBootstrapLag"] = m_settings.value("saw/globalBootstrapLag").toDouble();
    // Carry any quarantined corrupt blob. Without this a device transfer taken after a
    // corruption migrates the emptied store and leaves the only copy of the salvageable bytes
    // behind on the old device — and the quarantine is also the seed's gate, so the new device
    // would close the seed the transfer was supposed to preserve learning across.
    for (const auto& key : {QStringLiteral("saw/perProfileHistory.corrupt"),
                            QStringLiteral("saw/perProfileHistory.corruptAt"),
                            QStringLiteral("saw/perProfileBatch.corrupt"),
                            QStringLiteral("saw/perProfileBatch.corruptAt")}) {
        if (m_settings.contains(key))
            o[key.mid(4)] = QString::fromUtf8(m_settings.value(key).toByteArray());
    }
    return o;
}

void SettingsCalibration::sawLearningImport(const QJsonObject& o) {
    // A pre-basket export carries two-segment buckets, and a device will normally have closed
    // the seed long before an import — so importing without reopening it restores data no
    // reader will ever look at. Clearing is unconditional, which is safe either way: a device
    // whose seed never closed (a failed history read) simply stays open. Note the seed only
    // runs from MainController's constructor, so the restored data becomes readable on the
    // NEXT launch, not at import time. resetSawLearning() clears the flag for the
    // same reason; the device-transfer path reaches HERE instead, and used not to.
    m_settings.remove("saw/basketKeyMigrated");
    for (const auto& key : {QStringLiteral("perProfileHistory.corrupt"),
                            QStringLiteral("perProfileHistory.corruptAt"),
                            QStringLiteral("perProfileBatch.corrupt"),
                            QStringLiteral("perProfileBatch.corruptAt")}) {
        if (o.contains(key))
            m_settings.setValue(QStringLiteral("saw/") + key, o[key].toString().toUtf8());
    }
    if (o.contains("learningHistory"))
        m_settings.setValue("saw/learningHistory", QJsonDocument(o["learningHistory"].toArray()).toJson());
    if (o.contains("perProfileHistory"))
        m_settings.setValue("saw/perProfileHistory", QJsonDocument(o["perProfileHistory"].toObject()).toJson());
    if (o.contains("perProfileBatch"))
        m_settings.setValue("saw/perProfileBatch", QJsonDocument(o["perProfileBatch"].toObject()).toJson());
    if (o.contains("globalBootstrapLag"))
        m_settings.setValue("saw/globalBootstrapLag", o["globalBootstrapLag"].toDouble());
    // Invalidate the read caches so the imported data takes effect immediately.
    m_sawHistoryCacheDirty = true;
    m_sawConvergedCache = -1;
    m_perProfileSawHistoryCacheValid = false;
    m_perProfileSawBatchCacheValid = false;
    emit sawLearnedLagChanged();
}

void SettingsCalibration::resetSawLearning() {
    m_settings.remove("saw/learningHistory");
    m_settings.remove("saw/perProfileHistory");
    m_settings.remove("saw/perProfileBatch");
    m_settings.remove("saw/globalBootstrapLag");
    // The basket re-key flag goes too: with every bucket gone there is nothing left to
    // attribute to the wrong basket, and leaving it set would make a store restored from a
    // pre-basket backup permanently unreadable — the buckets would come back two-segment
    // with the migration already marked done.
    m_settings.remove("saw/basketKeyMigrated");
    // Clear the corruption latch too: this call deliberately discards every bucket, so there
    // is nothing left for the latch to protect, and leaving it set would block the seed for
    // the rest of the session over data the user just threw away.
    // The quarantined blobs go too: this call is the user saying "start over", so keeping bytes
    // they just discarded would hold the seed's gate open forever. It also keeps the "wipes EVERY
    // saw/* key" promise on this function true.
    for (const auto& qkey : {QStringLiteral("saw/perProfileHistory.corrupt"),
                             QStringLiteral("saw/perProfileHistory.corruptAt"),
                             QStringLiteral("saw/perProfileBatch.corrupt"),
                             QStringLiteral("saw/perProfileBatch.corruptAt")}) {
        m_settings.remove(qkey);
    }
    m_sawHistoryCacheDirty = true;
    m_sawConvergedCache = -1;
    m_perProfileSawHistoryCacheValid = false;
    m_perProfileSawBatchCacheValid = false;
    SAWC_LOG(QStringLiteral("Reset all learning"));
    emit sawLearnedLagChanged();

    // Cross-domain: hot-water SAW offset reset is performed by SettingsBrew
    // via the connect() wired up in Settings::Settings().
    emit sawLearningResetRequested();
}

bool SettingsCalibration::hasSawLearningForProfile(const QString& profileFilename,
                                                   const QString& scaleType) const {
    if (profileFilename.isEmpty()) return false;
    // Same exact-or-"::" prefix rule as resetSawLearningForProfile — the reason is spelled out
    // there (the collision is in the SCALE segment), and is deliberately not repeated here.
    const QString legacyKey = sawLegacyPairKey(profileFilename, resolveScaleKey(scaleType));
    const QString perBasketPrefix = legacyKey + QStringLiteral("::");
    const auto anyIn = [&](const QJsonObject& map) {
        for (auto it = map.begin(); it != map.end(); ++it) {
            if ((it.key() == legacyKey || it.key().startsWith(perBasketPrefix))
                && !it.value().toArray().isEmpty())
                return true;
        }
        return false;
    };
    return anyIn(loadPerProfileSawHistoryMap()) || anyIn(loadPerProfileSawBatchMap());
}

void SettingsCalibration::resetSawLearningForProfile(const QString& profileFilename, const QString& scaleType) {
    if (profileFilename.isEmpty()) {
        SAWC_WARN(QStringLiteral("resetSawLearningForProfile called with empty profile"));
        return;
    }
    // Clears EVERY basket bucket for this (profile, scale), plus the pre-basket-keying
    // two-segment bucket. The legacy key is the prefix of every per-basket key for the
    // same pair, so one prefix test covers both shapes — and it must be an exact-or-"::"
    // test, not a bare startsWith. The collision is in the SCALE segment, not the profile
    // one: "p::decent" IS a prefix of "p::decent-wifi::<basket>", so a bare test resetting
    // the BLE scale would wipe the WiFi and USB scales' learning — the very split the
    // transport measurement says must stay separate. (A profile-name prefix cannot collide;
    // "d_flow::decent" is not a prefix of "d_flow_q::decent", and this comment used to claim
    // it was.)
    const QString legacyKey = sawLegacyPairKey(profileFilename, scaleType);
    const QString perBasketPrefix = legacyKey + QStringLiteral("::");
    const auto matches = [&](const QString& k) {
        return k == legacyKey || k.startsWith(perBasketPrefix);
    };
    QJsonObject historyMap = loadPerProfileSawHistoryMap();
    QJsonObject batchMap = loadPerProfileSawBatchMap();
    int removed = 0;
    for (const QString& k : historyMap.keys()) {
        if (matches(k)) { historyMap.remove(k); ++removed; }
    }
    for (const QString& k : batchMap.keys()) {
        if (matches(k)) { batchMap.remove(k); ++removed; }
    }
    if (removed > 0) {
        savePerProfileSawHistoryMap(historyMap);
        savePerProfileSawBatchMap(batchMap);
        SAWC_LOG(QStringLiteral("Reset perProfileHistory for %1 — %2 bucket(s) across all baskets")
                     .arg(legacyKey).arg(removed));
        emit sawLearnedLagChanged();
    }
}

// ---- per-(profile, scale) helpers ----

QString SettingsCalibration::sawLegacyPairKey(const QString& profileFilename, const QString& scaleType) {
    // Key on the canonical type-id so per-(profile, scale) reads/writes stay in sync
    // regardless of whether the caller passed an id or a legacy display name. This is
    // the single choke point for perProfileSawHistory / sawPendingBatch /
    // resetSawLearningForProfile / addSawPerPairEntry.
    return profileFilename + QStringLiteral("::") + ScaleTypeIds::normalizeScaleTypeId(scaleType);
}

QString SettingsCalibration::sawPairKey(const QString& profileFilename, const QString& scaleType,
                                        const QString& basketKey) {
    // Three segments. Two-segment keys are what every pre-basket build wrote; nothing reads
    // them on the prediction path and nothing writes them now. They are NOT a fallback tier
    // and they do not age out — seedSawBucketsFromPreBasketKeys() copies them forward once,
    // and they stay in place so a rollback is lossless. Segment count is how the seed spots
    // data it has not carried yet.
    return sawLegacyPairKey(profileFilename, scaleType) + QStringLiteral("::")
           + (basketKey.isEmpty() ? QString(kNoBasketKey) : basketKey);
}

QString SettingsCalibration::sawBasketKey(const QString& brand, const QString& model) {
    const QString combined = (brand.trimmed() + QLatin1Char(' ') + model.trimmed()).trimmed();
    if (combined.isEmpty()) return QString(kNoBasketKey);
    QString out;
    out.reserve(combined.size());
    bool pendingDash = false;
    for (const QChar c : combined) {
        if (c.isLetterOrNumber()) {
            // NOT restricted to [a-z0-9-]: isLetterOrNumber() accepts any Unicode letter or
            // digit and toLower() case-maps Greek and Cyrillic, so a non-Latin basket name
            // passes through in its own script. kNoBasketKey stays unreachable anyway, for a
            // different reason than this comment used to give: "(" and ")" are not
            // alphanumeric, so they can never reach the output.
            if (pendingDash && !out.isEmpty()) out.append(QLatin1Char('-'));
            pendingDash = false;
            out.append(c.toLower());
        } else {
            pendingDash = true;
        }
    }
    return out.isEmpty() ? QString(kNoBasketKey) : out;
}

QString SettingsCalibration::currentBasketKey() const {
    // SettingsDye already resolves the active package's basket and caches it, so this is
    // a member read rather than a query — safe on the shot path.
    if (!m_owner || !m_owner->dye()) return QString(kNoBasketKey);
    return sawBasketKey(m_owner->dye()->dyeBasketBrand(), m_owner->dye()->dyeBasketModel());
}

QString SettingsCalibration::resolveBasketKey(const QString& explicitKey) const {
    return explicitKey.isEmpty() ? currentBasketKey() : explicitKey;
}

QJsonObject SettingsCalibration::loadSawMap(const QString& settingsKey) const {
    // One implementation for both SAW maps. They were near-identical copies, and the
    // quarantine below is exactly the kind of policy that would have been added to one and
    // not the other.
    QJsonParseError parseError;
    const QByteArray raw = m_settings.value(settingsKey, "{}").toByteArray();
    QJsonObject map = QJsonDocument::fromJson(raw, &parseError).object();
    if (parseError.error == QJsonParseError::NoError) return map;

    // QUARANTINE before overwriting. This used to reset the key to "{}" and log "history lost",
    // which made the loss sound unavoidable while destroying the only copy of the bytes — and
    // truncation from a partly-flushed write is precisely the case where they are partly
    // salvageable.
    //
    // NEWEST capture wins. Keeping the first was the original instinct and it is wrong: the only
    // case it defends is re-reading the same blob after a failed "{}" write, where overwriting is
    // a no-op — while the case it loses is real. Truncation in month one, six months of rebuilt
    // learning, truncation again: keep-first would retain the stale two-bucket blob and overwrite
    // the valuable one with "{}", which is the destruction this exists to prevent. (The "a later
    // failure re-reads our {}" reasoning was false anyway: "{}" is valid JSON.)
    const QString quarantineKey = settingsKey + QStringLiteral(".corrupt");
    m_settings.setValue(quarantineKey, raw);
    m_settings.setValue(quarantineKey + QStringLiteral("At"),
                        QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    m_settings.setValue(settingsKey, "{}");
    SAWC_WARN(QStringLiteral("Corrupt %1 JSON: %2 — %3 bytes quarantined at %4, store reset")
                  .arg(settingsKey, parseError.errorString())
                  .arg(raw.size()).arg(quarantineKey));
    return QJsonObject();
}

QJsonObject SettingsCalibration::loadPerProfileSawHistoryMap() const {
    if (m_perProfileSawHistoryCacheValid) return m_perProfileSawHistoryCache;
    m_perProfileSawHistoryCache = loadSawMap(QStringLiteral("saw/perProfileHistory"));
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
    m_perProfileSawBatchCache = loadSawMap(QStringLiteral("saw/perProfileBatch"));
    m_perProfileSawBatchCacheValid = true;
    return m_perProfileSawBatchCache;
}

void SettingsCalibration::savePerProfileSawBatchMap(const QJsonObject& map) {
    m_settings.setValue("saw/perProfileBatch",
                        QJsonDocument(map).toJson(QJsonDocument::Compact));
    m_perProfileSawBatchCache = map;
    m_perProfileSawBatchCacheValid = true;
}

QJsonArray SettingsCalibration::perProfileSawHistory(const QString& profileFilename,
                                                     const QString& scaleType,
                                                     const QString& basketKey) const {
    return loadPerProfileSawHistoryMap()
        .value(sawPairKey(profileFilename, resolveScaleKey(scaleType), resolveBasketKey(basketKey)))
        .toArray();
}

QJsonObject SettingsCalibration::allPerProfileSawHistory() const {
    return loadPerProfileSawHistoryMap();
}

QJsonArray SettingsCalibration::sawPendingBatch(const QString& profileFilename,
                                                const QString& scaleType,
                                                const QString& basketKey) const {
    return loadPerProfileSawBatchMap()
        .value(sawPairKey(profileFilename, resolveScaleKey(scaleType), resolveBasketKey(basketKey)))
        .toArray();
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

QString SettingsCalibration::sawModelSource(const QString& profileFilename, QString scaleType,
                                            const QString& basketKey) const {
    scaleType = resolveScaleKey(scaleType);
    if (!profileFilename.isEmpty()) {
        if (!graduatedPairHistory(profileFilename, scaleType, basketKey).isEmpty())
            return QStringLiteral("perProfile");
    }
    if (globalSawBootstrapLag(scaleType) > 0.0) return QStringLiteral("globalBootstrap");
    ensureSawCacheLoaded();
    for (const auto& v : std::as_const(m_sawHistoryCache)) {
        if (v.toObject().value("scale").toString() == scaleType) return QStringLiteral("globalPool");
    }
    return QStringLiteral("scaleDefault");
}

// The committed medians for (profile, scale, basket) once the triple has graduated, else
// empty — which is the caller's signal to fall through to the bootstrap. Oldest-first;
// every reader walks it backwards. One helper so the graduation test cannot drift between
// the four readers below.
//
// There is deliberately NO basket-blind fallback tier here. Pre-basket history is copied
// once into the combinations actually pulled by seedSawBucketsFromPreBasketKeys(), so this stays
// a single lookup rather than carrying a frozen bucket and a warmup blend forever.
QJsonArray SettingsCalibration::graduatedPairHistory(const QString& profileFilename,
                                                     const QString& scaleType,
                                                     const QString& basketKey) const {
    if (profileFilename.isEmpty()) return QJsonArray();
    const QJsonArray pairHistory = perProfileSawHistory(profileFilename, scaleType, basketKey);
    return (pairHistory.size() >= kSawMinMediansForGraduation) ? pairHistory : QJsonArray();
}

QList<QPair<double, double>> SettingsCalibration::sawLearningEntriesFor(const QString& profileFilename,
                                                                       const QString& scaleType,
                                                                       int maxEntries,
                                                                       const QString& basketKey) const {
    const QString scale = resolveScaleKey(scaleType);
    QList<QPair<double, double>> result;
    const QJsonArray pairHistory = graduatedPairHistory(profileFilename, scale, basketKey);
    for (qsizetype i = pairHistory.size() - 1; i >= 0 && result.size() < maxEntries; --i) {
        QJsonObject obj = pairHistory[i].toObject();
        if (obj.contains("drip")) {
            result.append({obj["drip"].toDouble(), obj["flow"].toDouble()});
        }
    }
    if (!result.isEmpty()) return result;
    return sawLearningEntries(scale, maxEntries);
}

double SettingsCalibration::sawLearnedLagFor(const QString& profileFilename, const QString& scaleType,
                                             const QString& basketKey) const {
    const QString scale = resolveScaleKey(scaleType);
    const QJsonArray pairHistory = graduatedPairHistory(profileFilename, scale, basketKey);
    if (!pairHistory.isEmpty()) {
        double sumLag = 0;
        qsizetype count = 0;
        for (qsizetype i = pairHistory.size() - 1; i >= 0 && count < kSawReadWindow; --i) {
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
    double bootstrap = globalSawBootstrapLag(scale);
    if (bootstrap > 0.0) return bootstrap;
    return sawLearnedLag();
}

double SettingsCalibration::getExpectedDripFor(const QString& profileFilename,
                                               const QString& scaleType,
                                               double currentFlowRate,
                                               const QString& basketKey) const {
    const QString scale = resolveScaleKey(scaleType);
    const QJsonArray pairHistory = graduatedPairHistory(profileFilename, scale, basketKey);
    if (!pairHistory.isEmpty()) {
        // Same flow-similarity kernel as the global getExpectedDrip(), but
        // recencyMin is fixed at 3.0 — per-pair history only kicks in after
        // graduation (≥ kSawMinMediansForGraduation committed medians).
        // Reads at most 3 recent entries, matching the per-pair read window.
        struct Entry { double drip; double flow; };
        QVector<Entry> entries;
        for (qsizetype i = pairHistory.size() - 1; i >= 0 && entries.size() < kSawReadWindow; --i) {
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
    double bootstrap = globalSawBootstrapLag(scale);
    if (bootstrap > 0.0) {
        return qMin(currentFlowRate * bootstrap, 8.0);
    }
    return qMin(currentFlowRate * (sensorLag(scale) + 0.1), 8.0);
}

// ---- per-pair batch accumulator + commit ----

void SettingsCalibration::addSawPerPairEntry(double drip, double flowRate, const QString& scaleType,
                                             double overshoot, const QString& profileFilename,
                                             const QString& basketKey) {
    // resolveBasketKey, not a bare basketKey: an empty key here would key on the literal
    // empty segment and silently open a bucket no reader ever looks in.
    const QString basket = resolveBasketKey(basketKey);
    const QString key = sawPairKey(profileFilename, scaleType, basket);

    // 1. Append entry to pending batch
    QJsonObject batchMap = loadPerProfileSawBatchMap();
    QJsonArray batch = batchMap.value(key).toArray();
    QJsonObject entry;
    entry["drip"] = drip;
    entry["flow"] = flowRate;
    entry["overshoot"] = overshoot;
    entry["scale"] = scaleType;
    entry["profile"] = profileFilename;
    entry["basket"] = basket;
    entry["ts"] = QDateTime::currentSecsSinceEpoch();
    batch.append(entry);

    if (batch.size() < kBatchSize) {
        batchMap[key] = batch;
        savePerProfileSawBatchMap(batchMap);
        const double lag = (flowRate > 0.5) ? drip / flowRate : 0.0;
        SAWC_LOG(QStringLiteral("Accumulated drip=%1 g flow=%2 g/s for %3 (%4/%5) lag=%6 s")
                     .arg(drip, 0, 'f', 2).arg(flowRate, 0, 'f', 2).arg(key)
                     .arg(batch.size()).arg(kBatchSize).arg(lag, 0, 'f', 3));
        return;
    }

    // 2. Batch full — collect the batch's overshoots, and each usable shot's lag beside
    //    the pair it came from. One place derives a lag and one place decides a flow is
    //    usable, so the filter and the ratio cannot drift apart.
    struct BatchLag { double lag; double drip; double flow; };
    QVector<double> overs;
    QVector<BatchLag> lagOf;
    overs.reserve(batch.size());
    lagOf.reserve(batch.size());
    for (const auto& v : std::as_const(batch)) {
        const QJsonObject o = v.toObject();
        const double entryDrip = o["drip"].toDouble();
        const double entryFlow = o["flow"].toDouble();
        overs.append(o["overshoot"].toDouble());
        if (entryFlow > 0.5) lagOf.append({entryDrip / entryFlow, entryDrip, entryFlow});
    }

    auto medianOf = [](QVector<double> v) -> double {
        if (v.isEmpty()) return 0.0;
        std::sort(v.begin(), v.end());
        const qsizetype n = v.size();
        return (n % 2 == 0) ? (v[n / 2 - 1] + v[n / 2]) / 2.0 : v[n / 2];
    };
    const double medianOver = medianOf(overs);

    // A batch with no usable lag has nothing to commit. Dropping it is the same handling
    // every other implausible batch gets in step 3 — the alternative, committing
    // medianOf(drips) over medianOf(flows), is exactly the composite this block exists to
    // stop storing, and it would arrive with no warning and a printed lag of 0.000 s.
    // Unreachable from the app (ShotTimingController returns before emitting below
    // 0.5 g/s, shottimingcontroller.cpp), but sawLearningImport() writes a pending batch
    // wholesale with no validation, so a transferred device can present one.
    if (lagOf.isEmpty()) {
        SAWC_WARN(QStringLiteral("batch rejected — no shot had a usable flow for %1 — "
                                 "dropping batch").arg(key));
        batchMap.remove(key);
        savePerProfileSawBatchMap(batchMap);
        return;
    }

    // The committed pair is ONE batch shot's own (drip, flow), and the lag is that shot's
    // lag — not medianOf(drips) over medianOf(flows). With a small batch those two medians
    // usually come from different shots, so their quotient is a lag no shot had. That
    // matters twice: the gate below compares every shot against this lag, and step 5
    // commits the pair, which four readers then divide or smooth —
    // sawLearningEntriesFor (main.cpp hands the pairs to the WeightProcessor snapshot that
    // decides when to fire the stop, so this is the one a wrong pair mis-stops a shot on),
    // getExpectedDripFor, sawLearnedLagFor, and recomputeGlobalSawBootstrap.
    //
    // Measured on a 250-shot corpus via tools/saw_parity: −1.0% MAE overall, −2.3%
    // mid-flow, +2.4% WORSE at high flow (n=14) — inside the noise of which shots land in
    // which batch. It is here because a committed pair must describe a shot that happened,
    // not because it predicts better. Do not cite it as a performance result.
    //
    // medianOver is deliberately NOT selected this way. It stays the median of the three
    // overshoots because it gates the auto-reset in step 4 and, through the step-6 pool
    // mirror, isSawConverged(); it is never paired with a drip or a flow to form a lag.
    //
    // Closest-lag rather than exact equality: medianOf() averages the middle two when the
    // count is even, and no shot owns that value. lagOf is shorter than the batch whenever
    // a shot's flow was unusable, so an even count is reachable without kBatchSize
    // changing. medianLag is then re-read off the chosen shot, so the gate and the commit
    // log always describe the same real shot.
    double commitDrip = 0.0;
    double commitFlow = 0.0;
    {
        QVector<double> lags;
        lags.reserve(lagOf.size());
        for (const auto& l : std::as_const(lagOf)) lags.append(l.lag);
        const double lagMedian = medianOf(lags);
        double bestDev = std::numeric_limits<double>::max();
        for (const auto& l : std::as_const(lagOf)) {
            const double dev = qAbs(l.lag - lagMedian);
            if (dev < bestDev) {
                bestDev = dev;
                commitDrip = l.drip;
                commitFlow = l.flow;
            }
        }
    }
    const double medianLag = commitDrip / commitFlow;

    // 3. Outlier check: reject batch if any lag deviates too far from the median.
    //    IQR gating is not used here because kBatchSize=3 produces too few values
    //    for a meaningful IQR estimate; per-element deviation is sufficient.
    QString rejectReason;
    for (const auto& l : std::as_const(lagOf)) {
        const double dev = qAbs(l.lag - medianLag);
        if (dev > kBatchMaxDeviation) {
            rejectReason = QString("outlier lag=%1 deviates %2s > %3s from median")
                               .arg(l.lag).arg(dev).arg(kBatchMaxDeviation);
            break;
        }
    }
    if (!rejectReason.isEmpty()) {
        SAWC_WARN(QStringLiteral("batch rejected — %1 median_lag=%2 s for %3 — dropping batch")
                      .arg(rejectReason).arg(medianLag, 0, 'f', 3).arg(key));
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
            SAWC_WARN(QStringLiteral("2nd consecutive overshoot<-6g for %1 — clearing "
                                     "committed history").arg(key));
            pairHistory = QJsonArray();
        }
    }

    // 5. Commit median to per-pair history.
    QJsonObject medianEntry;
    medianEntry["drip"] = commitDrip;
    medianEntry["flow"] = commitFlow;
    medianEntry["overshoot"] = medianOver;
    medianEntry["scale"] = scaleType;
    medianEntry["profile"] = profileFilename;
    medianEntry["basket"] = basket;
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

    SAWC_LOG(QStringLiteral("Committed median lag=%1 s (drip=%2 g flow=%3 g/s) for %4 "
                            "— n_medians=%5")
                 .arg(medianLag, 0, 'f', 3).arg(commitDrip, 0, 'f', 2)
                 .arg(commitFlow, 0, 'f', 2).arg(key).arg(pairHistory.size()));

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
    //
    // The contributor set is every THREE-SEGMENT bucket on this scale whose newest median is
    // not inherited — the two exclusions are applied in the loop below and each is explained
    // there. That is what pays for the basket dimension: a brand-new basket cold-starts from
    // this device's own history on this scale rather than from the 0.38 s constant, without
    // one seeded batch voting once per basket. The bootstrap KEY stays per-scale, so a basket
    // is never a bootstrap of its own.
    //
    // Consequence worth knowing: a store holding only pre-basket buckets, with no seeded
    // basket yet, now yields NO bootstrap at all (fewer than 2 contributors). That is correct
    // — every candidate would have been a frozen snapshot — but it is a behaviour change from
    // the version that counted them.
    QJsonObject map = loadPerProfileSawHistoryMap();
    QVector<double> lags;
    for (auto it = map.begin(); it != map.end(); ++it) {
        QJsonArray pairHistory = it.value().toArray();
        if (pairHistory.isEmpty()) continue;
        // Median entries record their scale; only include this scale's pairs.
        if (pairHistory.last().toObject().value("scale").toString() != scaleType) continue;
        // Use the last committed median lag as that pair's representative.
        QJsonObject last = pairHistory.last().toObject();
        // Skip buckets whose newest median is still inherited from the pre-basket upgrade
        // seed. That median was copied into EVERY basket, so counting it once per bucket
        // would let one batch of shots vote N times and drag the cross-basket median onto
        // itself. A bucket that has learned nothing of its own has nothing to contribute.
        if (last.value("inherited").toBool()) continue;
        // Nor the pre-basket bucket the seed copied FROM. It is left in place for rollback and
        // nothing ever writes it again, so it would contribute a frozen snapshot of its own
        // past forever — and, with a single profile and basket, its presence alone lifts the
        // contributor count to 2 and conjures a bootstrap out of the live bucket averaged
        // against its own history.
        if (it.key().count(QStringLiteral("::")) != 2) continue;
        double drip = last.value("drip").toDouble();
        double flow = last.value("flow").toDouble();
        if (flow > 0.5) lags.append(drip / flow);
    }
    if (lags.size() < 2) {
        // Need at least 2 contributing buckets to compute a useful bootstrap median. Logged
        // because the exclusions above made zero contributors reachable — an unseeded store, or
        // one whose seed was reopened — and a silent return there leaves no per-scale prior and
        // no explanation for it.
        SAWC_LOG(QStringLiteral("Global bootstrap for %1 not computed — %2 eligible bucket(s)")
                     .arg(scaleType).arg(lags.size()));
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
    SAWC_LOG(QStringLiteral("Global bootstrap lag for %1 updated to %2 s "
                            "(median of %3 pairs with committed history)")
                 .arg(scaleType).arg(median, 0, 'f', 3).arg(n));
}

void SettingsCalibration::seedSawBucketsFromPreBasketKeys(const QHash<QString, QStringList>& basketsByProfile,
                                                          bool historyComplete) {
    if (m_settings.value("saw/basketKeyMigrated", false).toBool()) return;

    // Upgrade path for stores written before the basket joined the key. Each pre-basket
    // "<profile>::<scale>" bucket is COPIED into "<profile>::<scale>::<basket>" for every
    // basket that profile was actually pulled with, so each of those combinations keeps
    // predicting exactly what the single shared model predicted and then diverges as it
    // earns medians of its own.
    //
    // Scoped per profile on purpose: a basket used with one profile says nothing about
    // another, and seeding every (profile x basket) product would fabricate buckets of
    // borrowed data for combinations the user never tried. A profile absent from the
    // caller's history window is left alone entirely — untried means untried, and its
    // bucket stays where it is.
    //
    // Copy, not move, and three things follow:
    //   - The two-segment keys are LEFT IN PLACE. Nothing reads them any more, but an older
    //     build rolled back to still does, so rollback is lossless and this stays repeatable.
    //   - Copies are tagged "inherited". Only recomputeGlobalSawBootstrap() consults that:
    //     one batch copied into N baskets must not vote N times in the cross-basket median.
    //   - Seeding is additive and per bucket, so it can run again as more history becomes
    //     known. The flag is set only when the caller says its window is complete.
    //
    // Rejected alternatives, both built first: a permanent basket-blind fallback tier (never
    // trims, never ages out, and keeps a second reader plus a blended tier and two extra
    // model-source values alive forever), and re-keying onto the single ACTIVE basket (which
    // on this maintainer's own device would have labelled 1038 shots of Decent-basket
    // history as the 4-shot Graph basket — both figures are the equipment packages' own
    // shotCount aggregates). See docs/CLAUDE_MD/SAW_LEARNING.md.
    // An empty map cannot be a TRUE answer for a store that has pre-basket buckets: those
    // buckets exist because shots were pulled. Treat it as a failed/unavailable history read
    // and leave the flag open — closing on this answer is what would make them unreachable.
    // Belt and braces behind the emit-only-on-success rule in requestRecentProfileBasketPairs:
    // three independent reviewers reached this same state by different doors.
    // Count PRE-BASKET buckets specifically, not every key: counting all of them refused
    // forever on a store that is already migrated and legitimately has no recent shots
    // (reachable after a device transfer, which reopens the seed) and logged a count of
    // pre-basket buckets that did not exist.
    // BOTH maps first. The order is load-bearing twice over: a quarantine is written lazily by
    // whichever load hits the bad blob, so a gate before the batch read would miss batch-only
    // corruption entirely, and the count below must see the post-reset store.
    const QJsonObject history = loadPerProfileSawHistoryMap();
    const QJsonObject batch = loadPerProfileSawBatchMap();

    int preBasketCount = 0;
    for (auto it = history.begin(); it != history.end(); ++it) {
        if (it.key().count(QStringLiteral("::")) == 1 && !it.value().toArray().isEmpty())
            ++preBasketCount;
    }

    // Gate on the PERSISTED quarantine, not a session flag. A session flag covered only the run
    // that found the corruption — but the reset it guards is persisted, and restoring bytes can
    // only happen between runs, so by the next launch the store parses clean, every other guard
    // passes over an empty store, and the flag closes anyway. The quarantine key outlives the
    // session; "bytes are still waiting to be restored" is the fact that matters.
    //
    // Deferring only while there is nothing to copy is deliberate: restore the blob and the
    // pre-basket buckets reappear, so the next launch seeds normally; never restore it and the
    // seed had no work anyway, so holding the flag open costs nothing. resetSawLearning() drops
    // the quarantine, which is how a user says "start over" and releases the hold.
    const bool quarantined = m_settings.contains("saw/perProfileHistory.corrupt")
                             || m_settings.contains("saw/perProfileBatch.corrupt");
    if (quarantined && preBasketCount == 0) {
        SAWC_WARN(QStringLiteral("Basket seed deferred: a corrupt SAW blob is quarantined and no "
                                 "pre-basket bucket is present to copy — closing the seed would "
                                 "make the loss permanent if those bytes are restored"));
        return;
    }
    if (basketsByProfile.isEmpty() && preBasketCount > 0) {
        SAWC_WARN(QStringLiteral("Basket seed got no profile/basket pairs while %1 pre-basket "
                                 "bucket(s) exist — treating as a failed history read, "
                                 "retrying next launch").arg(preBasketCount));
        return;
    }

    // Counted INSIDE the copy loop, never re-derived in a second pass: the target-key derivation
    // must exist at one site only, or the copy and the count are free to drift apart silently.
    struct SeedStats {
        int matched = 0;      // pre-basket buckets with data AND at least one basket in the window
        int alreadyFull = 0;  // of those, the ones whose every target bucket already existed
    };
    SeedStats historyStats;
    const auto seed = [&basketsByProfile](const QJsonObject& in,
                                         SeedStats* stats = nullptr) -> QJsonObject {
        QJsonObject out = in;
        for (auto it = in.begin(); it != in.end(); ++it) {
            const QString key = it.key();
            if (key.count(QStringLiteral("::")) != 1) continue;   // already per-basket
            const QString profile = key.left(key.indexOf(QStringLiteral("::")));
            const QStringList baskets = basketsByProfile.value(profile);
            if (baskets.isEmpty()) continue;                      // profile never pulled: untried
            if (it.value().toArray().isEmpty()) continue;         // nothing to inherit

            bool anyTarget = false;
            bool anyCreated = false;
            for (const QString& basket : baskets) {
                if (basket.isEmpty()) continue;
                anyTarget = true;
                const QString newKey = key + QStringLiteral("::") + basket;
                if (out.contains(newKey)) continue;               // basket already has data
                QJsonArray arr = it.value().toArray();
                for (qsizetype i = 0; i < arr.size(); ++i) {
                    QJsonObject o = arr[i].toObject();
                    o["basket"] = basket;
                    o["inherited"] = true;
                    arr[i] = o;
                }
                out[newKey] = arr;
                anyCreated = true;
            }
            if (!anyTarget || !stats) continue;
            ++stats->matched;
            if (!anyCreated) ++stats->alreadyFull;                 // every target was already there
        }
        return out;
    };

    const QJsonObject newHistory = seed(history, &historyStats);
    // Pending batches are copied too, so a part-filled batch is not lost — but a copied
    // batch is never committed here. The commit path owns the dispersion gate and the
    // auto-reset check, and a median minted by a migration would skip both.
    const QJsonObject newBatch = seed(batch);

    const bool changed = (newHistory != history) || (newBatch != batch);

    // The third door to the same unrecoverable state, and the one the first two guards miss: a
    // NON-empty pair map that matches nothing. Every bucket's profile segment fails the
    // basketsByProfile lookup, nothing is copied, and the flag would close over data no reader
    // will look at again. Reachable when every profile was renamed (old shots carry the old
    // title, whose slug is not the filename the bucket is keyed under), when buckets were keyed
    // on a raw TITLE by the load-profile-from-a-shot path, or when all SAW-trained profiles sit
    // outside the shot window. With pre-basket buckets present AND profiles in the window, zero
    // matches is far likelier a key-derivation defect than a user who retired every profile.
    //
    // "Created nothing" is NOT that state when every target already exists, and conflating the two
    // was a live defect rather than a wording nit. sawLearningImport() clears the flag
    // unconditionally (:679), and a donor device is normally already seeded — its three-segment
    // buckets are all present, beside the two-segment leftovers this seed deliberately keeps for
    // rollback. So the first launch after a device transfer created nothing, warned, and returned
    // without closing the flag: the same WARN every launch forever, blaming the key derivation for
    // a store that was simply already copied. Verified by clearing the flag on a seeded store and
    // launching. matched > 0 is what separates them; matched == 0 means every pre-basket bucket's
    // profile segment failed the lookup, which is the genuine derivation failure this guards.
    const bool everyMatchAlreadyCopied =
        historyStats.matched > 0 && historyStats.alreadyFull == historyStats.matched;
    if (newHistory.size() == history.size() && preBasketCount > 0 && !everyMatchAlreadyCopied) {
        SAWC_WARN(QStringLiteral("Basket seed matched none of %1 pre-basket bucket(s) against %2 "
                                 "profile(s) in the window — treating as a key-derivation "
                                 "failure, retrying next launch")
                      .arg(preBasketCount).arg(basketsByProfile.size()));
        return;
    }

    if (newHistory != history) savePerProfileSawHistoryMap(newHistory);
    if (newBatch != batch) savePerProfileSawBatchMap(newBatch);

    // Verify the copies reached disk BEFORE closing the one-way flag: savePerProfile* marks its
    // cache valid unconditionally, so a failed write is invisible for the rest of the session.
    // Same sync+status check settings_hardware.cpp:145 uses for a far less consequential value.
    if (changed) {
        m_settings.sync();
        if (m_settings.status() != QSettings::NoError) {
            SAWC_WARN(QStringLiteral("Seeded SAW buckets did not persist (QSettings status %1) — "
                                     "leaving the seed open so it retries next launch")
                          .arg(static_cast<int>(m_settings.status())));
            invalidateCache();   // do not serve an unpersisted map as though it were stored
            return;
        }
    }

    // Count the buckets left behind BEFORE closing the flag: once set, they are unreachable
    // unless the flag is reopened (resetSawLearning / sawLearningImport), so this is the only
    // record that they existed.
    int orphaned = 0;
    for (auto it = history.begin(); it != history.end(); ++it) {
        if (it.key().count(QStringLiteral("::")) != 1 || it.value().toArray().isEmpty()) continue;
        const QString profile = it.key().left(it.key().indexOf(QStringLiteral("::")));
        if (basketsByProfile.value(profile).isEmpty()) ++orphaned;
    }

    if (historyComplete) m_settings.setValue("saw/basketKeyMigrated", true);

    // INFO, and on EVERY outcome including "created nothing". This is a one-time
    // irreversible re-keying of the user's learning history, so it is user-visible per
    // LOGGING.md's audience rule — and the destructive case is exactly changed==false, which
    // the first version was the one case not to log. Counts are buckets CREATED, not map
    // sizes: logging newHistory.size() said "9 history bucket(s)" on a run that created two.
    SAWC_INFO(
        QStringLiteral("Basket seed (%1): %2 profile(s) in window, created %3 history and %4 "
                       "pending bucket(s), %5 pre-basket bucket(s) had no basket in the window")
            .arg(historyComplete ? QStringLiteral("complete") : QStringLiteral("partial"))
            .arg(basketsByProfile.size())
            .arg(newHistory.size() - history.size())
            .arg(newBatch.size() - batch.size())
            .arg(orphaned));
    if (orphaned > 0 && historyComplete) {
        SAWC_WARN(QStringLiteral("%1 pre-basket SAW bucket(s) closed out unseeded — their "
                                 "learning is no longer read").arg(orphaned));
    }
    if (changed) emit sawLearnedLagChanged();
}

void SettingsCalibration::migrateScaleTypeIds() {
    // (A) Global pool: rewrite each entry's "scale" field to its canonical id.
    {
        const QByteArray data = m_settings.value("saw/learningHistory").toByteArray();
        if (!data.isEmpty()) {
            QJsonParseError perr;
            const QJsonDocument doc = QJsonDocument::fromJson(data, &perr);
            if (perr.error != QJsonParseError::NoError) {
                // Surface corruption instead of silently rewriting nothing. The
                // global-pool reader (ensureSawCacheLoaded) already tolerates a bad
                // blob as empty, so leave the bytes for that path rather than reset.
                SAWC_WARN(QStringLiteral("migrate: corrupt learningHistory JSON — skipping "
                                         "global-pool rewrite: %1").arg(perr.errorString()));
            } else {
                QJsonArray arr = doc.array();
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
    }

    // Rewrite a "profile::scaleType" map: keys + per-entry "scale" -> ids, merging
    // colliding buckets (concatenate both, keep the newest `trim`). Iteration is
    // QJsonObject key-sorted, NOT chronological, so the post-merge order is arbitrary —
    // acceptable because a collision requires a pre-existing id bucket for a scale whose
    // legacy data was display-name-keyed (vanishingly rare) and we only care about not
    // losing data.
    auto migrateMap = [](const QJsonObject& in, qsizetype trim) -> QJsonObject {
        QJsonObject out;
        for (auto it = in.begin(); it != in.end(); ++it) {
            const QString key = it.key();
            QString newKey = key;
            // Two-segment keys only. A three-segment key already carries a basket, and its
            // LAST segment is that basket rather than the scale — normalizing it would
            // rewrite the basket as if it were a scale id. In practice this migration runs
            // from Settings init, before any shot can write a per-basket key, so the guard
            // is for the ordering nobody has promised to preserve rather than for a case
            // seen today.
            const qsizetype sep = key.lastIndexOf(QStringLiteral("::"));
            if (sep >= 0 && key.count(QStringLiteral("::")) == 1) {
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
                const QJsonArray existing = out.value(newKey).toArray();
                for (const auto& v : std::as_const(arr)) combined.append(v);       // this key's entries
                for (const auto& v : std::as_const(existing)) combined.append(v);  // entries already placed under newKey
                while (combined.size() > trim) combined.removeFirst();             // keep the newest `trim`
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

    SAWC_LOG(QStringLiteral("Migrated storage to canonical scale type-ids"));
}
