#pragma once

#include "../scaledevice.h"
#include "../transport/scalebletransport.h"

class TimemoreScale : public ScaleDevice {
    Q_OBJECT

public:
    explicit TimemoreScale(ScaleBleTransport* transport, QObject* parent = nullptr);
    ~TimemoreScale() override;

    void connectToDevice(const QBluetoothDeviceInfo& device) override;
    QString name() const override { return m_name; }
    QString type() const override { return "timemore"; }

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
    void sendCommand(const QByteArray& cmd);

    ScaleBleTransport* m_transport = nullptr;
    QString m_name = "Timemore";
    bool m_serviceFound = false;
    bool m_characteristicsReady = false;
};
