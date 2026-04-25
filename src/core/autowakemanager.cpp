#include "autowakemanager.h"
#include "settings_autowake.h"
#include <QDateTime>
#include <QDebug>

AutoWakeManager::AutoWakeManager(SettingsAutoWake* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_checkTimer(new QTimer(this))
{
    m_checkTimer->setSingleShot(true);
    connect(m_checkTimer, &QTimer::timeout, this, &AutoWakeManager::onTimerFired);

    // Reschedule when settings change
    connect(m_settings, &SettingsAutoWake::autoWakeScheduleChanged, this, [this]() {
        qDebug() << "AutoWakeManager: Schedule changed, rescheduling";
        m_lastTriggeredDates.clear();
        scheduleNextWake();
    });
}

void AutoWakeManager::onTimerFired() {
    qDebug() << "AutoWakeManager: *** WAKE TIME REACHED ***";

    // Mark today as triggered for this day of week
    QDate today = QDate::currentDate();
    int dayOfWeek = today.dayOfWeek() - 1;
    m_lastTriggeredDates[dayOfWeek] = today;

    emit wakeRequested();

    // Schedule the next wake
    scheduleNextWake();
}

void AutoWakeManager::scheduleNextWake() {
    m_checkTimer->stop();

    QDateTime now = QDateTime::currentDateTime();
    QVariantList schedule = m_settings->autoWakeSchedule();

    static const char* dayNames[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

    // Find the next wake time across all days (up to 8 days ahead to cover same day next week)
    qint64 minMsToWake = -1;
    int wakeDay = -1;
    QTime wakeTime;

    for (int daysAhead = 0; daysAhead < 8; ++daysAhead) {
        QDate checkDate = now.date().addDays(daysAhead);
        int dayOfWeek = checkDate.dayOfWeek() - 1; // 0=Mon, 6=Sun

        if (dayOfWeek < 0 || dayOfWeek >= schedule.size()) continue;

        QVariantMap daySchedule = schedule[dayOfWeek].toMap();
        if (!daySchedule.value("enabled", false).toBool()) continue;

        int hour = daySchedule.value("hour", 7).toInt();
        int minute = daySchedule.value("minute", 0).toInt();
        QDateTime wakeDateTime(checkDate, QTime(hour, minute, 1)); // 1 second into the minute

        // Skip if this wake time has already passed or already triggered today
        if (wakeDateTime <= now) continue;
        if (m_lastTriggeredDates.value(dayOfWeek) == checkDate) continue;

        qint64 msToWake = now.msecsTo(wakeDateTime);
        if (minMsToWake < 0 || msToWake < minMsToWake) {
            minMsToWake = msToWake;
            wakeDay = dayOfWeek;
            wakeTime = QTime(hour, minute);
        }

        // Found the nearest one, no need to check further days
        break;
    }

    if (minMsToWake > 0) {
        qint64 totalSeconds = minMsToWake / 1000;
        qint64 hours = totalSeconds / 3600;
        qint64 minutes = (totalSeconds % 3600) / 60;
        qint64 seconds = totalSeconds % 60;

        QString timeStr;
        if (hours > 0) {
            timeStr = QString("%1h %2m").arg(hours).arg(minutes);
        } else if (minutes > 0) {
            timeStr = QString("%1m %2s").arg(minutes).arg(seconds);
        } else {
            timeStr = QString("%1s").arg(seconds);
        }

        qDebug() << "AutoWakeManager: Next wake:" << dayNames[wakeDay]
                 << wakeTime.toString("HH:mm") << "in" << timeStr;
        m_checkTimer->start(static_cast<int>(minMsToWake));
    } else {
        qDebug() << "AutoWakeManager: No wake times enabled";
    }
}

void AutoWakeManager::start() {
    qDebug() << "AutoWakeManager: Starting";
    scheduleNextWake();
}

void AutoWakeManager::stop() {
    qDebug() << "AutoWakeManager: Stopping";
    m_checkTimer->stop();
}
