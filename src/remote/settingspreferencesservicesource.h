#pragma once

#include "rep_settingspreferencesservice_source.h"
#include "serviceoperation.h"

#include <QPointer>

class Settings;
class ScreensaverVideoManager;
class BatteryManager;
class ShotReporter;
class AccessibilityManager;
class AIManager;

class NativeSettingsPreferencesSource final : public SettingsPreferencesStateSimpleSource
{
    Q_OBJECT
public:
    explicit NativeSettingsPreferencesSource(Settings* settings, QObject* parent = nullptr);
    void setScreensaverManager(ScreensaverVideoManager* manager);
    void setBatteryManager(BatteryManager* manager);
    void setShotReporter(ShotReporter* reporter);
    void setAccessibilityManager(AccessibilityManager* manager);
    void setAIManager(AIManager* manager);

    void refresh() override;
    void setPreference(QString requestId, QString domain, QString name,
                       QVariant value, quint64 expectedRevision) override;
    void refreshShotMapLocation() override;
    void openShotMapLocationSettings() override;

signals:
    // The two heater-policy preferences must also be applied to the connected
    // machine by the native controller after their persisted value changes.
    void steamSettingsApplyRequested();

private:
    void refreshDomain(const QString& domain);
    QVariantMap capturePreferences() const;
    void finishPreference(const QString& requestId, const QString& domain,
                          const QString& name, bool success, const QString& reason);
    bool validateAndApply(const QString& domain, const QString& name,
                          const QVariant& value, QString* reason);
    quint64 domainRevision(const QString& domain) const;

    QPointer<Settings> m_settings;
    QPointer<ScreensaverVideoManager> m_screensaverManager;
    QPointer<BatteryManager> m_batteryManager;
    QPointer<ShotReporter> m_shotReporter;
    QPointer<AccessibilityManager> m_accessibilityManager;
    QPointer<AIManager> m_aiManager;
    bool m_shotMapGpsEnabled = false;
    QVariantMap m_preferences;
    QVariantMap m_revisions;
    ServiceRequestIds m_requests;
};
