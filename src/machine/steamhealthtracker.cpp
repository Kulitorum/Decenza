#include "steamhealthtracker.h"
#include "models/steamdatamodel.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>
#include <algorithm>

SteamHealthTracker::SteamHealthTracker(QObject* parent)
    : QObject(parent)
    , m_settings("DecentEspresso", "DE1Qt")
{
    auto history = loadHistory();
    m_sessionCount = static_cast<int>(history.size());
    m_lastWarnedSession = m_settings.value("steam/lastWarnedSession", -99).toInt();
    m_lastSteamFlow = m_settings.value("steam/lastTrackedFlow", 0).toInt();
    m_lastSteamTemp = m_settings.value("steam/lastTrackedTemp", 0).toInt();
    if (!history.isEmpty()) {
        updateCachedStats(history, m_lastSteamTemp);
    }
}

double SteamHealthTracker::normalizePressure(double avgPressure, int steamFlow) const {
    return qMax(0.1, avgPressure - PRESSURE_PER_FLOW_UNIT * (steamFlow - REFERENCE_FLOW));
}

void SteamHealthTracker::onSample(double pressure, double temperature) {
    if (!m_pressureWarningEmitted && pressure > PRESSURE_HARD_LIMIT) {
        m_pressureWarningEmitted = true;
        emit pressureTooHigh();
    }
    if (!m_temperatureWarningEmitted && temperature > TEMPERATURE_THRESHOLD) {
        m_temperatureWarningEmitted = true;
        emit temperatureTooHigh();
    }
}

void SteamHealthTracker::resetSession() {
    m_pressureWarningEmitted = false;
    m_temperatureWarningEmitted = false;
}

void SteamHealthTracker::onSessionComplete(SteamDataModel* model, int steamFlowSetting, int steamTempSetting) {
    if (!model) return;

    int samples = model->sampleCount();
    int duration = static_cast<int>(model->rawTime());
    if (samples < MIN_SAMPLES_FOR_ANALYSIS || duration < MIN_DURATION_FOR_ANALYSIS) {
        qDebug() << "SteamHealth [skip]" << samples << "samples" << duration << "s"
                 << "(need" << MIN_SAMPLES_FOR_ANALYSIS << "samples," << MIN_DURATION_FOR_ANALYSIS << "s)";
        return;
    }

    // --- Absolute threshold check (matching de1app) ---

    const auto& pressureData = model->pressureData();
    const auto& temperatureData = model->temperatureData();

    int highPressureCount = 0;
    int highTempCount = 0;
    for (const auto& pt : pressureData) {
        if (pt.x() >= TRIM_SECONDS && pt.y() > PRESSURE_HARD_LIMIT)
            ++highPressureCount;
    }
    for (const auto& pt : temperatureData) {
        if (pt.x() >= TRIM_SECONDS && pt.y() > TEMPERATURE_THRESHOLD)
            ++highTempCount;
    }

    if (highPressureCount > CLOG_SAMPLE_THRESHOLD) {
        qWarning() << "SteamHealth [clog] descale warning -" << highPressureCount
                   << "samples exceeded" << PRESSURE_HARD_LIMIT << "bar";
        emit descaleWarning();
    }
    if (highTempCount > CLOG_SAMPLE_THRESHOLD) {
        qWarning() << "SteamHealth [clog] temperature warning -" << highTempCount
                   << "samples exceeded" << TEMPERATURE_THRESHOLD << "°C";
        emit temperatureWarning(
            tr("Your steam is getting too hot. Increase your steam flow rate or lower the steam temperature."));
    }

    // --- Save session summary ---

    SteamSessionSummary summary;
    summary.timestamp = QDateTime::currentDateTime();
    summary.avgPressure = model->averagePressure();
    summary.peakPressure = model->peakPressure();
    summary.avgTemperature = model->averageTemperature();
    summary.peakTemperature = model->peakTemperature();
    summary.steamFlow = steamFlowSetting;
    summary.steamTemperature = steamTempSetting;
    summary.durationSeconds = static_cast<int>(model->rawTime());

    // Note: loadHistory/saveHistory do QSettings I/O on the main thread.
    // At 150 sessions (~15KB JSON) this completes in <1ms — acceptable tradeoff
    // vs. the complexity of threading the entire load→analyze→save pipeline.
    auto history = loadHistory();
    history.prepend(summary);
    while (history.size() > MAX_HISTORY_SIZE) {
        history.removeLast();
    }
    saveHistory(history);

    qDebug() << "SteamHealth [session]"
             << "timestamp:" << summary.timestamp.toString(Qt::ISODate)
             << "avgP:" << summary.avgPressure << "bar"
             << "peakP:" << summary.peakPressure << "bar"
             << "avgT:" << summary.avgTemperature << "°C"
             << "peakT:" << summary.peakTemperature << "°C"
             << "flow:" << steamFlowSetting
             << "tempSetting:" << steamTempSetting
             << "duration:" << summary.durationSeconds << "s"
             << "sessions:" << history.size();

    // --- Trend detection (may auto-reset baseline on meaningful drop) ---

    checkTrend(history, steamFlowSetting, steamTempSetting);

    // Cache stats and persist last-used settings for startup
    m_lastSteamFlow = steamFlowSetting;
    m_lastSteamTemp = steamTempSetting;
    m_settings.setValue("steam/lastTrackedFlow", steamFlowSetting);
    m_settings.setValue("steam/lastTrackedTemp", steamTempSetting);
    updateCachedStats(history, steamTempSetting);

    emit sessionHistoryChanged();
}

