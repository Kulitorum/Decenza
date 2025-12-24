#pragma once

#include "../scaledevice.h"
#include <QLowEnergyCharacteristic>
#include <QTimer>

class DecentScale : public ScaleDevice {
    Q_OBJECT

public:
    explicit DecentScale(QObject* parent = nullptr);
    ~DecentScale() override;

    void connectToDevice(const QBluetoothDeviceInfo& device) override;
    QString name() const override { return m_name; }
    QString type() const override { return "decent"; }

public slots:
    void tare() override;
    void startTimer() override;
    void stopTimer() override;
    void resetTimer() override;
    void sleep() override;
    void wake() override;
    void setLed(int r, int g, int b);

private slots:
    void onControllerConnected();
    void onControllerDisconnected();
    void onControllerError(QLowEnergyController::Error error);
    void onServiceDiscovered(const QBluetoothUuid& uuid);
    void onServiceStateChanged(QLowEnergyService::ServiceState state);
    void onCharacteristicChanged(const QLowEnergyCharacteristic& c, const QByteArray& value);

private:
    void parseWeightData(const QByteArray& data);
    void sendCommand(const QByteArray& command);
    uint8_t calculateXor(const QByteArray& data);

    QString m_name = "Decent Scale";
    QLowEnergyCharacteristic m_readChar;
    QLowEnergyCharacteristic m_writeChar;
};
