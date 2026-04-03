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
        updateCachedStats(history, m_lastSteamFlow, m_lastSteamTemp);
    }
}

bool SteamHealthTracker::isComparable(const SteamSessionSummary& s, int steamFlow, int steamTemp) const {
    return qAbs(s.steamFlow - steamFlow) <= FLOW_TOLERANCE
        && qAbs(s.steamTemperature - steamTemp) <= TEMP_TOLERANCE;
}

void SteamHealthTracker::onSample(double pressure, double temperature) {
    if (!m_pressureWarningEmitted && pressure > PRESSURE_THRESHOLD) {
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
    if (samples < MIN_SAMPLES_FOR_ANALYSIS) {
        qDebug() << "SteamHealth [skip] only" << samples << "samples (need" << MIN_SAMPLES_FOR_ANALYSIS << ")";
        return;
    }

    // --- Absolute threshold check (matching de1app) ---

    const auto& pressureData = model->pressureData();
    const auto& temperatureData = model->temperatureData();

    int highPressureCount = 0;
    int highTempCount = 0;
    for (const auto& pt : pressureData) {
        if (pt.x() >= TRIM_SECONDS && pt.y() > PRESSURE_THRESHOLD)
            ++highPressureCount;
    }
    for (const auto& pt : temperatureData) {
        if (pt.x() >= TRIM_SECONDS && pt.y() > TEMPERATURE_THRESHOLD)
            ++highTempCount;
    }

    if (highPressureCount > CLOG_SAMPLE_THRESHOLD) {
        qWarning() << "SteamHealth [clog] descale warning -" << highPressureCount
                   << "samples exceeded" << PRESSURE_THRESHOLD << "bar";
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
    updateCachedStats(history, steamFlowSetting, steamTempSetting);

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
// Baselines:
//   Pressure: lowest recorded avgPressure across all comparable sessions.
//   Temperature: the user's steamTemperature setting (target). If actual temp
//     overshoots the target, that's the signal.
//
// Warning fires when current value has moved 60% of the way from baseline
// toward the hard threshold (8 bar / 180°C). Re-warns at most every 5 sessions.
//
// Auto-reset: If the newest session drops >= 30% of the range (relative to the
// rolling average of recent sessions) toward the threshold, we assume a descale
// happened and trim old sessions. Uses actual measured values for both pressure
// and temperature (not the target setting).

void SteamHealthTracker::checkTrend(QList<SteamSessionSummary>& history,
                                     int steamFlow, int steamTemp) {
    // Filter to sessions with matching settings (within tolerance)
    QList<const SteamSessionSummary*> comparable;
    for (const auto& s : history) {
        if (isComparable(s, steamFlow, steamTemp)) {
            comparable.append(&s);
        }
    }

    if (comparable.size() < MIN_SESSIONS_FOR_TREND) {
        qDebug() << "SteamHealth [trend] not enough comparable sessions:"
                 << comparable.size() << "/" << MIN_SESSIONS_FOR_TREND;
        return;
    }

    // Pressure baseline: lowest avgPressure across all comparable sessions
    double baselinePressure = comparable.first()->avgPressure;
    for (const auto* s : comparable) {
        baselinePressure = qMin(baselinePressure, s->avgPressure);
    }

    // Temperature baseline: the user's target setting
    double baselineTemp = static_cast<double>(steamTemp);

    // Current: most recent session
    double currentPressure = comparable.first()->avgPressure;
    double currentTemp = comparable.first()->avgTemperature;

    qsizetype n = comparable.size();

    // --- Auto-reset on meaningful drop (likely descale) ---
    // Compare the newest session against a rolling average of the previous few sessions
    // (not the all-time minimum, which would make auto-reset impossible to trigger).
    // For temperature, compare actual measurements (not the target setting).
    bool autoReset = false;
    qsizetype recentCount = qMin(qsizetype(5), n - 1);
    if (recentCount > 0) {
        double recentPressureSum = 0, recentTempSum = 0;
        for (qsizetype i = 1; i <= recentCount; ++i) {
            recentPressureSum += comparable[i]->avgPressure;
            recentTempSum += comparable[i]->avgTemperature;
        }
        double recentAvgPressure = recentPressureSum / recentCount;
        double recentAvgTemp = recentTempSum / recentCount;

        double pressureRange = PRESSURE_THRESHOLD - recentAvgPressure;
        if (pressureRange > 0) {
            double drop = recentAvgPressure - currentPressure;
            if (drop >= pressureRange * AUTO_RESET_DROP_THRESHOLD) {
                autoReset = true;
            }
        }
        double tempRange = TEMPERATURE_THRESHOLD - recentAvgTemp;
        if (!autoReset && tempRange > 0) {
            double drop = recentAvgTemp - currentTemp;
            if (drop >= tempRange * AUTO_RESET_DROP_THRESHOLD) {
                autoReset = true;
            }
        }
    }

    if (autoReset) {
        // Clear comparable list before mutating history (pointers would be invalidated)
        comparable.clear();

        qDebug() << "SteamHealth [auto-reset]"
                 << "pressure:" << currentPressure << "bar"
                 << "temp:" << currentTemp << "°C"
                 << "trimmedTo:" << AUTO_RESET_KEEP_SESSIONS << "sessions";

        while (history.size() > AUTO_RESET_KEEP_SESSIONS) {
            history.removeLast();
        }
        saveHistory(history);
        m_lastWarnedSession = -99;  // Allow warnings again after reset
        m_settings.setValue("steam/lastWarnedSession", m_lastWarnedSession);
        return;
    }

    // --- Compute progress toward thresholds ---

    double pressureRange = PRESSURE_THRESHOLD - baselinePressure;
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
             << "comparable:" << n
             << "baselineP:" << baselinePressure << "bar"
             << "currentP:" << currentPressure << "bar"
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
        qWarning() << "SteamHealth [warn] pressure at" << currentPressure
                   << "bar, baseline" << baselinePressure
                   << "bar (" << qRound(progressP * 100) << "% toward" << PRESSURE_THRESHOLD << "bar)";
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
                                            int steamFlow, int steamTemp) {
    QList<const SteamSessionSummary*> comparable;
    for (const auto& s : history) {
        if (isComparable(s, steamFlow, steamTemp)) {
            comparable.append(&s);
        }
    }

    if (comparable.isEmpty()) {
        m_baselinePressure = 0.0;
        m_baselineTemperature = 0.0;
        m_currentPressure = 0.0;
        m_currentTemperature = 0.0;
        return;
    }

    // Current = most recent
    m_currentPressure = comparable.first()->avgPressure;
    m_currentTemperature = comparable.first()->avgTemperature;

    // Pressure baseline = lowest recorded avgPressure
    m_baselinePressure = comparable.first()->avgPressure;
    for (const auto* s : comparable) {
        m_baselinePressure = qMin(m_baselinePressure, s->avgPressure);
    }

    // Temperature baseline = user's target setting
    m_baselineTemperature = static_cast<double>(steamTemp);
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
