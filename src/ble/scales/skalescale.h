#pragma once

#include "../scaledevice.h"
#include <QLowEnergyCharacteristic>
#include <QTimer>

class SkaleScale : public ScaleDevice {
    Q_OBJECT

public:
    explicit SkaleScale(QObject* parent = nullptr);
    ~SkaleScale() override;

    void connectToDevice(const QBluetoothDeviceInfo& device) override;
    QString name() const override { return m_name; }
    QString type() const override { return "skale"; }

public slots:
    void tare() override;
    void startTimer() override;
    void stopTimer() override;
    void resetTimer() override;
    void sleep() override { disableLcd(); }
    void wake() override { enableLcd(); }

    // Skale-specific functions
    void enableLcd();
    void disableLcd();
    void enableGrams();

private slots:
    void onControllerConnected();
    void onControllerDisconnected();
    void onControllerError(QLowEnergyController::Error error);
    void onServiceDiscovered(const QBluetoothUuid& uuid);
    void onServiceStateChanged(QLowEnergyService::ServiceState state);
    void onCharacteristicChanged(const QLowEnergyCharacteristic& c, const QByteArray& value);

private:
    void sendCommand(uint8_t cmd);

    QString m_name = "Skale";
    QLowEnergyCharacteristic m_cmdChar;
    QLowEnergyCharacteristic m_weightChar;
    QLowEnergyCharacteristic m_buttonChar;
};
