#pragma once

#include <QObject>
#include <QSettings>
#include <QVariantList>

class SettingsAutoWake : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool autoWakeEnabled READ autoWakeEnabled WRITE setAutoWakeEnabled NOTIFY autoWakeEnabledChanged)
    Q_PROPERTY(QVariantList autoWakeSchedule READ autoWakeSchedule WRITE setAutoWakeSchedule NOTIFY autoWakeScheduleChanged)
    Q_PROPERTY(bool autoWakeStayAwakeEnabled READ autoWakeStayAwakeEnabled WRITE setAutoWakeStayAwakeEnabled NOTIFY autoWakeStayAwakeEnabledChanged)
    Q_PROPERTY(int autoWakeStayAwakeMinutes READ autoWakeStayAwakeMinutes WRITE setAutoWakeStayAwakeMinutes NOTIFY autoWakeStayAwakeMinutesChanged)

public:
    explicit SettingsAutoWake(QObject* parent = nullptr);

    bool autoWakeEnabled() const;
    void setAutoWakeEnabled(bool enabled);

    QVariantList autoWakeSchedule() const;
    void setAutoWakeSchedule(const QVariantList& schedule);

    Q_INVOKABLE void setAutoWakeDayEnabled(int dayIndex, bool enabled);
    Q_INVOKABLE void setAutoWakeDayTime(int dayIndex, int hour, int minute);

    bool autoWakeStayAwakeEnabled() const;
    void setAutoWakeStayAwakeEnabled(bool enabled);

    int autoWakeStayAwakeMinutes() const;
    void setAutoWakeStayAwakeMinutes(int minutes);

signals:
    void autoWakeEnabledChanged();
    void autoWakeScheduleChanged();
    void autoWakeStayAwakeEnabledChanged();
    void autoWakeStayAwakeMinutesChanged();

private:
    mutable QSettings m_settings;
};
