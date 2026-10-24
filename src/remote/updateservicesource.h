#pragma once

#include "rep_updateservice_source.h"
#include "serviceoperation.h"

#include <QPointer>
#include <functional>

class FirmwareUpdater;
class DE1Device;
class SettingsApp;
class UpdateChecker;
class NativeImportServiceSource;
class QThread;

class NativeUpdateServiceSource final : public UpdateStateSimpleSource
{
    Q_OBJECT
public:
    NativeUpdateServiceSource(UpdateChecker* appUpdater, FirmwareUpdater* firmwareUpdater,
                              SettingsApp* settingsApp, DE1Device* device,
                              std::function<void()> checkAllUpdates,
                              QObject* parent = nullptr);
    void setImportService(NativeImportServiceSource* imports);
    void setInstallAllowed(std::function<bool()> allowed);
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
    QPointer<NativeImportServiceSource> m_imports;
    std::function<bool()> m_installAllowed;
    QPointer<QThread> m_apkWorker;
    QString m_apkRequest;
    std::function<void()> m_checkAllUpdates;
    ServiceRequestIds m_requests;
};
