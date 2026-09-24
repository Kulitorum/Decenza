#pragma once

#include "../scaledevice.h"
#include "../transport/scalebletransport.h"
#include <QTimer>

class SkaleScale : public ScaleDevice {
    Q_OBJECT

public:
    explicit SkaleScale(ScaleBleTransport* transport, QObject* parent = nullptr);
    ~SkaleScale() override;

    void connectToDevice(const QBluetoothDeviceInfo& device) override;
    QString name() const override { return m_name; }
    QString type() const override { return ScaleTypeIds::scaleTypeId(ScaleType::Skale); }

public slots:
    void tare() override;
    bool supportsTimer() const override { return true; }
    void startTimer() override;
    void stopTimer() override;
    void resetTimer() override;
    // No keep-alive override, deliberately: #1897's 30 s CCCD rewrite did not
    // stop the #1896 drops, and one unanswered rewrite wedges Android (#1965).
    void sleep() override;
    void wake() override { enableLcd(); }

    // Skale-specific functions
    void enableLcd();
    void disableLcd() override;
    void enableGrams();

private slots:
    void onTransportConnected();
    void onTransportDisconnected();
    void onTransportError(const QString& message);
    void onServiceDiscovered(const QBluetoothUuid& uuid);
    void onServicesDiscoveryFinished();
    void onCharacteristicsDiscoveryFinished(const QBluetoothUuid& serviceUuid);
    void onCharacteristicChanged(const QBluetoothUuid& characteristicUuid, const QByteArray& value);

private:
    void sendCommand(uint8_t cmd);
    void sendCommand(uint8_t cmd, ScaleBleTransport::WriteType writeType);

    ScaleBleTransport* m_transport = nullptr;
    QString m_name = "Skale";
    bool m_serviceFound = false;
    bool m_characteristicsReady = false;
};
