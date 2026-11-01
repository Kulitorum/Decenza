#pragma once

#include "rep_hostsessionservice_source.h"
#include "network/machineuisnapshot.h"

#include <QPointer>
#include <QTimer>

class Settings;
class DE1Device;
class MachineState;
class MainController;
class ScaleDeviceProxy;
class BLEManager;
class UsbScaleManager;
class BelkaPortalDevice;
class AccessibilityManager;

class NativeHostSessionServiceSource final : public HostSessionStateSimpleSource
{
    Q_OBJECT
public:
    NativeHostSessionServiceSource(Settings* settings, DE1Device* device,
        MachineState* machineState, MainController* controller, ScaleDeviceProxy* scale,
        BLEManager* ble, UsbScaleManager* usbScale, BelkaPortalDevice* portal,
        AccessibilityManager* accessibility, QObject* parent = nullptr);
    void setIdentity(const QString& packageId, const QString& hostId);
    void setAppearance(const QVariantMap& appearance);
    MachineUiSnapshot machineUiSnapshot() const;
    static MachineUiSnapshot captureMachineUi(DE1Device* m_device,
        MachineState* m_machineState, MainController* m_mainController,
        ScaleDeviceProxy* m_scaleDeviceProxy, BLEManager* m_bleManager,
        UsbScaleManager* m_usbScaleManager, Settings* m_settings,
        BelkaPortalDevice* m_belkaPortal);

private:
    void publish();
    QPointer<Settings> m_settings;
    QPointer<DE1Device> m_device;
    QPointer<MachineState> m_machineState;
    QPointer<MainController> m_mainController;
    QPointer<ScaleDeviceProxy> m_scaleDeviceProxy;
    QPointer<BLEManager> m_bleManager;
    UsbScaleManager* m_usbScaleManager = nullptr;
    QPointer<BelkaPortalDevice> m_belkaPortal;
    QPointer<AccessibilityManager> m_accessibility;
    QVariantMap m_appearance;
    qint64 m_appearanceBytes = -1;
    QString m_packageId;
    QString m_hostId;
    quint64 m_sequence = 0;
    QTimer m_publishTimer;
};
