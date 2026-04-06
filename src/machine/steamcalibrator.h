#pragma once

#include <QObject>
#include <QVector>
#include <QPointF>
#include <QDateTime>
#include <QVariantList>

class SteamDataModel;
class Settings;
class DE1Device;

struct CalibrationStepResult {
    int flowRate = 0;           // 0.01 mL/s units (e.g. 80 = 0.80 mL/s)
    int steamTemp = 0;          // °C machine setting
    double avgPressure = 0.0;   // bar
    double pressureCV = 0.0;    // coefficient of variation (stddev/mean)
    double oscillationRate = 0.0; // zero-crossings per second
    double peakToPeakRange = 0.0; // bar (max - min)
    double pressureSlope = 0.0; // bar/s (linear regression)
    double stabilityScore = 0.0; // 0-100
    double estimatedDryness = 0.0; // 0-1 steam quality (1.0 = fully vaporized)
    double estimatedDilution = 0.0; // % water added to 180g milk heated by 55°C
    int sampleCount = 0;
    double durationSeconds = 0.0;
};

struct CalibrationStepRawData {
    QVector<QPointF> pressure;     // (time_sec, bar)
    QVector<QPointF> flow;         // (time_sec, mL/s)
    QVector<QPointF> temperature;  // (time_sec, °C)
};

struct CalibrationResult {
    QDateTime timestamp;
    int machineModel = 0;
    int heaterVoltage = 0;
    int recommendedFlow = 0;    // 0.01 mL/s units
    int recommendedTemp = 0;    // °C
    double recommendedDilution = 0.0;  // estimated % dilution at recommended settings
    QVector<CalibrationStepResult> steps;
    QVector<CalibrationStepRawData> rawData;  // parallel to steps, for detailed logging
};

class SteamCalibrator : public QObject {
    Q_OBJECT

    Q_PROPERTY(int state READ state NOTIFY stateChanged)
    Q_PROPERTY(int currentStep READ currentStep NOTIFY stepChanged)
    Q_PROPERTY(int totalSteps READ totalSteps NOTIFY stepChanged)
    Q_PROPERTY(int currentFlowRate READ currentFlowRate NOTIFY stepChanged)
    Q_PROPERTY(int currentSteamTemp READ currentSteamTemp NOTIFY stepChanged)
    Q_PROPERTY(int phase READ phase NOTIFY stepChanged)
    Q_PROPERTY(int recommendedFlow READ recommendedFlow NOTIFY calibrationComplete)
    Q_PROPERTY(int recommendedTemp READ recommendedTemp NOTIFY calibrationComplete)
    Q_PROPERTY(double recommendedDilution READ recommendedDilution NOTIFY calibrationComplete)
    Q_PROPERTY(QVariantList results READ results NOTIFY calibrationComplete)
    Q_PROPERTY(bool hasCalibration READ hasCalibration NOTIFY calibrationComplete)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

public:
    enum CalibrationState {
        Idle = 0,
        Instructions,
        WaitingToStart,
        Steaming,
        Analyzing,
        Results
    };
    Q_ENUM(CalibrationState)

    // Phase 1: sweep flow rates at fixed temp. Phase 2: refine best flows across temps.
    enum CalibrationPhase {
        FlowSweep = 0,
        TempSweep
    };
    Q_ENUM(CalibrationPhase)

    explicit SteamCalibrator(Settings* settings, DE1Device* device, QObject* parent = nullptr);

    int state() const { return static_cast<int>(m_state); }
    int currentStep() const { return m_currentStep; }
    int totalSteps() const { return static_cast<int>(m_sweepPlan.size()); }
    int currentFlowRate() const;
    int currentSteamTemp() const;
    int phase() const { return static_cast<int>(m_phase); }
    int recommendedFlow() const;
    int recommendedTemp() const;
    double recommendedDilution() const;
    QVariantList results() const;
    bool hasCalibration() const;
    QString statusMessage() const { return m_statusMessage; }

