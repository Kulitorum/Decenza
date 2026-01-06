#pragma once

#include "scalebletransport.h"
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QBluetoothDeviceInfo>
#include <QBluetoothAddress>
#include <QMap>

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
    void onServiceError(QLowEnergyService::ServiceError err);

private:
    QLowEnergyService* getOrCreateService(const QBluetoothUuid& serviceUuid);
    void connectServiceSignals(QLowEnergyService* service);

    QLowEnergyController* m_controller = nullptr;
    QMap<QBluetoothUuid, QLowEnergyService*> m_services;
    QString m_deviceAddress;
    QString m_deviceName;
    bool m_connected = false;
};
