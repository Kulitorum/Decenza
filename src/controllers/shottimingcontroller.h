#pragma once

#include <QObject>
#include <QTimer>
#include <QDateTime>
#include "../profile/profile.h"

class DE1Device;
class ScaleDevice;
class Settings;
class MachineState;
struct ShotSample;

/**
 * ShotTimingController centralizes all shot timing, tare management, and weight processing.
 *
 * This eliminates the previous architecture where timing was spread across:
 * - MachineState (wall-clock timer, tare flags)
 * - MainController (DE1 BLE timer sync)
 * - ShotDataModel (raw time tracking)
 *
 * Single source of truth: DE1's BLE timer (sample.timer)
 *
 * Responsibilities:
 * 1. Shot timing using DE1's BLE timer
 * 2. Tare state machine (Idle -> Pending -> Complete)
 * 3. Weight-to-timestamp synchronization
 * 4. Stop-at-weight detection
 * 5. Per-frame weight exit detection
 */
class ShotTimingController : public QObject {
    Q_OBJECT
    Q_PROPERTY(double shotTime READ shotTime NOTIFY shotTimeChanged)
    Q_PROPERTY(bool tareComplete READ isTareComplete NOTIFY tareCompleteChanged)
    Q_PROPERTY(double currentWeight READ currentWeight NOTIFY weightChanged)

public:
    enum class TareState { Idle, Pending, Complete };
    Q_ENUM(TareState)

    explicit ShotTimingController(DE1Device* device, QObject* parent = nullptr);

    // Properties
    double shotTime() const;
    bool isTareComplete() const { return m_tareState == TareState::Complete; }
    double currentWeight() const { return m_weight; }
    TareState tareState() const { return m_tareState; }

    // Configuration
    void setScale(ScaleDevice* scale);
    void setSettings(Settings* settings);
    void setMachineState(MachineState* machineState);
    void setTargetWeight(double weight);
    void setCurrentProfile(const Profile* profile);

    // Shot lifecycle
    void startShot();   // Called when espresso cycle starts
    void endShot();     // Called when shot ends

    // Data ingestion (called by MainController)
    void onShotSample(const ShotSample& sample, double pressureGoal, double flowGoal,
                      double tempGoal, int frameNumber, bool isFlowMode);
    void onWeightSample(double weight, double flowRate);

    // Tare control
    void tare();

signals:
    void shotTimeChanged();
    void tareCompleteChanged();
    void weightChanged();

    // Unified sample output (all data with consistent timestamp)
    void sampleReady(double time, double pressure, double flow, double temp,
                     double pressureGoal, double flowGoal, double tempGoal,
                     int frameNumber, bool isFlowMode);
    void weightSampleReady(double time, double weight);

    // Stop conditions
    void stopAtWeightReached();
    void perFrameWeightReached(int frameNumber);

private slots:
    void onTareTimeout();
    void updateDisplayTimer();

private:
    void checkStopAtWeight();
    void checkPerFrameWeight(int frameNumber);

    DE1Device* m_device = nullptr;
    ScaleDevice* m_scale = nullptr;
    Settings* m_settings = nullptr;
    MachineState* m_machineState = nullptr;
    const Profile* m_currentProfile = nullptr;

    // Timing state (single source of truth: DE1's BLE timer)
    double m_bleTimeBase = 0;      // First BLE timestamp (shot start)
    double m_currentTime = 0;      // Current shot time (relative to base)
    bool m_shotActive = false;

    // Weight state
    double m_weight = 0;
    double m_flowRate = 0;
    double m_targetWeight = 0;
    bool m_stopAtWeightTriggered = false;
    int m_frameWeightSkipSent = -1;  // Frame for which we've sent weight-based skip
    int m_currentFrameNumber = -1;   // Current frame number from shot samples
    bool m_extractionStarted = false; // True after frame 0 seen (preheating complete)

    // Tare state machine
    TareState m_tareState = TareState::Idle;
    QTimer m_tareTimeout;

    // Display timer (for smooth UI updates between BLE samples)
    QTimer m_displayTimer;
    qint64 m_displayTimeBase = 0;  // Wall clock when shot started
};
