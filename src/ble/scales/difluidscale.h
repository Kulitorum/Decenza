#pragma once

#include "../scaledevice.h"
#include "../transport/scalebletransport.h"

#include <QBluetoothUuid>
#include <QStringList>

class DifluidScale : public ScaleDevice {
    Q_OBJECT

public:
    explicit DifluidScale(ScaleBleTransport* transport, QObject* parent = nullptr);
    ~DifluidScale() override;

    void connectToDevice(const QBluetoothDeviceInfo& device) override;
    QString name() const override { return m_name; }
    QString type() const override { return ScaleTypeIds::scaleTypeId(ScaleType::Difluid); }

public slots:
    void tare() override;
    bool supportsTimer() const override { return true; }
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
    // Clears the per-link state so a dropped connection cannot leave sendCommand's
    // guards passing against a torn-down transport.
    void resetLinkState();
    void enableNotifications();
    void setToGrams();

    ScaleBleTransport* m_transport = nullptr;
    QString m_name = "Difluid";
    // Which DiFluid service this device actually advertised — 0x00EE on the
    // original Microbalance, 0x00DD on the Ti. Null until discovery finds one,
    // so it doubles as the "service found" flag. Everything downstream
    // (characteristic discovery, notifications, writes) uses this rather than a
    // hard-coded constant, because that is where the two models differ.
    QBluetoothUuid m_service;
    // Everything else the device advertised, so a "not a DiFluid" failure can say
    // what it did find instead of only what it didn't.
    QStringList m_discoveredServices;
    bool m_characteristicsReady = false;
};
