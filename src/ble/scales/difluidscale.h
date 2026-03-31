#pragma once

#include "../scaledevice.h"
#include "../transport/scalebletransport.h"

class DifluidScale : public ScaleDevice {
    Q_OBJECT

public:
    explicit DifluidScale(ScaleBleTransport* transport, QObject* parent = nullptr);
    ~DifluidScale() override;

    void connectToDevice(const QBluetoothDeviceInfo& device) override;
    QString name() const override { return m_name; }
    QString type() const override { return "difluid"; }

public slots:
    void tare() override;
    void startTimer() override;
    void stopTimer() override;
    void resetTimer() override;
    bool hasIndependentTimerReset() const override { return false; }  // Same bytes as startTimer
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
    void enableNotifications();
    void setToGrams();

    ScaleBleTransport* m_transport = nullptr;
    QString m_name = "Difluid";
    bool m_serviceFound = false;
    bool m_characteristicsReady = false;
};
