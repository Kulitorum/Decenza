#pragma once

#include "rep_calibrationservice_source.h"
#include "serviceoperation.h"

#include <QPointer>

class DE1Device;
class MachineState;
class ProfileManager;
class Settings;
class SteamHealthTracker;

class NativeCalibrationSource final : public CalibrationStateSimpleSource
{
    Q_OBJECT
public:
    NativeCalibrationSource(Settings* settings, ProfileManager* profiles,
                            MachineState* machineState, DE1Device* device,
                            SteamHealthTracker* steamHealth, QObject* parent = nullptr);

    void refresh() override;
    void execute(QString requestId, QString operation, QVariantMap arguments,
                 qint64 expectedRevision) override;

private:
    void publish(bool changed = false);
    void hostChanged();
    void finish(const QString& requestId, const QString& operation, bool success,
                const QVariantMap& result = {}, const QString& reason = {});
    QVariantMap capture() const;

    QPointer<Settings> m_settings;
    QPointer<ProfileManager> m_profiles;
    QPointer<MachineState> m_machineState;
    QPointer<DE1Device> m_device;
    QPointer<SteamHealthTracker> m_steamHealth;
    ServiceRequestIds m_requests;
    qint64 m_revision = 0;
    bool m_applyingOperation = false;
    bool m_hostChangedDuringOperation = false;
};
