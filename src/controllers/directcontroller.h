#pragma once

#include <QObject>
#include <QTimer>
#include "../profile/profile.h"
#include "../ble/de1device.h"
#include "../ble/scaledevice.h"

/**
 * DirectController manages Direct Setpoint Control mode.
 *
 * In Direct Control mode, the app sends live setpoints to the machine during
 * extraction instead of relying on pre-uploaded profiles. This enables:
 * - Real-time adjustments based on sensor feedback
 * - Weight-based phase transitions (using scale data)
 * - Complex profiles that exceed the DE1's 20-frame limit
 * - Adaptive profiles that respond to extraction dynamics
 *
 * Usage:
 * 1. Set the profile with setProfile()
 * 2. Call startShot() when the shot begins
 * 3. DirectController monitors shot samples and scale data
 * 4. It sends appropriate frames to the machine based on profile logic
 * 5. Call stopShot() when finished or machine stops
 */
class DirectController : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool active READ isActive NOTIFY activeChanged)
    Q_PROPERTY(int currentFrameIndex READ currentFrameIndex NOTIFY frameChanged)
    Q_PROPERTY(QString currentFrameName READ currentFrameName NOTIFY frameChanged)
    Q_PROPERTY(double frameElapsedTime READ frameElapsedTime NOTIFY progressUpdated)
    Q_PROPERTY(double frameProgress READ frameProgress NOTIFY progressUpdated)

public:
    explicit DirectController(DE1Device* device, ScaleDevice* scale, QObject* parent = nullptr);
    ~DirectController();

    // Set the profile to use in direct control mode
    void setProfile(const Profile& profile);
    const Profile& profile() const { return m_profile; }

    // Control
    Q_INVOKABLE void startShot();
    Q_INVOKABLE void stopShot();
    Q_INVOKABLE void skipToNextFrame();
    Q_INVOKABLE void goToPreviousFrame();

    // Manual overrides (for live adjustment)
    Q_INVOKABLE void setLivePressure(double pressure);
    Q_INVOKABLE void setLiveFlow(double flow);
    Q_INVOKABLE void setLiveTemperature(double temperature);

    // State
    bool isActive() const { return m_active; }
    int currentFrameIndex() const { return m_currentFrameIndex; }
    QString currentFrameName() const;
    double frameElapsedTime() const { return m_frameElapsedTime; }
    double frameProgress() const;

signals:
    void activeChanged();
    void frameChanged(int index, const QString& name, const QString& transitionReason);
    void progressUpdated();
    void shotCompleted();
    void errorOccurred(const QString& error);

private slots:
    void onShotSampleReceived(const ShotSample& sample);
    void onWeightChanged(double weight);
    void onUpdateTimer();

private:
    void sendCurrentFrame();
    void advanceToNextFrame();
    bool checkExitCondition(const ProfileFrame& frame, const ShotSample& sample);
    void createDirectControlProfile();
    QByteArray frameToBytes(const ProfileFrame& frame, int frameIndex);

    DE1Device* m_device = nullptr;
    ScaleDevice* m_scale = nullptr;

    Profile m_profile;
    bool m_active = false;
    int m_currentFrameIndex = -1;

    // Timing
    QTimer m_updateTimer;
    double m_shotStartTime = 0;      // From shot samples
    double m_frameStartTime = 0;     // When current frame started
    double m_frameElapsedTime = 0;   // Time in current frame

    // Last known values for exit condition checking
    double m_lastPressure = 0;
    double m_lastFlow = 0;
    double m_lastWeight = 0;

    // Live overrides
    bool m_liveOverrideActive = false;
    double m_livePressure = 0;
    double m_liveFlow = 0;
    double m_liveTemperature = 0;
};
