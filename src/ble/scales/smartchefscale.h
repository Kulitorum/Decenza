#pragma once

#include "../scaledevice.h"
#include "../transport/scalebletransport.h"

class SmartChefScale : public ScaleDevice {
    Q_OBJECT

public:
    explicit SmartChefScale(ScaleBleTransport* transport, QObject* parent = nullptr);
    ~SmartChefScale() override;

    void connectToDevice(const QBluetoothDeviceInfo& device) override;
    QString name() const override { return m_name; }
    QString type() const override { return ScaleTypeIds::scaleTypeId(ScaleType::SmartChef); }

public slots:
    void tare() override;  // SmartChef doesn't support software tare
    void sendKeepAlive() override;

private slots:
    void onTransportConnected();
    void onTransportDisconnected();
    void onTransportError(const QString& message);
    void onServiceDiscovered(const QBluetoothUuid& uuid);
    void onServicesDiscoveryFinished();
    void onCharacteristicsDiscoveryFinished(const QBluetoothUuid& serviceUuid);
    void onCharacteristicChanged(const QBluetoothUuid& characteristicUuid, const QByteArray& value);

private:
    ScaleBleTransport* m_transport = nullptr;
    QString m_name = "SmartChef";
    bool m_serviceFound = false;
    bool m_characteristicsReady = false;
};