    // Start a new calibration run
    Q_INVOKABLE void startCalibration();

    // Cancel an in-progress calibration
    Q_INVOKABLE void cancelCalibration();

    // Apply the recommended settings
    Q_INVOKABLE void applyRecommendation();

    // Advance to the next step (called from QML after Instructions state)
    Q_INVOKABLE void advanceToNextStep();

    // Called by MainController when steam phase starts/ends
    void onSteamStarted();
    void onSteamEnded(const SteamDataModel* model);

    // Static analysis function — computes stability metrics from raw pressure data.
    static CalibrationStepResult analyzeStability(
        const QVector<QPointF>& pressureData,
        int flowRate,
        int steamTemp,
        double heaterWatts,
        double trimSeconds = 2.0);

    // Compute just the stability score from an existing CalibrationStepResult
    static double computeStabilityScore(const CalibrationStepResult& step);

    // Estimate steam dryness fraction given heater power and flow rate
    // Returns 0-1 (1.0 = fully vaporized, <1.0 = some liquid water passes through)
    static double estimateDryness(double heaterWatts, double flowMlPerSec, double steamTempC);

    // Estimate milk dilution percentage given steam dryness
    // Assumes 180g milk heated from 8°C to 63°C (55°C rise) in a 224g steel pitcher
    static double estimateDilution(double dryness, double milkMassG = 180.0,
                                    double deltaTempC = 55.0, double pitcherMassG = 224.0);

    // Lookup heater wattage for a machine model (with voltage adjustment)
    static double heaterWattsForModel(int machineModel, int heaterVoltage = 0);

    // Generate sweep plan: flow rates for phase 1, or flow+temp combos for phase 2
    static QVector<int> generateFlowSweep(int machineModel, int heaterVoltage = 0);
    static QVector<int> generateTempSweep();

    // Persistence
    void saveCalibration() const;
    void loadCalibration();

    // Save raw time-series data for the last calibration run to a JSON log file.
    // Returns the file path. Each step includes full pressure/flow/temp arrays.
    QString saveDetailedLog() const;
    static QString logFilePath();

    // For MCP: get the full calibration result
    const CalibrationResult& calibrationResult() const { return m_calibrationResult; }

signals:
    void stateChanged();
    void stepChanged();
    void stepAnalyzed();
    void calibrationComplete();
    void statusMessageChanged();

private:
    struct SweepStep {
        int flowRate;   // 0.01 mL/s units
        int steamTemp;  // °C
    };

    void setState(CalibrationState state);
    void setStatusMessage(const QString& msg);
    void buildPhase2Plan();
    void finishCalibration();

    Settings* m_settings = nullptr;
    DE1Device* m_device = nullptr;

    CalibrationState m_state = Idle;
    CalibrationPhase m_phase = FlowSweep;
    int m_currentStep = 0;
    QVector<SweepStep> m_sweepPlan;
    int m_originalFlow = 0;             // User's flow before calibration started
    int m_originalTemp = 0;             // User's temp before calibration started
    double m_heaterWatts = 0.0;         // Looked up from model at start
    QString m_statusMessage;

    CalibrationResult m_calibrationResult;

    static constexpr double TRIM_SECONDS = 2.0;
    static constexpr int MIN_SAMPLES = 30;
    static constexpr double MIN_DURATION = 10.0;  // seconds after trim
    static constexpr double GOOD_SCORE_THRESHOLD = 75.0;

    // Thermodynamic constants
    static constexpr double SPECIFIC_HEAT_WATER = 4.18;   // J/(g·°C)
    static constexpr double LATENT_HEAT_VAPORIZATION = 2257.0;  // J/g at 100°C
    static constexpr double SPECIFIC_HEAT_STEEL = 0.50;   // J/(g·°C) for stainless steel pitcher
    static constexpr double SPECIFIC_HEAT_MILK = 3.93;    // J/(g·°C)
};