void SteamHealthTracker::clearHistory() {
    m_settings.remove("steam/sessionHistory");
    m_settings.remove("steam/lastWarnedSession");
    m_sessionCount = 0;
    m_lastWarnedSession = -99;
    m_baselinePressure = 0.0;
    m_baselineTemperature = 0.0;
    m_currentPressure = 0.0;
    m_currentTemperature = 0.0;
    emit sessionHistoryChanged();
    qDebug() << "SteamHealth [reset] session history and warning cooldown cleared";
}

// --- Scale buildup trend detection ---
//
// All sessions are used regardless of flow/temperature settings. Pressures are
// normalized to REFERENCE_FLOW using a linear model so sessions at different
// flow settings are directly comparable. Raw values are stored; normalization
// is applied at read time so recalibrating the model doesn't require
// re-collecting data.
//
// Baselines:
//   Pressure: lowest normalized avgPressure across all sessions.
//   Temperature: lowest measured avgTemperature across all sessions.
//
// Warning fires when current value has moved 60% of the way from baseline
// toward the warn threshold (baseline x 3.0 for pressure, capped at 8 bar / 180°C
// for temperature). Re-warns at most every 5 sessions.
//
// Auto-reset: Pressure only. If the newest session's normalized pressure drops
// >= 30% of the range (relative to the rolling average of recent sessions),
// we assume a descale happened and trim old sessions. Temperature is excluded
// from auto-reset because temperature changes are driven by the heater setpoint,
// not limescale — changing the steam temperature setting would spuriously trigger.

