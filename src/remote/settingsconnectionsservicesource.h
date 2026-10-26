#pragma once

#include "rep_settingsconnectionsservice_source.h"
#include "serviceoperation.h"
#include "manualwifirequestowner.h"
#include "ble/scaledeviceproxy.h"

#include <QPointer>
#include <QStringList>
#include <QVariantList>

class BLEManager;
class BelkaPortalDevice;
class DE1Device;
class HdsFirmwareUpdateController;
class MachineState;
class Settings;
class USBManager;
class UsbScaleManager;

// Native implementation delegates to the application's existing managers.
// `MachineControlService.scanForDevices()` remains the sole scan command.
class NativeSettingsConnectionsServiceSource final : public ConnectionsStateSimpleSource
{
    Q_OBJECT
public:
    NativeSettingsConnectionsServiceSource(Settings* settings, BLEManager* bleManager,
                                           DE1Device* de1Device, ScaleDeviceProxy* scaleDevice,
                                           MachineState* machineState,
                                           USBManager* usbManager,
                                           UsbScaleManager* usbScaleManager,
                                           BelkaPortalDevice* portalDevice,
                                           HdsFirmwareUpdateController* hdsFirmwareUpdate,
                                           QObject* parent = nullptr);

    void execute(QString requestId, QString operation, QVariantMap arguments) override;
    void refresh() override;

private:
    void refreshSnapshot();
    void finish(const QString& requestId, const QString& operation, bool success,
                const QVariantMap& result = {}, const QString& reason = {});
    bool exactArguments(const QVariantMap& arguments,
                        const QStringList& keys) const;
    QString boundedText(const QVariantMap& arguments, const QString& key,
                        int maxLength = 256) const;
    bool containsAddress(const QVariantList& rows, const QString& key,
                         const QString& address) const;

    QPointer<Settings> m_settings;
    QPointer<BLEManager> m_bleManager;
    QPointer<DE1Device> m_de1Device;
    QPointer<ScaleDeviceProxy> m_scaleDevice;
    QPointer<MachineState> m_machineState;
#ifndef Q_OS_IOS
    QPointer<USBManager> m_usbManager;
    QPointer<UsbScaleManager> m_usbScaleManager;
#endif
    QPointer<BelkaPortalDevice> m_portalDevice;
    QPointer<HdsFirmwareUpdateController> m_hdsFirmwareUpdate;
    ServiceRequestIds m_requestIds;
    bool m_manualWifiProbeInProgress = false;
    bool m_startingManualWifiConnect = false;
    ManualWifiRequestOwner m_manualWifiProbeOwner;
    ManualWifiRequestOwner m_manualWifiConnectOwner;
    QString m_manualWifiHostname;
    QString m_manualWifiIp;
    QString m_manualWifiError;
    int m_knownScalesRevision = 1;
    int m_refractometerRevision = 1;
};
