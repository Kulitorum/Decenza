#pragma once

#include "scalebletransport.h"

#ifdef Q_OS_ANDROID

#include <QJniObject>

/**
 * Android native BLE transport implementation.
 * Uses JNI to call ScaleBleManager.java.
 *
 * Key advantage over Qt BLE:
 * - setCharacteristicNotification() is NOT reverted if CCCD write fails
 * - This fixes Bookoo and similar scales that reject CCCD writes
 */
class AndroidScaleBleTransport : public ScaleBleTransport {
    Q_OBJECT

public:
    explicit AndroidScaleBleTransport(QObject* parent = nullptr);
    ~AndroidScaleBleTransport() override;

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

    // Called from JNI
    void onConnected();
    void onDisconnected();
    void onServiceDiscovered(const QString& serviceUuid);
    void onServicesDiscoveryFinished();
    void onCharacteristicDiscovered(const QString& serviceUuid,
                                    const QString& charUuid, int properties);
    void onCharacteristicsDiscoveryFinished(const QString& serviceUuid);
    void onCharacteristicChanged(const QString& charUuid, const QByteArray& value);
    void onCharacteristicRead(const QString& charUuid, const QByteArray& value);
    void onCharacteristicWritten(const QString& charUuid);
    void onNotificationsEnabled(const QString& charUuid);
    void onError(const QString& message);

private:
    QJniObject m_javaBleManager;
    bool m_connected = false;
};

#endif // Q_OS_ANDROID
