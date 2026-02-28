#pragma once

#include <QObject>
#include <QBluetoothDeviceInfo>
#include <QBluetoothUuid>

#include "protocol/de1characteristics.h"

class Profile;
class Settings;
class DE1Transport;
class BleTransport;

#if (defined(Q_OS_WIN) || defined(Q_OS_MACOS)) && defined(QT_DEBUG)
class DE1Simulator;
#endif

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
    Q_PROPERTY(bool simulationMode READ simulationMode WRITE setSimulationMode NOTIFY simulationModeChanged)
    Q_PROPERTY(bool guiEnabled READ isGuiEnabled NOTIFY guiEnabledChanged)
    Q_PROPERTY(int state READ stateInt NOTIFY stateChanged)
    Q_PROPERTY(int subState READ subStateInt NOTIFY subStateChanged)
    Q_PROPERTY(QString stateString READ stateString NOTIFY stateChanged)
    Q_PROPERTY(QString subStateString READ subStateString NOTIFY subStateChanged)
    Q_PROPERTY(double pressure READ pressure NOTIFY shotSampleReceived)
    Q_PROPERTY(double flow READ flow NOTIFY shotSampleReceived)
    Q_PROPERTY(double temperature READ temperature NOTIFY shotSampleReceived)
    Q_PROPERTY(double steamTemperature READ steamTemperature NOTIFY shotSampleReceived)
    Q_PROPERTY(double waterLevel READ waterLevel NOTIFY waterLevelChanged)
    Q_PROPERTY(double waterLevelMm READ waterLevelMm NOTIFY waterLevelChanged)
    Q_PROPERTY(int waterLevelMl READ waterLevelMl NOTIFY waterLevelChanged)
    Q_PROPERTY(QString firmwareVersion READ firmwareVersion NOTIFY firmwareVersionChanged)
    Q_PROPERTY(bool usbChargerOn READ usbChargerOn NOTIFY usbChargerOnChanged)
    Q_PROPERTY(bool isHeadless READ isHeadless NOTIFY isHeadlessChanged)
    Q_PROPERTY(int refillKitDetected READ refillKitDetected NOTIFY refillKitDetectedChanged)
    Q_PROPERTY(QString connectionType READ connectionType NOTIFY connectedChanged)

public:
    explicit DE1Device(QObject* parent = nullptr);
    ~DE1Device();

    bool isConnected() const;
    bool isConnecting() const;
    bool isGuiEnabled() const;

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
    double steamTemperature() const { return m_steamTemp; }
    double waterLevel() const { return m_waterLevel; }
    double waterLevelMm() const { return m_waterLevelMm; }
    int waterLevelMl() const { return m_waterLevelMl; }
    QString firmwareVersion() const { return m_firmwareVersion; }
    bool usbChargerOn() const { return m_usbChargerOn; }
    bool isHeadless() const { return m_isHeadless; }
    Q_INVOKABLE void setIsHeadless(bool headless);  // Debug toggle
    int refillKitDetected() const { return m_refillKitDetected; }  // -1=unknown, 0=not detected, 1=detected

    // Transport abstraction
    void setTransport(DE1Transport* transport);
    DE1Transport* transport() const { return m_transport; }
    QString connectionType() const;

    // Simulation mode for GUI development without hardware
    bool simulationMode() const { return m_simulationMode; }
    void setSimulationMode(bool enabled);

    // For simulator integration - allows external code to set state and emit signals
    void setSimulatedState(DE1::State state, DE1::SubState subState);
    void emitSimulatedShotSample(const ShotSample& sample);
#if (defined(Q_OS_WIN) || defined(Q_OS_MACOS)) && defined(QT_DEBUG)
    void setSimulator(DE1Simulator* simulator) { m_simulator = simulator; }
