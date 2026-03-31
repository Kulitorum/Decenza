#pragma once

#include "../scaledevice.h"
#include "../transport/scalebletransport.h"

class AtomheartEclairScale : public ScaleDevice {
    Q_OBJECT

public:
    explicit AtomheartEclairScale(ScaleBleTransport* transport, QObject* parent = nullptr);
    ~AtomheartEclairScale() override;

    void connectToDevice(const QBluetoothDeviceInfo& device) override;
    QString name() const override { return m_name; }
    QString type() const override { return "atomheart_eclair"; }

public slots:
    void tare() override;
    void startTimer() override;
    void stopTimer() override;
    void resetTimer() override;
    bool hasIndependentTimerReset() const override { return false; }  // Delegates to tare()
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
    bool validateXor(const QByteArray& data);

    ScaleBleTransport* m_transport = nullptr;
    QString m_name = "Atomheart Eclair";
    bool m_serviceFound = false;
    bool m_characteristicsReady = false;
};
