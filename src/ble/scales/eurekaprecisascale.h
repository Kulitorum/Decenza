#pragma once

#include "../scaledevice.h"
#include "../transport/scalebletransport.h"

class EurekaPrecisaScale : public ScaleDevice {
    Q_OBJECT

public:
    explicit EurekaPrecisaScale(ScaleBleTransport* transport, QObject* parent = nullptr);
    ~EurekaPrecisaScale() override;

    void connectToDevice(const QBluetoothDeviceInfo& device) override;
    QString name() const override { return m_name; }
    QString type() const override { return "eureka_precisa"; }

public slots:
    void tare() override;
    void startTimer() override;
    void stopTimer() override;
    void resetTimer() override;
    void sendKeepAlive() override;
    void sleep() override;

    void setUnitToGrams();
    void turnOff();
    void beepTwice();

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
    QString m_name = "Eureka Precisa";
    bool m_serviceFound = false;
    bool m_characteristicsReady = false;
};
