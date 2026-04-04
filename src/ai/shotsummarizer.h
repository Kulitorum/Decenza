#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <QVector>
#include <QPointF>
#include <QVariant>

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
    QString beverageType;   // "espresso", "filter", etc.
    QString profileRecipeDescription;  // Compact text description of frame sequence

    // Overall metrics
    double totalDuration = 0;
    double doseWeight = 0;
    double finalWeight = 0;
    double targetWeight = 0;   // Profile's target yield (0 = not set)
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

    // Profile knowledge base ID (from DB or computed at summarize time)
    QString profileKbId;

    // Anomaly flags
    bool channelingDetected = false;  // Sudden flow spikes
    bool temperatureUnstable = false; // >2C variation

    // DYE metadata (from user input)
    QString beanBrand;
    QString beanType;
    QString roastDate;
    QString roastLevel;
    QString grinderBrand;
    QString grinderModel;
    QString grinderBurrs;
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

    // Summarize from historical shot data (QVariantMap from database)
    ShotSummary summarizeFromHistory(const QVariantMap& shotData) const;

    // Generate text prompt from summary
    QString buildUserPrompt(const ShotSummary& summary) const;

    // Format recent shot history as AI context (lightweight, no curve data)
    static QString buildHistoryContext(const QVariantList& recentShots);

    // Get the system prompt based on beverage type
    static QString systemPrompt(const QString& beverageType = "espresso");
    static QString espressoSystemPrompt();
    static QString filterSystemPrompt();

    // Profile-aware system prompt: base prompt + per-profile knowledge section.
    // profileKbId: direct knowledge base key (from DB), bypasses fuzzy matching if set.
    // profileType: editor type description string, used as fallback for custom-titled profiles.
    static QString shotAnalysisSystemPrompt(const QString& beverageType, const QString& profileTitle,
                                               const QString& profileType = QString(),
                                               const QString& profileKbId = QString());

    // Compute the knowledge base ID for a profile (for storage in shot history DB).
    // Returns empty string if no match found. Uses title + editorType fallback.
    static QString computeProfileKbId(const QString& profileTitle, const QString& editorType = QString());

    // Get the knowledge base content for a profile by title/type. Returns empty string if no match.
    static QString findProfileSection(const QString& profileTitle, const QString& profileType = QString());

private:
    // Helper methods
    double findValueAtTime(const QVector<QPointF>& data, double time) const;
    double calculateAverage(const QVector<QPointF>& data, double startTime, double endTime) const;
    double calculateMax(const QVector<QPointF>& data, double startTime, double endTime) const;
    double calculateMin(const QVector<QPointF>& data, double startTime, double endTime) const;
    double calculateStdDev(const QVector<QPointF>& data, double startTime, double endTime) const;
    double findTimeToFirstDrip(const QVector<QPointF>& flowData) const;
    bool detectChanneling(const QVector<QPointF>& flowData, double startTime, double endTime) const;

    static QString profileTypeDescription(const QString& editorType);
    void detectChannelingInPhases(ShotSummary& summary, const QVector<QPointF>& flowData) const;
    void calculateTemperatureStability(ShotSummary& summary,
        const QVector<QPointF>& tempData, const QVector<QPointF>& tempGoalData) const;

    // Profile knowledge base
    struct ProfileKnowledge {
        QString name;       // Display name (e.g. "D-Flow")
        QString content;    // Full markdown section for this profile
    };
    static QMap<QString, ProfileKnowledge> s_profileKnowledge;
    static bool s_knowledgeLoaded;
    static void loadProfileKnowledge();
    static QString matchProfileKey(const QMap<QString, ProfileKnowledge>& knowledge,
                                   const QString& profileTitle, const QString& editorTypeHint);

    // Dial-in reference tables (shared between in-app AI and MCP)
    static QString s_dialInReference;
    static bool s_dialInReferenceLoaded;
    static void loadDialInReference();
};
