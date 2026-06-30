#pragma once

#include "scalebletransport.h"
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QBluetoothDeviceInfo>
#include <QBluetoothAddress>
#include <QMap>
#include <QElapsedTimer>
#include "bleprioritydetector.h"

/**
 * Qt-based BLE transport implementation.
 * Uses QLowEnergyController and QLowEnergyService.
 * Works well on desktop platforms.
 */
class QtScaleBleTransport : public ScaleBleTransport {
    Q_OBJECT

public:
    explicit QtScaleBleTransport(QObject* parent = nullptr);
    ~QtScaleBleTransport() override;

    void connectToDevice(const QString& address, const QString& name) override;
    void connectToDevice(const QBluetoothDeviceInfo& device) override;
    void disconnectFromDevice() override;
    void discoverServices() override;
    void discoverCharacteristics(const QBluetoothUuid& serviceUuid) override;
    void enableNotifications(const QBluetoothUuid& serviceUuid,
                            const QBluetoothUuid& characteristicUuid) override;
    void writeCharacteristic(const QBluetoothUuid& serviceUuid,
                            const QBluetoothUuid& characteristicUuid,
                            const QByteArray& data,
                            WriteType writeType = WriteType::WithResponse) override;
    void readCharacteristic(const QBluetoothUuid& serviceUuid,
                           const QBluetoothUuid& characteristicUuid) override;
    bool isConnected() const override;

    void setSkipHighPriority(bool skip) override { m_priority.setSkipHighPriority(skip); }
    void setConnectionPriorityManaged(bool managed) override { m_connectionPriorityManaged = managed; }

public slots:
    // Connection-priority detection (#1093/#1176). Correlated against the
    // internal HIGH-request window; ≥ kDe1FaultThreshold faults in-window, or
    // any in-shot scale-feed stall, triggers the skip-HIGH + self-reconnect
    // backoff. No-ops once backed off or when starting at BALANCED.
    void onDe1LinkFault(const QString& kind) override;
    void onScaleFeedStalled(qint64 gapMs) override;
    void onScaleFeedStallConfirmed(qint64 gapMs) override;
    void onScaleFeedResumed(qint64 gapMs) override;
    void setShotActive(bool active) override;

private slots:
    void onControllerConnected();
    void onControllerDisconnected();
    void onControllerError(QLowEnergyController::Error err);
    void onServiceDiscovered(const QBluetoothUuid& uuid);
    void onServiceDiscoveryFinished();
    void onServiceStateChanged(QLowEnergyService::ServiceState state);
    void onCharacteristicChanged(const QLowEnergyCharacteristic& c, const QByteArray& value);
    void onCharacteristicRead(const QLowEnergyCharacteristic& c, const QByteArray& value);
    void onCharacteristicWritten(const QLowEnergyCharacteristic& c);
    void onDescriptorWritten(const QLowEnergyDescriptor& d, const QByteArray& value);
    void onServiceError(QLowEnergyService::ServiceError err);

private:
    void log(const QString& message);
    void warn(const QString& message);  // qWarning() + logMessage — for significant events
    QLowEnergyService* getOrCreateService(const QBluetoothUuid& serviceUuid);
    void connectServiceSignals(QLowEnergyService* service);
    // Disconnect; existing scale auto-reconnect brings this same transport
    // object back, and onControllerConnected() then skips HIGH (the detector
    // has latched skip-HIGH) so the link comes up at BALANCED.
    // `triggerKind` is the stable MCP/diagnostic tag for the latch metadata
    // ("de1-fault-cluster" or "scale-feed-stall").
    void triggerScaleBackoff(const char* reason, const QString& triggerKind);
    // Observe-mode counterpart of triggerScaleBackoff(): WARN-logs the
    // would-have-fired event and records it on the BLEManager observe ring,
    // but takes NO action (no latch, no disconnect, link stays HIGH).
    // `stallSec` < 0 means "not applicable" (e.g. DE1-fault-cluster).
    void logWouldBackoff(const QString& reason, const QString& triggerKind,
                         double stallSec);
    int64_t nowMs();  // monotonic ms for the detector window
    // True only when the controller is connected/discovered. Guards write/read/
    // notify so a periodic write (e.g. a scale heartbeat) issued after the link
    // dropped isn't handed to a torn-down QLowEnergyController — the same
    // write-to-a-dead-link bug class as the iOS-only crashes #1400/#1405, applied
    // here defensively. This is the Android + Windows/Linux scale+refractometer
    // transport (Darwin uses CoreBluetoothScaleBleTransport, which got its own
    // guard; the DE1's BleTransport got the matching write guard).
    bool isLinkReady() const;

    QLowEnergyController* m_controller = nullptr;
    QMap<QBluetoothUuid, QLowEnergyService*> m_services;
    QString m_deviceAddress;
    QString m_deviceName;
    QString m_deviceId;  // UUID on iOS, address on other platforms - for duplicate detection
    bool m_connected = false;
    // True from EspressoPreheating through shot end (fed by MachineState in
    // main.cpp). Gates triggerScaleBackoff(): defer the teardown while a shot
    // is in progress, bounce only when idle. #1176.
    bool m_shotActive = false;

    // False when this transport drives a non-scale link (refractometer): the
    // connection-priority enforcement and scale-feed-stall detection are
    // scale-only and would destabilize / log-spam a device that never sends
    // weight. Default true keeps every scale path unchanged.
    bool m_connectionPriorityManaged = true;

    // Connection-priority backoff. Pure decision logic in a Qt-free helper
    // (unit-tested in isolation); it lives on this transport, which persists
    // across the backoff-induced reconnect, so the latched skip-HIGH state
    // survives the bounce.
    BlePriorityDetector m_priority;
    QElapsedTimer m_clock;  // monotonic source feeding m_priority's window
};
