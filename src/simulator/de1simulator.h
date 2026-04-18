#pragma once

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <array>
#include "../ble/protocol/de1characteristics.h"
#include "../ble/de1device.h"
#include "../profile/profile.h"

/**
 * DE1Simulator - Simulates DE1 espresso machine behavior
 *
 * Physics model based on research from:
 * - Coffee ad Astra: Puck resistance studies (R² ∝ Flow² / ΔP)
 * - Darcy's law for flow through porous media
 * - Thermal mass modeling for group head
 * - Perlin noise for natural-looking variations
 *
 * Realistic behaviors:
 * - Puck swelling during saturation (resistance increases)
 * - Oil extraction causing resistance decline
 * - Micro-channeling events
 * - Pump response lag and system inertia
 * - Thermal lag from group head mass
 */
class DE1Simulator : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(DE1::State state READ state NOTIFY stateChanged)
    Q_PROPERTY(DE1::SubState subState READ subState NOTIFY subStateChanged)

public:
    explicit DE1Simulator(QObject* parent = nullptr);

    bool isRunning() const { return m_running; }
    DE1::State state() const { return m_state; }
    DE1::SubState subState() const { return m_subState; }

    void setProfile(const Profile& profile);
    void setDose(double grams);
    void setGrindSetting(const QString& setting);

public slots:
    // Machine control (called from GHC buttons or app)
    void startEspresso();
    void startSteam();
    void startHotWater();
    void startFlush();
    void startDescale();
    void startClean();
    void stop();
    void goToSleep();
    void wakeUp();
    // Skip to next profile frame (mirrors DE1::State::SkipToNext from the real machine).
    // No-op outside espresso (Preinfusion/Pouring substates).
    void skipFrame();

    // Target steam temperature from the app's ShotSettings write. A target of 0
    // means "heater off" (Off preset / steamDisabled). The sim doesn't model a
    // thermal ramp — it snaps m_steamTemp to target (or to ambient ~25C when
    // target is 0) and emits idleSteamTempChanged so DE1Device's steam temp
    // property reflects the commanded state instead of a stale hardcoded value.
    void setTargetSteamTemp(double targetC);

signals:
    void runningChanged();
    void stateChanged();
    void subStateChanged();
    void shotSampleReceived(const ShotSample& sample);
    void scaleWeightChanged(double weight);  // Simulated scale output
    void idleSteamTempChanged(double steamTempC);  // Fired when target changes (idle thermal update)

private slots:
    void onSimulationTimerTick();