void SteamHealthTracker::checkTrend(QList<SteamSessionSummary>& history,
                                     int steamFlow, int steamTemp) {
    qsizetype n = history.size();
    if (n < MIN_SESSIONS_FOR_TREND) {
        qDebug() << "SteamHealth [trend] not enough sessions:"
                 << n << "/" << MIN_SESSIONS_FOR_TREND;
        return;
    }

    // Normalize all pressures to reference flow
    double baselinePressure = normalizePressure(history.first().avgPressure, history.first().steamFlow);
    for (const auto& s : history) {
        baselinePressure = qMin(baselinePressure, normalizePressure(s.avgPressure, s.steamFlow));
    }

    // Temperature baseline: lowest measured temperature across all sessions.
    // This tracks real multi-session trends (scale buildup causes overshoot)
    // rather than comparing against the current setting (which changes when
    // the user adjusts their steam temperature preference).
    double baselineTemp = history.first().avgTemperature;
    for (const auto& s : history) {
        baselineTemp = qMin(baselineTemp, s.avgTemperature);
    }

    // Current: most recent session (normalized)
    double currentPressure = normalizePressure(history.first().avgPressure, history.first().steamFlow);
    double currentTemp = history.first().avgTemperature;

    // --- Auto-reset on meaningful pressure drop (likely descale) ---
    // Only pressure is used for auto-reset detection. Temperature changes are
    // driven by the heater setpoint, not limescale — a user changing their steam
    // temperature setting would spuriously trigger a reset.
    bool autoReset = false;
    qsizetype recentCount = qMin(qsizetype(5), n - 1);
    if (recentCount > 0) {
        double recentPressureSum = 0;
        for (qsizetype i = 1; i <= recentCount; ++i) {
            recentPressureSum += normalizePressure(history[i].avgPressure, history[i].steamFlow);
        }
        double recentAvgPressure = recentPressureSum / recentCount;

        double pressureWarnLevel = qMin(baselinePressure * PRESSURE_WARN_MULTIPLIER, PRESSURE_HARD_LIMIT);
        // Use baseline range so auto-reset fires even when recentAvg exceeds warn level
        double pressureRange = pressureWarnLevel - baselinePressure;
        if (pressureRange > 0) {
            double drop = recentAvgPressure - currentPressure;
            if (drop >= pressureRange * AUTO_RESET_DROP_THRESHOLD) {
                autoReset = true;
            }
        }
    }

    if (autoReset) {
        qDebug() << "SteamHealth [auto-reset]"
                 << "normalizedP:" << currentPressure << "bar"
                 << "temp:" << currentTemp << "°C"
                 << "trimmedTo:" << AUTO_RESET_KEEP_SESSIONS << "sessions";

        while (history.size() > AUTO_RESET_KEEP_SESSIONS) {
            history.removeLast();
        }
        saveHistory(history);
        m_lastWarnedSession = -99;
        m_settings.setValue("steam/lastWarnedSession", m_lastWarnedSession);
        return;
    }

    // --- Compute progress toward thresholds ---
    double pressureWarnLevel = qMin(baselinePressure * PRESSURE_WARN_MULTIPLIER, PRESSURE_HARD_LIMIT);
    double pressureRange = pressureWarnLevel - baselinePressure;
    double tempRange = TEMPERATURE_THRESHOLD - baselineTemp;

    double progressP = 0;
    if (pressureRange > 0 && currentPressure > baselinePressure) {
        progressP = (currentPressure - baselinePressure) / pressureRange;
    }

    double progressT = 0;
    if (tempRange > 0 && currentTemp > baselineTemp) {
        progressT = (currentTemp - baselineTemp) / tempRange;
    }

    qDebug() << "SteamHealth [trend]"
             << "sessions:" << n
             << "baselineP:" << baselinePressure << "bar (normalized)"
             << "currentP:" << currentPressure << "bar (normalized)"
             << "warnLevel:" << QString::number(pressureWarnLevel, 'f', 1) << "bar"
             << "progressP:" << QString::number(progressP, 'f', 2)
             << "baselineT:" << baselineTemp << "°C (target:" << steamTemp << ")"
             << "currentT:" << currentTemp << "°C"
             << "progressT:" << QString::number(progressT, 'f', 2)
             << "lastWarned:" << m_lastWarnedSession
             << "sessionCount:" << m_sessionCount;

    // --- Warning cooldown check ---
    if (m_sessionCount - m_lastWarnedSession < WARN_COOLDOWN_SESSIONS) {
        return;
    }

    // --- Emit warnings at 60% progress ---

    if (progressP >= TREND_PROGRESS_THRESHOLD) {
        qWarning() << "SteamHealth [warn] normalized pressure at" << currentPressure
                   << "bar, baseline" << baselinePressure
                   << "bar (" << qRound(progressP * 100) << "% toward" << pressureWarnLevel << "bar)";
        m_lastWarnedSession = m_sessionCount;
        m_settings.setValue("steam/lastWarnedSession", m_lastWarnedSession);
        emit scaleBuildupWarning(
            tr("Steam pressure has increased from %1 to %2 bar over %3 sessions. "
               "This may indicate scale buildup. Consider descaling your machine.")
            .arg(baselinePressure, 0, 'f', 1)
            .arg(currentPressure, 0, 'f', 1)
            .arg(n));
    }

    if (progressT >= TREND_PROGRESS_THRESHOLD) {
        qWarning() << "SteamHealth [warn] temperature at" << currentTemp
                   << "°C, target" << baselineTemp
                   << "°C (" << qRound(progressT * 100) << "% toward" << TEMPERATURE_THRESHOLD << "°C)";
        m_lastWarnedSession = m_sessionCount;
        m_settings.setValue("steam/lastWarnedSession", m_lastWarnedSession);
        emit scaleBuildupWarning(
            tr("Steam temperature has increased from %1 to %2 °C over %3 sessions. "
               "This may indicate scale buildup. Consider descaling your machine.")
            .arg(baselineTemp, 0, 'f', 1)
            .arg(currentTemp, 0, 'f', 1)
            .arg(n));
    }
}

