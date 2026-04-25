#include "settings_autowake.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

SettingsAutoWake::SettingsAutoWake(QObject* parent)
    : QObject(parent)
    , m_settings("DecentEspresso", "DE1Qt")
{
}

bool SettingsAutoWake::autoWakeEnabled() const {
    return m_settings.value("autoWake/enabled", false).toBool();
}

void SettingsAutoWake::setAutoWakeEnabled(bool enabled) {
    if (autoWakeEnabled() != enabled) {
        m_settings.setValue("autoWake/enabled", enabled);
        emit autoWakeEnabledChanged();
    }
}

QVariantList SettingsAutoWake::autoWakeSchedule() const {
    QByteArray data = m_settings.value("autoWake/schedule").toByteArray();
    if (data.isEmpty()) {
        // Return default schedule: all days disabled, 07:00
        QVariantList defaultSchedule;
        for (int i = 0; i < 7; ++i) {
            QVariantMap day;
            day["enabled"] = false;
            day["hour"] = 7;
            day["minute"] = 0;
            defaultSchedule.append(day);
        }
        return defaultSchedule;
    }
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    QVariantList result;
    for (const QJsonValue& v : arr) {
        result.append(v.toObject().toVariantMap());
    }
    return result;
}

void SettingsAutoWake::setAutoWakeSchedule(const QVariantList& schedule) {
    QJsonArray arr;
    for (const QVariant& v : schedule) {
        arr.append(QJsonObject::fromVariantMap(v.toMap()));
    }
    m_settings.setValue("autoWake/schedule", QJsonDocument(arr).toJson());
    emit autoWakeScheduleChanged();
}

void SettingsAutoWake::setAutoWakeDayEnabled(int dayIndex, bool enabled) {
    if (dayIndex < 0 || dayIndex > 6) return;

    QVariantList schedule = autoWakeSchedule();
    QVariantMap day = schedule[dayIndex].toMap();
    day["enabled"] = enabled;
    schedule[dayIndex] = day;
    setAutoWakeSchedule(schedule);
}

void SettingsAutoWake::setAutoWakeDayTime(int dayIndex, int hour, int minute) {
    if (dayIndex < 0 || dayIndex > 6) return;
    if (hour < 0 || hour > 23) return;
    if (minute < 0 || minute > 59) return;

    QVariantList schedule = autoWakeSchedule();
    QVariantMap day = schedule[dayIndex].toMap();
    day["hour"] = hour;
    day["minute"] = minute;
    schedule[dayIndex] = day;
    setAutoWakeSchedule(schedule);
}

bool SettingsAutoWake::autoWakeStayAwakeEnabled() const {
    return m_settings.value("autoWake/stayAwakeEnabled", false).toBool();
}

void SettingsAutoWake::setAutoWakeStayAwakeEnabled(bool enabled) {
    if (autoWakeStayAwakeEnabled() != enabled) {
        m_settings.setValue("autoWake/stayAwakeEnabled", enabled);
        emit autoWakeStayAwakeEnabledChanged();
    }
}

int SettingsAutoWake::autoWakeStayAwakeMinutes() const {
    return m_settings.value("autoWake/stayAwakeMinutes", 120).toInt();
}

void SettingsAutoWake::setAutoWakeStayAwakeMinutes(int minutes) {
    if (autoWakeStayAwakeMinutes() != minutes) {
        m_settings.setValue("autoWake/stayAwakeMinutes", minutes);
        emit autoWakeStayAwakeMinutesChanged();
    }
}