#endif

    // Settings for water level calibration persistence
    void setSettings(Settings* settings);

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
    void startDescale();
    void startClean();
    void stopOperation();         // Soft stop (for steam: stops flow, no purge)
    void stopOperationUrgent();   // Bypasses command queue for faster stop (SOW)
    void stopOperationUrgent(qint64 sawTriggerMs);  // Includes SAW trigger timestamp for latency tracing
    void requestIdle();           // Hard stop (requests Idle state, triggers steam purge)
    void skipToNextFrame();   // Skip to next profile frame during extraction (0x0E)
    void goToSleep();
    void wakeUp();

    // Profile upload
    void uploadProfile(const Profile& profile);
    void uploadProfileAndStartEspresso(const Profile& profile);  // Upload then start in correct order
    void clearCommandQueue();  // Clear all pending BLE commands (use when extraction starts)

    // Direct frame writing (for direct control mode)
    void writeHeader(const QByteArray& headerData);
    void writeFrame(const QByteArray& frameData);

    // Settings
    void setShotSettings(double steamTemp, int steamDuration,
                        double hotWaterTemp, int hotWaterVolume,
                        double groupTemp);

    // MMR write (for advanced settings like steam flow)
    void writeMMR(uint32_t address, uint32_t value);

    // USB charger control (force=true to resend even if state unchanged, needed for DE1's 10-min timeout)
    void setUsbChargerOn(bool on, bool force = false);

    // Water refill level (write StartFillLevel to machine via WaterLevels characteristic)
    void setWaterRefillLevel(int refillPointMm);

    // Flow calibration multiplier (MMR 0x80383C: value = int(1000 * multiplier))
    void setFlowCalibrationMultiplier(double multiplier);

    // Refill kit control (MMR 0x80385C: 0=off, 1=on, 2=auto-detect)
    void setRefillKitPresent(int value);
    void requestRefillKitStatus();

signals:
    void connectedChanged();
    void connectingChanged();
    void stateChanged();
    void subStateChanged();
    void shotSampleReceived(const ShotSample& sample);
    void waterLevelChanged();
    void firmwareVersionChanged();
    void profileUploaded(bool success);
    void initialSettingsComplete();
    void errorOccurred(const QString& error);
    void simulationModeChanged();
    void guiEnabledChanged();
    void usbChargerOnChanged();
    void isHeadlessChanged();
    void refillKitDetectedChanged();
    void logMessage(const QString& message);

private:
    // Transport signal handlers
    void onTransportConnected();
    void onTransportDisconnected();
    void onTransportDataReceived(const QBluetoothUuid& uuid, const QByteArray& data);
    void onTransportWriteComplete(const QBluetoothUuid& uuid, const QByteArray& data);

    // Parse methods (dispatch from onTransportDataReceived)
    void parseStateInfo(const QByteArray& data);
    void parseShotSample(const QByteArray& data);
    void parseWaterLevel(const QByteArray& data);
    void parseVersion(const QByteArray& data);
    void parseMMRResponse(const QByteArray& data);
    void requestGHCStatus();

    void sendInitialSettings();

    // Transport pointer — not owned; BleTransport or SerialTransport
    DE1Transport* m_transport = nullptr;

    DE1::State m_state = DE1::State::Sleep;
    DE1::SubState m_subState = DE1::SubState::Ready;
    double m_pressure = 0.0;
    double m_flow = 0.0;
    double m_mixTemp = 0.0;
    double m_headTemp = 0.0;
    double m_steamTemp = 0.0;
    double m_waterLevel = 0.0;
    double m_waterLevelMm = 0.0;  // Raw mm value (with sensor offset applied)
    int m_waterLevelMl = 0;       // Volume in ml (from CAD lookup table)
    double m_lastEmittedWaterLevel = -1.0;  // Throttle: only emit when change >= 0.5%
    int m_lastEmittedWaterLevelMl = -1;    // Throttle: also emit when ml changes (color thresholds)
    QString m_firmwareVersion;

    bool m_connecting = false;
    bool m_simulationMode = false;
#if (defined(Q_OS_WIN) || defined(Q_OS_MACOS)) && defined(QT_DEBUG)
    DE1Simulator* m_simulator = nullptr;  // For simulation mode
#endif
    Settings* m_settings = nullptr;       // For water level calibration persistence
    bool m_profileUploadInProgress = false;  // True while profile header+frames are being sent
    bool m_sleepPendingAfterUpload = false;  // Sleep requested during profile upload
    bool m_usbChargerOn = true;  // Default on (safe default like de1app)
    bool m_isHeadless = false;   // True if app can start operations (GHC not installed or inactive)
    int m_refillKitDetected = -1;  // -1=unknown, 0=not detected, 1=detected

    // SAW stop latency instrumentation (monotonic ms timestamps)
    bool m_sawStopWritePending = false;
    qint64 m_lastSawTriggerMs = 0;
    qint64 m_lastSawWriteMs = 0;
};
