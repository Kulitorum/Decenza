#pragma once

#include <QObject>
#include <QHash>
#include <QSet>
#include <QByteArray>
#include <QString>
#include <QBluetoothUuid>
#include <QBluetoothDeviceInfo>

#include "../scalebletransport.h"

class CoreBluetoothScaleBleTransport final : public ScaleBleTransport
{
    Q_OBJECT
public:
    explicit CoreBluetoothScaleBleTransport(QObject* parent = nullptr);
    ~CoreBluetoothScaleBleTransport() override;

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

private:
    void log(const QString& msg);

public:
    // PIMPL - must be public for ObjC delegate access
    struct Impl;
private:
    Impl* m_impl = nullptr;
};