private:
    void setState(DE1::State state, DE1::SubState subState);
    void startOperation(DE1::State state);
    void stopOperation();

    // Profile execution
    void executeFrame();
    void executeEnding(double dt);
    bool checkExitCondition(const ProfileFrame& frame);
    void advanceToNextFrame();

    // Physics simulation
    double simulatePuckResistance(double timeInShot, double totalWaterThrough);
    double calculateFlow(double pressure, double resistance);
    double calculatePressure(double flow, double resistance);

    // Noise generation for realistic variations
    double perlinNoise1D(double x);              // Smooth coherent noise
    double fractalNoise(double x, int octaves);  // Multi-frequency noise
    double channelNoise(double time);            // Micro-channeling events
    void initNoisePermutation();                 // Initialize noise tables

    // State
    bool m_running = false;
    DE1::State m_state = DE1::State::Sleep;
    DE1::SubState m_subState = DE1::SubState::Ready;

    // Profile
    Profile m_profile;
    int m_currentFrameIndex = 0;
    double m_frameStartTime = 0.0;
    double m_frameVolume = 0.0;

    // Dose and grind affect puck resistance
    double m_dose = 18.0;        // grams of coffee (default 18g)
    double m_grindFactor = 1.0;  // finer grind = higher factor = more resistance

    // Timing
    QTimer m_tickTimer;
    QElapsedTimer m_shotTimer;
    QElapsedTimer m_operationTimer;
    static constexpr int TICK_INTERVAL_MS = 100;  // 10Hz simulation, send samples at 5Hz

    // Simulated machine state - actual values
    double m_pressure = 0.0;
    double m_flow = 0.0;
    double m_groupTemp = 93.0;   // Start preheated (machine must be hot to start shot)
    double m_mixTemp = 91.5;     // Slightly lower than group temp
    double m_steamTemp = 140.0;

    // System dynamics - for smooth ramping
    double m_pressureVelocity = 0.0;  // Rate of pressure change (for inertia)
    double m_flowVelocity = 0.0;      // Rate of flow change
    double m_targetPressure = 0.0;    // What we're ramping toward
    double m_targetFlow = 0.0;

    // Volume tracking
    double m_totalVolume = 0.0;     // Total water into puck
    double m_outputVolume = 0.0;    // Water that has exited puck (yield)
    double m_scaleWeight = 0.0;     // Simulated scale reading

    // Valve and plumbing state
    bool m_valveOpen = false;           // Group head valve (closed during preheat)
    double m_plumbingVolume = 0.0;      // Water accumulated in hoses during preheat
    double m_plumbingPressure = 0.0;    // Pressure built up in plumbing

    // Puck physics state
    double m_puckResistance = 4.0;
    double m_baseResistance = 4.0;  // Resistance before noise
    bool m_waterInPuck = false;
    bool m_puckFilled = false;

    // Channeling simulation
    double m_channelIntensity = 0.0;  // Current channeling level (0-1)
    double m_lastChannelTime = 0.0;   // When last channel event started

    // Ending phase tracking
    double m_endingStartTime = 0.0;   // When ending phase began

    // Descale/clean simulation
    int m_descaleStep = 0;            // Current descale substate (8-12)
    double m_descaleStepStart = 0.0;  // When current step started

    // Noise permutation table (for Perlin noise)
    std::array<int, 512> m_perm;
    uint32_t m_noiseSeed = 0;

    // ============ Physics Constants ============
    // Based on research from Coffee ad Astra, Barista Hustle, and espresso physics papers

    // Timing
    static constexpr double PREHEAT_DURATION = 5.0;       // Seconds valve stays closed (builds pressure)

    // Plumbing model (valve closed during preheat)
    // Water compresses in hoses, building pressure before valve opens
    static constexpr double PLUMBING_COMPLIANCE = 0.15;   // ml/bar - how much volume per bar of pressure
    static constexpr double PREHEAT_PUMP_FLOW = 2.5;      // ml/s pump rate during preheat

    // Puck resistance model (Darcy's law based)
    // Lower resistance for easier flow simulation
    // R = k * P / Q - lower values = more flow at same pressure
    static constexpr double REFERENCE_DOSE = 18.0;        // Grams - baseline for resistance calc
    static constexpr double REFERENCE_GRIND = 25.0;       // Grind setting where factor = 1.0
    static constexpr double BASELINE_RESISTANCE = 2.5;    // Fresh dry puck at reference dose/grind
    static constexpr double PEAK_RESISTANCE = 3.5;        // After coffee swells (~40% increase)
    static constexpr double MIN_RESISTANCE = 0.0;         // No minimum - test mode
    static constexpr double PUCK_FILL_VOLUME = 5.0;       // ml to saturate puck before dripping (18g dose fills ~5ml void space)

    // Resistance dynamics
    static constexpr double SWELLING_TIME = 5.0;          // Seconds for puck to fully swell
    static constexpr double DEGRADATION_RATE = 0.004;     // Resistance drop per ml water

    // Thermal model (75g steel group head)
    static constexpr double TEMP_RISE_RATE = 6.0;         // °C/s during preheat (with water flowing)
    static constexpr double TEMP_FALL_RATE = 0.3;         // °C/s cooling (thermal mass limits this)
    static constexpr double TEMP_APPROACH_RATE = 0.08;    // Exponential approach factor

    // System dynamics (pump, hoses, puck compression)
    static constexpr double PRESSURE_INERTIA = 0.4;       // Seconds to reach 63% of target
    static constexpr double FLOW_INERTIA = 0.3;           // Flow responds slightly faster
    static constexpr double MAX_PRESSURE = 12.0;          // DE1 pump maximum
    static constexpr double MAX_FLOW = 8.0;               // Vibration pump limit (ml/s)

    // Darcy's law constant: Flow = k * Pressure / Resistance
    static constexpr double DARCY_K = 1.8;

    // Yield curve (output vs input)
    // Once the puck is saturated, most water flows through as coffee — real espresso
    // extraction is ~85-90% efficient. 36ml SAV should produce ~30g in cup, not ~15g.
    static constexpr double DRIP_START_EFFICIENCY = 0.82; // Initial output/input ratio (puck already saturated)
    static constexpr double DRIP_MAX_EFFICIENCY = 0.92;   // Maximum efficiency
    static constexpr double EFFICIENCY_RAMP_ML = 8.0;     // ml output to reach max efficiency (fast — puck is already wet)

    // Noise characteristics - subtle for well-prepared puck
    static constexpr double NOISE_PRESSURE_AMP = 0.08;    // ±0.08 bar random variation
    static constexpr double NOISE_FLOW_AMP = 0.04;        // ±0.04 ml/s random variation
    static constexpr double NOISE_RESISTANCE_AMP = 0.03;  // ±3% resistance variation

    // Channeling disabled - simulates a well-prepared puck
    static constexpr double CHANNEL_PROBABILITY = 0.0;    // No channeling events
    static constexpr double CHANNEL_DURATION = 1.5;       // (unused when probability is 0)
    static constexpr double CHANNEL_RESISTANCE_DROP = 0.15; // (unused when probability is 0)

    // Scale simulation
    static constexpr double COFFEE_DENSITY = 1.03;        // g/ml (dissolved solids increase density)
    static constexpr double SCALE_NOISE_AMP = 0.05;       // ±0.05g scale jitter

    // Ending phase - pressure decay through puck
    static constexpr double HEADSPACE_VOLUME = 12.0;      // ml of water above puck under pressure
    static constexpr double MIN_ENDING_PRESSURE = 0.15;   // Pressure threshold to end simulation
    static constexpr double MAX_ENDING_TIME = 10.0;       // Safety timeout for ending phase (seconds)

    int m_tickCount = 0;  // For 5Hz sample output
};
