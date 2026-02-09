#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QVector>
#include <QPointF>

class ShotDataModel;
class Profile;
struct ShotMetadata;

// Summary of a single phase (e.g., Preinfusion, Extraction)
struct PhaseSummary {
    QString name;
    double startTime = 0;
    double endTime = 0;
    double duration = 0;
    bool isFlowMode = false;  // True if flow-controlled, false if pressure-controlled

    // Pressure metrics (bar)
    double avgPressure = 0;
    double maxPressure = 0;
    double minPressure = 0;
    double pressureAtStart = 0;
    double pressureAtMiddle = 0;
    double pressureAtEnd = 0;

    // Flow metrics (mL/s)
    double avgFlow = 0;
    double maxFlow = 0;
    double minFlow = 0;
    double flowAtStart = 0;
    double flowAtMiddle = 0;
    double flowAtEnd = 0;

    // Temperature metrics (C)
    double avgTemperature = 0;
    double tempStability = 0;  // Std deviation

    // Weight gained during this phase
    double weightGained = 0;
};

// Complete shot summary for AI analysis
struct ShotSummary {
    // Profile info
    QString profileTitle;
    QString profileType;
    QString profileNotes;   // Author's description of profile intent/design
    QString profileAuthor;

    // Overall metrics
    double totalDuration = 0;
    double doseWeight = 0;
    double finalWeight = 0;
    double ratio = 0;  // finalWeight / doseWeight

    // Phase breakdown
    QList<PhaseSummary> phases;

    // Raw curve data for detailed analysis
    QVector<QPointF> pressureCurve;
    QVector<QPointF> flowCurve;
    QVector<QPointF> tempCurve;
    QVector<QPointF> weightCurve;

    // Target/goal curves (what the profile intended)
    QVector<QPointF> pressureGoalCurve;
    QVector<QPointF> flowGoalCurve;
    QVector<QPointF> tempGoalCurve;

    // Extraction indicators
    double timeToFirstDrip = 0;  // When flow > 0.5 mL/s
    double preinfusionDuration = 0;
    double mainExtractionDuration = 0;

    // Anomaly flags
    bool channelingDetected = false;  // Sudden flow spikes
    bool temperatureUnstable = false; // >2C variation

    // DYE metadata (from user input)
    QString beanBrand;
    QString beanType;
    QString roastDate;
    QString roastLevel;
    QString grinderModel;
    QString grinderSetting;
    double drinkTds = 0;
    double drinkEy = 0;
    int enjoymentScore = 0;
    QString tastingNotes;
};

class ShotSummarizer : public QObject {
    Q_OBJECT

public:
    explicit ShotSummarizer(QObject* parent = nullptr);

    // Main summarization method
    ShotSummary summarize(const ShotDataModel* shotData,
                          const Profile* profile,
                          const ShotMetadata& metadata,
                          double doseWeight,
                          double finalWeight) const;

    // Generate text prompt from summary
    QString buildUserPrompt(const ShotSummary& summary) const;

    // Get the system prompt (James Hoffmann methodology)
    static QString systemPrompt();

private:
    // Helper methods
    double findValueAtTime(const QVector<QPointF>& data, double time) const;
    double calculateAverage(const QVector<QPointF>& data, double startTime, double endTime) const;
    double calculateMax(const QVector<QPointF>& data, double startTime, double endTime) const;
    double calculateMin(const QVector<QPointF>& data, double startTime, double endTime) const;
    double calculateStdDev(const QVector<QPointF>& data, double startTime, double endTime) const;
    double findTimeToFirstDrip(const QVector<QPointF>& flowData) const;
    bool detectChanneling(const QVector<QPointF>& flowData, double afterTime) const;
};
