#pragma once

#include <QObject>
#include <QBluetoothDeviceInfo>
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QTimer>
#include <QQueue>
#include <functional>

#include "protocol/de1characteristics.h"

class Profile;

struct ShotSample {
    qint64 timestamp = 0;
    double timer = 0.0;
    double groupPressure = 0.0;
    double groupFlow = 0.0;
    double mixTemp = 0.0;
    double headTemp = 0.0;
    double setTempGoal = 0.0;
    double setFlowGoal = 0.0;
    double setPressureGoal = 0.0;
    int frameNumber = 0;
    double steamTemp = 0.0;
};

class DE1Device : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(bool connecting READ isConnecting NOTIFY connectingChanged)
    Q_PROPERTY(int state READ stateInt NOTIFY stateChanged)
    Q_PROPERTY(int subState READ subStateInt NOTIFY subStateChanged)
    Q_PROPERTY(QString stateString READ stateString NOTIFY stateChanged)
    Q_PROPERTY(QString subStateString READ subStateString NOTIFY subStateChanged)
    Q_PROPERTY(double pressure READ pressure NOTIFY shotSampleReceived)
    Q_PROPERTY(double flow READ flow NOTIFY shotSampleReceived)
    Q_PROPERTY(double temperature READ temperature NOTIFY shotSampleReceived)
    Q_PROPERTY(double waterLevel READ waterLevel NOTIFY waterLevelChanged)
    Q_PROPERTY(QString firmwareVersion READ firmwareVersion NOTIFY firmwareVersionChanged)

public:
    explicit DE1Device(QObject* parent = nullptr);
    ~DE1Device();

    bool isConnected() const;
    bool isConnecting() const;

    DE1::State state() const { return m_state; }
    DE1::SubState subState() const { return m_subState; }
    int stateInt() const { return static_cast<int>(m_state); }
    int subStateInt() const { return static_cast<int>(m_subState); }
    QString stateString() const { return DE1::stateToString(m_state); }
    QString subStateString() const { return DE1::subStateToString(m_subState); }

    double pressure() const { return m_pressure; }
    double flow() const { return m_flow; }
    double temperature() const { return m_headTemp; }
    double mixTemperature() const { return m_mixTemp; }
    double waterLevel() const { return m_waterLevel; }
    QString firmwareVersion() const { return m_firmwareVersion; }

public slots:
    void connectToDevice(const QString& address);
    void connectToDevice(const QBluetoothDeviceInfo& device);
    void disconnect();

    // Machine control
    void requestState(DE1::State state);
    void startEspresso();
    void startSteam();
    void startHotWater();
    void startFlush();
    void stopOperation();
    void goToSleep();
    void wakeUp();

    // Profile upload
    void uploadProfile(const Profile& profile);

    // Settings
    void setShotSettings(double steamTemp, int steamDuration,
                        double hotWaterTemp, int hotWaterVolume,
                        double groupTemp);

signals:
    void connectedChanged();
    void connectingChanged();
    void stateChanged();
    void subStateChanged();
    void shotSampleReceived(const ShotSample& sample);
    void waterLevelChanged();
    void firmwareVersionChanged();
    void profileUploaded(bool success);
    void errorOccurred(const QString& error);

private slots:
    void onControllerConnected();
    void onControllerDisconnected();
    void onControllerError(QLowEnergyController::Error error);
    void onServiceDiscovered(const QBluetoothUuid& uuid);
    void onServiceDiscoveryFinished();
    void onServiceStateChanged(QLowEnergyService::ServiceState state);
    void onCharacteristicChanged(const QLowEnergyCharacteristic& c, const QByteArray& value);
    void onCharacteristicWritten(const QLowEnergyCharacteristic& c, const QByteArray& value);
    void processCommandQueue();

private:
    void setupService();
    void subscribeToNotifications();
    void parseStateInfo(const QByteArray& data);
    void parseShotSample(const QByteArray& data);
    void parseWaterLevel(const QByteArray& data);
    void parseVersion(const QByteArray& data);

    void writeCharacteristic(const QBluetoothUuid& uuid, const QByteArray& data);
    void queueCommand(std::function<void()> command);
    void sendInitialSettings();

    QLowEnergyController* m_controller = nullptr;
    QLowEnergyService* m_service = nullptr;
    QMap<QBluetoothUuid, QLowEnergyCharacteristic> m_characteristics;

    DE1::State m_state = DE1::State::Sleep;
    DE1::SubState m_subState = DE1::SubState::Ready;
    double m_pressure = 0.0;
    double m_flow = 0.0;
    double m_mixTemp = 0.0;
    double m_headTemp = 0.0;
    double m_waterLevel = 0.0;
    QString m_firmwareVersion;

    QQueue<std::function<void()>> m_commandQueue;
    QTimer m_commandTimer;
    bool m_writePending = false;
    bool m_connecting = false;
};
