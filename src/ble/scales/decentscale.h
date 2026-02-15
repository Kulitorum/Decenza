#pragma once

#include "../scaledevice.h"
#include "../transport/scalebletransport.h"
#include <QTimer>

class DecentScale : public ScaleDevice {
    Q_OBJECT

public:
    explicit DecentScale(ScaleBleTransport* transport, QObject* parent = nullptr);
    ~DecentScale() override;

    void connectToDevice(const QBluetoothDeviceInfo& device) override;
    QString name() const override { return m_name; }
    QString type() const override { return "decent"; }

public slots:
    void tare() override;
    void sendKeepAlive() override;
    void startTimer() override;
    void stopTimer() override;
    void resetTimer() override;
    void sleep() override;
    void wake() override;
    void disableLcd() override;
    void setLed(int r, int g, int b);

private slots:
    void onTransportConnected();
    void onTransportDisconnected();
    void onTransportError(const QString& message);
    void onServiceDiscovered(const QBluetoothUuid& uuid);
    void onServicesDiscoveryFinished();
    void onCharacteristicsDiscoveryFinished(const QBluetoothUuid& serviceUuid);
    void onCharacteristicChanged(const QBluetoothUuid& characteristicUuid, const QByteArray& value);

private:
    void parseWeightData(const QByteArray& data);
    void sendCommand(const QByteArray& command);
    void sendHeartbeat();
    void startHeartbeat();
    void stopHeartbeat();
    uint8_t calculateXor(const QByteArray& data);

    ScaleBleTransport* m_transport = nullptr;
    QString m_name = "Decent Scale";
    bool m_serviceFound = false;
    bool m_characteristicsReady = false;
    QTimer* m_heartbeatTimer = nullptr;
};
