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
    double pressureCV = 0.0;    // coefficient of variation (stddev/mean) — primary metric
    double oscillationRate = 0.0; // zero-crossings per second
    double peakToPeakRange = 0.0; // bar (max - min)
    double pressureSlope = 0.0; // bar/s (linear regression)
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
    int steamTemp = 0;          // °C (user's temp at time of calibration)
    double recommendedDilution = 0.0;  // estimated % dilution at recommended settings
    double bestCV = 0.0;        // CV of the recommended flow rate
    QVector<CalibrationStepResult> steps;
    QVector<CalibrationStepRawData> rawData;  // parallel to steps, for detailed logging
};

class SteamCalibrator : public QObject {
    Q_OBJECT

    Q_PROPERTY(int state READ state NOTIFY stateChanged)
    Q_PROPERTY(int currentStep READ currentStep NOTIFY stepChanged)
    Q_PROPERTY(int totalSteps READ totalSteps NOTIFY stepChanged)
    Q_PROPERTY(int currentFlowRate READ currentFlowRate NOTIFY stepChanged)
    Q_PROPERTY(double steamingElapsed READ steamingElapsed NOTIFY steamingElapsedChanged)
    Q_PROPERTY(bool hasEnoughData READ hasEnoughData NOTIFY hasEnoughDataChanged)
    Q_PROPERTY(bool heaterReady READ heaterReady NOTIFY heaterReadyChanged)
    Q_PROPERTY(double currentHeaterTemp READ currentHeaterTemp NOTIFY currentHeaterTempChanged)
    Q_PROPERTY(int recommendedFlow READ recommendedFlow NOTIFY calibrationComplete)
    Q_PROPERTY(double recommendedDilution READ recommendedDilution NOTIFY calibrationComplete)
    Q_PROPERTY(double bestCV READ bestCV NOTIFY calibrationComplete)
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

    explicit SteamCalibrator(Settings* settings, DE1Device* device, QObject* parent = nullptr);

    int state() const { return static_cast<int>(m_state); }
    int currentStep() const { return m_currentStep; }
    int totalSteps() const { return static_cast<int>(m_flowSteps.size()); }
    int currentFlowRate() const;
    double steamingElapsed() const { return m_steamingElapsed; }
    bool hasEnoughData() const { return m_hasEnoughData; }
    bool heaterReady() const { return m_heaterReady; }
    double currentHeaterTemp() const { return m_currentHeaterTemp; }
    int recommendedFlow() const;
    double recommendedDilution() const;
    double bestCV() const;
    QVariantList results() const;
    bool hasCalibration() const;
    QString statusMessage() const { return m_statusMessage; }

    Q_INVOKABLE void startCalibration();
    Q_INVOKABLE void cancelCalibration();
    Q_INVOKABLE void applyRecommendation();
    Q_INVOKABLE void advanceToNextStep();

    // Called by MainController when steam phase starts/ends
    void onSteamStarted();
    void onSteamEnded(const SteamDataModel* model);

    // Called by MainController on each steam sample (~5Hz)
    void onSteamSample(double elapsed);

    // Called by MainController on each shot sample to track heater temp recovery
    void updateHeaterTemp(double steamTempC);

    // Static analysis — computes stability metrics from raw pressure data
    static CalibrationStepResult analyzeStability(
        const QVector<QPointF>& pressureData,
        int flowRate,
        int steamTemp,
        double heaterWatts,
        double trimSeconds = 2.0);

    // Estimate steam dryness fraction given heater power and flow rate
    static double estimateDryness(double heaterWatts, double flowMlPerSec, double steamTempC);

    // Estimate milk dilution percentage given steam dryness
    static double estimateDilution(double dryness, double milkMassG = 180.0,
                                    double deltaTempC = 55.0, double pitcherMassG = 224.0);

    // Lookup heater wattage for a machine model
    static double heaterWattsForModel(int machineModel, int heaterVoltage = 0);

    // Generate flow rate sweep for a given machine model
    static QVector<int> generateFlowSweep(int machineModel, int heaterVoltage = 0);

    // Find recommendation: highest flow within 20% of best CV
    static int findRecommendedFlow(const QVector<CalibrationStepResult>& steps);

    // Persistence
    void saveCalibration() const;
    void loadCalibration();

    QString saveDetailedLog() const;
    static QString logFilePath();

    const CalibrationResult& calibrationResult() const { return m_calibrationResult; }

signals:
    void stateChanged();
    void stepChanged();
    void stepAnalyzed();
    void calibrationComplete();
    void statusMessageChanged();
    void steamingElapsedChanged();
    void hasEnoughDataChanged();
    void heaterReadyChanged();
    void currentHeaterTempChanged();
    void settingsApplied();

private:
    void setState(CalibrationState state);
    void setStatusMessage(const QString& msg);
    void finishCalibration();

    Settings* m_settings = nullptr;
    DE1Device* m_device = nullptr;

    CalibrationState m_state = Idle;
    int m_currentStep = 0;
    QVector<int> m_flowSteps;           // Flow rates to test (0.01 mL/s units)
    int m_originalFlow = 0;
    int m_originalTemp = 0;
    bool m_originalKeepHeaterOn = false;
    double m_heaterWatts = 0.0;
    double m_steamingElapsed = 0.0;
    bool m_hasEnoughData = false;
    bool m_autoStopRequested = false;
    bool m_heaterReady = true;
    double m_currentHeaterTemp = 0.0;
    QString m_statusMessage;

    CalibrationResult m_calibrationResult;

    static constexpr double TRIM_SECONDS = 2.0;
    static constexpr int MIN_SAMPLES = 30;
    static constexpr double MIN_DURATION = 10.0;
    static constexpr double TARGET_DURATION = 20.0;
    static constexpr double CV_MARGIN = 0.20;  // Pick highest flow within 20% of best CV

    // Thermodynamic constants
    static constexpr double SPECIFIC_HEAT_WATER = 4.18;
    static constexpr double LATENT_HEAT_VAPORIZATION = 2257.0;
    static constexpr double SPECIFIC_HEAT_STEEL = 0.50;
    static constexpr double SPECIFIC_HEAT_MILK = 3.93;
};
