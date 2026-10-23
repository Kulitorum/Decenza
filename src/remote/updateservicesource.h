#pragma once

#include "rep_updateservice_source.h"
#include "serviceoperation.h"

#include <QPointer>
#include <functional>

class FirmwareUpdater;
class DE1Device;
class SettingsApp;
class UpdateChecker;

class NativeUpdateServiceSource final : public UpdateStateSimpleSource
{
    Q_OBJECT
public:
    NativeUpdateServiceSource(UpdateChecker* appUpdater, FirmwareUpdater* firmwareUpdater,
                              SettingsApp* settingsApp, DE1Device* device,
                              std::function<void()> checkAllUpdates,
                              QObject* parent = nullptr);
    void refresh() override;
    void execute(QString requestId, QString operation, QVariantMap arguments) override;

private:
    QVariantMap capture() const;
    void publish();
    void finish(const QString& requestId, const QString& operation, bool success,
                const QVariantMap& result = {}, const QString& reason = {});

    QPointer<UpdateChecker> m_appUpdater;
    QPointer<FirmwareUpdater> m_firmwareUpdater;
    QPointer<SettingsApp> m_settingsApp;
    QPointer<DE1Device> m_device;
    std::function<void()> m_checkAllUpdates;
    ServiceRequestIds m_requests;
    int m_installedVersion = 0;
};
