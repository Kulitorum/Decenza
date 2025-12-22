#pragma once

#include <QObject>
#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>
#include <QList>
#include <QVariant>

class BLEManager : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool scanning READ isScanning NOTIFY scanningChanged)
    Q_PROPERTY(QVariantList discoveredDevices READ discoveredDevices NOTIFY devicesChanged)
    Q_PROPERTY(QVariantList discoveredScales READ discoveredScales NOTIFY scalesChanged)

public:
    explicit BLEManager(QObject* parent = nullptr);
    ~BLEManager();

    bool isScanning() const;
    QVariantList discoveredDevices() const;
    QVariantList discoveredScales() const;

public slots:
    void startScan();
    void stopScan();
    void clearDevices();

signals:
    void scanningChanged();
    void devicesChanged();
    void scalesChanged();
    void de1Discovered(const QBluetoothDeviceInfo& device);
    void scaleDiscovered(const QBluetoothDeviceInfo& device, const QString& type);
    void errorOccurred(const QString& error);

private slots:
    void onDeviceDiscovered(const QBluetoothDeviceInfo& device);
    void onScanFinished();
    void onScanError(QBluetoothDeviceDiscoveryAgent::Error error);

private:
    bool isDE1Device(const QBluetoothDeviceInfo& device) const;
    QString getScaleType(const QBluetoothDeviceInfo& device) const;

    QBluetoothDeviceDiscoveryAgent* m_discoveryAgent = nullptr;
    QList<QBluetoothDeviceInfo> m_de1Devices;
    QList<QPair<QBluetoothDeviceInfo, QString>> m_scales;  // device, type
    bool m_scanning = false;
};
