#pragma once

#include "../scaledevice.h"
#include "../transport/scalebletransport.h"

class HiroiaScale : public ScaleDevice {
    Q_OBJECT

public:
    explicit HiroiaScale(ScaleBleTransport* transport, QObject* parent = nullptr);
    ~HiroiaScale() override;

    void connectToDevice(const QBluetoothDeviceInfo& device) override;
    QString name() const override { return m_name; }
    QString type() const override { return ScaleTypeIds::scaleTypeId(ScaleType::HiroiaJimmy); }

public slots:
    void tare() override;
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
    QString m_name = "Hiroia Jimmy";
    bool m_serviceFound = false;
    bool m_characteristicsReady = false;
};