void SteamHealthTracker::updateCachedStats(const QList<SteamSessionSummary>& history,
                                            int steamTemp) {
    if (history.isEmpty()) {
        m_baselinePressure = 0.0;
        m_baselineTemperature = 0.0;
        m_currentPressure = 0.0;
        m_currentTemperature = 0.0;
        return;
    }

    // Current = most recent session (normalized to reference flow)
    m_currentPressure = normalizePressure(history.first().avgPressure, history.first().steamFlow);
    m_currentTemperature = history.first().avgTemperature;

    // Pressure baseline = lowest normalized pressure across all sessions
    m_baselinePressure = m_currentPressure;
    for (const auto& s : history) {
        m_baselinePressure = qMin(m_baselinePressure, normalizePressure(s.avgPressure, s.steamFlow));
    }

    // Temperature baseline = lowest measured temperature across all sessions
    m_baselineTemperature = m_currentTemperature;
    for (const auto& s : history) {
        m_baselineTemperature = qMin(m_baselineTemperature, s.avgTemperature);
    }
}

// --- QSettings JSON persistence ---

QList<SteamSessionSummary> SteamHealthTracker::loadHistory() const {
    QList<SteamSessionSummary> result;
    QByteArray data = m_settings.value("steam/sessionHistory").toByteArray();
    if (data.isEmpty()) return result;

    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    for (const QJsonValue& v : arr) {
        QJsonObject obj = v.toObject();
        SteamSessionSummary s;
        s.timestamp = QDateTime::fromString(obj["timestamp"].toString(), Qt::ISODate);
        s.avgPressure = obj["avgPressure"].toDouble();
        s.peakPressure = obj["peakPressure"].toDouble();
        s.avgTemperature = obj["avgTemperature"].toDouble();
        s.peakTemperature = obj["peakTemperature"].toDouble();
        s.steamFlow = obj["steamFlow"].toInt();
        s.steamTemperature = obj["steamTemperature"].toInt();
        s.durationSeconds = obj["durationSeconds"].toInt();
        result.append(s);
    }
    return result;
}

void SteamHealthTracker::saveHistory(const QList<SteamSessionSummary>& history) {
    QJsonArray arr;
    for (const auto& s : history) {
        QJsonObject obj;
        obj["timestamp"] = s.timestamp.toString(Qt::ISODate);
        obj["avgPressure"] = s.avgPressure;
        obj["peakPressure"] = s.peakPressure;
        obj["avgTemperature"] = s.avgTemperature;
        obj["peakTemperature"] = s.peakTemperature;
        obj["steamFlow"] = s.steamFlow;
        obj["steamTemperature"] = s.steamTemperature;
        obj["durationSeconds"] = s.durationSeconds;
        arr.append(obj);
    }
    m_settings.setValue("steam/sessionHistory", QJsonDocument(arr).toJson());
    m_sessionCount = static_cast<int>(history.size());
}
