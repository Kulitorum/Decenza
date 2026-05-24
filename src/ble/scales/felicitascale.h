#pragma once

#include "../scaledevice.h"
#include "../transport/scalebletransport.h"

class FelicitaScale : public ScaleDevice {
    Q_OBJECT

public:
    explicit FelicitaScale(ScaleBleTransport* transport, QObject* parent = nullptr);
    ~FelicitaScale() override;

    void connectToDevice(const QBluetoothDeviceInfo& device) override;
    QString name() const override { return m_name; }
    QString type() const override { return ScaleTypeIds::scaleTypeId(ScaleType::Felicita); }

public slots:
    void tare() override;
    void startTimer() override;
    void stopTimer() override;
    void resetTimer() override;
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
    void parseResponse(const QByteArray& data);
    void sendCommand(uint8_t cmd);

    ScaleBleTransport* m_transport = nullptr;
    QString m_name = "Felicita";
    bool m_serviceFound = false;
    bool m_characteristicsReady = false;
};
