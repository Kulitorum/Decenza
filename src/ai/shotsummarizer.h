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
struct HistoryPhaseMarker;

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
    bool temperatureUnstable = false;

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

    // Profile knowledge base ID (from DB or computed at summarize time)
    QString profileKbId;

    // Pre-computed observation lines from ShotAnalysis::analyzeShot —
    // the same list that drives the in-app Shot Summary dialog. Each entry is
    // a QVariantMap with "text" (QString) and "type" (QString: "good" |
    // "caution" | "warning" | "observation" | "verdict"). Sharing the dialog's
    // output keeps the AI advisor's prompt and the badge UI in lockstep, so
    // the suppression cascade (pour truncated → channeling/temp/grind forced
    // false) is enforced in exactly one place. See docs/SHOT_REVIEW.md §3.
    QVariantList summaryLines;

    // Pour-truncated flag — gates the per-phase temperature markers below
    // (which analyzeShot's aggregate output doesn't surface).
    bool pourTruncatedDetected = false;

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

    // Profile-aware system prompt: base prompt + dial-in reference tables (espresso only)
    // + per-profile knowledge section.
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

    // Get structured analysis flags for a KB entry by its ID.
    // Returns empty list if kbId is not found. Flags are parsed from "AnalysisFlags:" lines
    // in profile_knowledge.md and control which checks analyzeShot() suppresses.
    static QStringList getAnalysisFlags(const QString& kbId);

private:
    // Curve helpers — pure functions, kept static so they can be called from
    // file-scope helpers (e.g. makeWholeShotPhase) without a ShotSummarizer
    // instance.
    static double findValueAtTime(const QVector<QPointF>& data, double time);
    static double calculateAverage(const QVector<QPointF>& data, double startTime, double endTime);
    static double calculateMax(const QVector<QPointF>& data, double startTime, double endTime);
    static double calculateMin(const QVector<QPointF>& data, double startTime, double endTime);
    static QString profileTypeDescription(const QString& editorType);
    // Build a synthetic single-phase PhaseSummary spanning the full shot.
    // Used as a fallback for shots with no phase markers (legacy shots, or
    // shots aborted before frame 0 emitted) so callers don't have to
    // special-case the no-markers shape. Both summarize() and
    // summarizeFromHistory() share this helper.
    static PhaseSummary makeWholeShotPhase(const QVector<QPointF>& pressure,
                                           const QVector<QPointF>& flow,
                                           const QVector<QPointF>& temperature,
                                           const QVector<QPointF>& weight,
                                           double totalDuration);
    // Build per-phase metric summaries from a typed marker list + the four
    // curve series. Skips phases with `endTime <= startTime` (degenerate
    // spans contribute nothing to per-phase metrics) but the caller's
    // parallel HistoryPhaseMarker list still includes them so the marker
    // stream `analyzeShot` consumes is unaffected. Single source of truth
    // shared by `summarize()` (live shot) and `summarizeFromHistory()`
    // (saved shot) — both paths build the typed marker list from their
    // own input source then call this helper.
    static QList<PhaseSummary> buildPhaseSummariesForRange(
        const QVector<QPointF>& pressure,
        const QVector<QPointF>& flow,
        const QVector<QPointF>& temperature,
        const QVector<QPointF>& weight,
        const QList<HistoryPhaseMarker>& markers,
        double totalDuration);
    // Per-phase temperature instability. Sets only PhaseSummary::temperatureUnstable;
    // the aggregate "Temperature drifted X°C from goal" observation is produced by
    // ShotAnalysis::analyzeShot instead. Callers must gate on
    // !pourTruncatedDetected AND ShotAnalysis::reachedExtractionPhase() — same
    // gates the aggregate detector uses. Without the reachedExtractionPhase
    // check, aborted-during-preinfusion shots get false positives on the
    // preheat ramp; see SHOT_REVIEW.md §2.3 and PR #898.
    void markPerPhaseTempInstability(ShotSummary& summary,
        const QVector<QPointF>& tempData, const QVector<QPointF>& tempGoalData) const;

    // Run the detector pipeline and stamp the result onto `summary`: call
    // `ShotAnalysis::analyzeShot`, copy `summaryLines` from the result,
    // derive `pourTruncatedDetected` from `detectors.pourTruncated`, then
    // conditionally call `markPerPhaseTempInstability` under the cascade
    // gate (`!pourTruncatedDetected && reachedExtractionPhase(markers, ...)`).
    //
    // Preconditions on `summary`: `beverageType`, `totalDuration`, and
    // `finalWeight` must already be populated — this helper reads them off
    // `summary` rather than taking them as parameters. `finalWeight` in
    // particular drives the grind-vs-yield arms inside `analyzeShot`; a
    // forgotten assignment leaves it at 0.0 and silently disables those arms.
    //
    // Used by `summarize()` (live) and the slow path of `summarizeFromHistory()`
    // (saved-shot recompute), so those two paths can no longer drift on
    // detector wiring. The fast path of `summarizeFromHistory` bypasses
    // this helper — it consumes pre-computed `summaryLines` +
    // `detectorResults.pourTruncated` from `convertShotRecord` (PR #939, D)
    // and runs the same cascade gate inline; that gate must be kept in
    // sync with this helper's gate.
    void runShotAnalysisAndPopulate(ShotSummary& summary,
        const QVector<QPointF>& pressure,
        const QVector<QPointF>& flow,
        const QVector<QPointF>& weight,
        const QVector<QPointF>& temperature,
        const QVector<QPointF>& temperatureGoal,
        const QVector<QPointF>& conductanceDerivative,
        const QList<HistoryPhaseMarker>& markers,
        const QVector<QPointF>& pressureGoal,
        const QVector<QPointF>& flowGoal,
        const QStringList& analysisFlags,
        double firstFrameSeconds,
        double targetWeightG,
        int frameCount) const;

    // Shared prompt sections
    static QString sharedCorePhilosophy();
    static QString sharedGrinderGuidance();
    static QString sharedBeanKnowledge();
    static QString sharedForbiddenSimplifications();
    static QString sharedResponseGuidelines();

    // Profile knowledge base
    struct ProfileKnowledge {
        QString name;       // Display name (e.g. "D-Flow")
        QString content;    // Full markdown section for this profile
        // Structured flags parsed from "AnalysisFlags: flag1, flag2" lines.
        // Used by analyzeShot() to suppress false positives for profiles
        // where specific behaviors are intentional. Current flags:
        //   flow_trend_ok       — don't flag declining/rising flow as a caution
        //   channeling_expected — minor channeling is normal for this profile
        QStringList analysisFlags;
    };
    static QMap<QString, ProfileKnowledge> s_profileKnowledge;
    static bool s_knowledgeLoaded;
    static void loadProfileKnowledge();
    static QString matchProfileKey(const QMap<QString, ProfileKnowledge>& knowledge,
                                   const QString& profileTitle, const QString& editorTypeHint);

    // Profile catalog (compact one-liner per KB profile for cross-profile awareness)
    static QString s_profileCatalog;
    static void buildProfileCatalog();

    // Dial-in reference tables (shared between in-app AI and MCP)
    static QString s_dialInReference;
    static bool s_dialInReferenceLoaded;
    static void loadDialInReference();
};
