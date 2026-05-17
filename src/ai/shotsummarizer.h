#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <QVector>
#include <QPointF>
#include <QVariant>
#include <limits>

#include "../history/shotprojection.h"
#include "shotanalysis.h"  // ShotAnalysis::ExpertBand — expertBandForKbId return type (D14)

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
    // the suppression cascade (pour truncated → channeling/grind forced
    // false) is enforced in exactly one place. See docs/SHOT_REVIEW.md §3.
    QVariantList summaryLines;

    // Pour-truncated flag carried alongside summaryLines for callers that
    // need the cascade dominator without re-running detectors.
    bool pourTruncatedDetected = false;

    // Profile recipe rendered from profileJson (frame-by-frame intent).
    // Pre-computed at summarize() time so the JSON user prompt can ship it
    // under currentProfile.recipe without re-parsing the JSON on every read.
    QString profileRecipe;

    // Profile's target brewing temperature in Celsius (0 = not set).
    double targetTemperatureC = 0;

    // Profile's recommended dose in grams (0 = not set; matches the
    // currentProfile.recommendedDoseG field shipped by dialing_get_context).
    double recommendedDoseG = 0;

    // Bean / grinder / tasting metadata. Source depends on the path:
    // `summarize()` (live) reads from the just-collected `ShotMetadata`;
    // `summarizeFromHistory()` reads from the shot's saved database
    // record. The fields named here mirror the columns under those two
    // sources — this struct is not itself a live-DYE snapshot.
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

    // Summarize from historical shot data (typed projection from database)
    ShotSummary summarizeFromHistory(const ShotProjection& shotData) const;

    // Per openspec optimize-dialing-context-payload (task 10): the
    // history-block path that AIManager::requestRecentShotContext walks
    // (one buildUserPrompt call per historical shot) wraps each block in
    // its own `### Shot (date)` header — duplicating the per-shot
    // `## Shot Summary` and `## Detector Observations` markdown
    // headers the standalone path emits. `HistoryBlock` mode skips ONLY
    // those two top-level header *lines*; the content under them still
    // emits (dose, yield, duration, grind setting, peaks, phase data,
    // detector observation lines with their `[warning] / [good]` tags).
    // Profile / intent / recipe / Coffee / brand+model+burrs Grinder are
    // already absent from both `RenderMode` values of `buildUserPrompt`
    // via tasks 8 and 9 — those strips do NOT extend to the separate
    // `buildHistoryContext` static helper, which still emits per-shot
    // grinder + bean lines (its callers don't hoist a setup header).
    enum class RenderMode { Standalone, HistoryBlock };

    // Generate user prompt from summary.
    //
    // - `Standalone` mode (default): returns a JSON-encoded envelope
    //   carrying currentBean / profile / tastingFeedback / shotAnalysis.
    //   Key names mirror dialing_get_context's response shape so a single
    //   system prompt reads correctly off either surface. The `shotAnalysis`
    //   field embeds prose rendered by `renderShotAnalysisProse(..., Standalone)`
    //   — that prose carries the `## Shot Summary` and `## Detector
    //   Observations` headers, which `HistoryBlock` mode strips. Regex
    //   consumers in AIConversation match on that prose after parsing the
    //   JSON envelope first via `extractShotProse`.
    // - `HistoryBlock` mode: returns prose only, no JSON envelope, with
    //   the two top-level headers stripped. The caller wraps each block
    //   in a `### Shot (date)` header so JSON-per-shot would be unreadable
    //   when concatenated.
    //
    // Output is byte-stable for identical input — no wall-clock or per-call
    // values appear in the payload, so prompt caching keeps hitting.
    QString buildUserPrompt(const ShotSummary& summary,
                            RenderMode mode = RenderMode::Standalone) const;

    // Same shape as `buildUserPrompt` but returns the unwrapped envelope so
    // callers with DB / Settings / ProfileManager scope (the in-app advisor's
    // bg-thread closure, ai_advisor_invoke) can append the four DB-scoped
    // blocks (`dialInSessions`, `bestRecentShot`, `sawPrediction`,
    // `grinderContext`) before serializing. `HistoryBlock` mode returns an
    // empty `QJsonObject` — the prose body lives in `shotAnalysis` only via
    // `buildUserPrompt`'s string path; HistoryBlock has no envelope.
    QJsonObject buildUserPromptObject(const ShotSummary& summary,
                                      RenderMode mode = RenderMode::Standalone) const;

    // Return ONLY the prose body (`## Shot Summary` + `## Phase Data` +
    // `## Detector Observations`) — no JSON envelope. Used by
    // `dialing_get_context` to populate `result.shotAnalysis` without
    // double-shipping `currentBean` / `profile` / `tastingFeedback`
    // (which already live at the top level of the response).
    //
    // Implemented as `renderShotAnalysisProse(summary, RenderMode::Standalone)`.
    // `buildUserPromptObject(summary, Standalone)` calls the same renderer
    // with the same mode for its `shotAnalysis` key, so the two surfaces
    // emit identical prose when both use `Standalone` (the only mode for
    // which `buildUserPromptObject` returns a non-empty envelope today).
    // Callers that pass `HistoryBlock` to `buildUserPromptObject` would
    // get different prose (HistoryBlock suppresses the `## Shot Summary`
    // and `## Detector Observations` headers) — that path doesn't share
    // an envelope at all.
    QString buildShotAnalysisProse(const ShotSummary& summary) const;

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

    // Direct KB lookup by ID — bypasses fuzzy title matching. Returns empty string
    // if the ID isn't in the knowledge base. Used by MCP to ship just the current
    // profile's KB entry without the surrounding system prompt + reference tables
    // + cross-profile catalog (#987).
    static QString profileKnowledgeForKbId(const QString& profileKbId);

    // Get structured analysis flags for a KB entry by its ID.
    // Returns empty list if kbId is not found. Flags are parsed from "AnalysisFlags:" lines
    // in profile_knowledge.md and control which checks analyzeShot() suppresses.
    static QStringList getAnalysisFlags(const QString& kbId);

    // Cross-cutting reference content (entries with skipCatalog:true in
    // profile_knowledge.json) — injected into the espresso system prompt
    // and per-profile MCP payloads (filter/pour-over beverage types
    // excluded), since they apply across profiles rather than to any one.
    // Assembles each skipCatalog entry's `prose` from the structured KB.
    static QString crossProfileReferenceContent();

    // UGS lookup and enumeration — used by DialingBlocks::buildGrinderCalibrationBlock.
    //
    // `kbId` is any value resolveKbInput accepts (a current `id` or a legacy
    // normalized title). `ugsForKbId` returns the resolved entry's UGS;
    // NaN when unresolved or the entry has no `ugs`. `ugsInferredForKbId`
    // returns the entry's `ugs.inferred` JSON flag (estimated, not from a
    // two-anchor calibration). `canonicalNameForKbId` returns the resolved
    // entry's `displayName`; empty when unresolved.
    //
    // `KbUgsEntry` / `allKbUgsEntries` enumerate every KB entry with a
    // non-NaN UGS for the cross-profile RGS array. s_profileKnowledge is
    // keyed by `id` (one entry per identity) so iteration is already
    // de-duplicated — no by-name pass. skipCatalog entries have no UGS.
    static double ugsForKbId(const QString& kbId);
    static bool ugsInferredForKbId(const QString& kbId);
    static QString canonicalNameForKbId(const QString& kbId);

    struct KbUgsEntry {
        QString kbId;               // the canonical `id`
        QString name;               // displayName
        double ugs       = std::numeric_limits<double>::quiet_NaN();
        bool ugsInferred = false;
    };
    static QList<KbUgsEntry> allKbUgsEntries();

    // Resolve the cited expert-recommended operating band for a profile.
    // `kbId` is any value resolveKbInput accepts. Returns
    // `std::optional<ShotAnalysis::ExpertBand>` (the type is owned by the
    // lower ShotAnalysis layer so analyzeShot can consume it without
    // depending on ShotSummarizer).
    //
    // The band is the `expertBand` object of the resolved
    // profile_knowledge.json entry (validated by tools/validate_kb.py),
    // re-read fresh from the qrc resource every call so a correction
    // retroactively applies (recompute-on-load contract). The former
    // hardcoded C++ kBands table is removed — the JSON is the single
    // source of truth; expertBandFromJson degrades a malformed shape to
    // std::nullopt (never a wrong band). Alias twins that share an entry
    // (D-Flow / Q ≡ Damian's Q → one `id`) collapse via the alias→id map;
    // distinctly-keyed profiles (D-Flow / La Pavoni; D-Flow / default,
    // which has no expertBand) stay distinct — zero special-case logic.
    //
    // Self-classifying: a profile has a band iff a cited source states a
    // pressure-peak OR extraction-flow band for it. No cited band →
    // absent (check no-ops); absence is intentional, never fabricated.
    // Unresolved key or no band → std::nullopt.
    static std::optional<ShotAnalysis::ExpertBand>
    expertBandForKbId(const QString& kbId);

private:
    // Render the prose body (## Shot Summary, ## Phase Data, ## Tasting
    // Feedback, ## Detector Observations) the legacy buildUserPrompt
    // emitted. Standalone mode wraps this in a JSON envelope under the
    // `shotAnalysis` key; HistoryBlock mode returns it directly so the
    // multi-shot history caller can concatenate per-shot blocks under
    // `### Shot (date)` wrappers.
    QString renderShotAnalysisProse(const ShotSummary& summary, RenderMode mode) const;

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
    // Run the detector pipeline and stamp the result onto `summary`: call
    // `ShotAnalysis::analyzeShot`, copy `summaryLines` from the result, and
    // derive `pourTruncatedDetected` from `detectors.pourTruncated`.
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
    // `detectorResults.pourTruncated` from `convertShotRecord` (PR #939, D).
    void runShotAnalysisAndPopulate(ShotSummary& summary,
        const QVector<QPointF>& pressure,
        const QVector<QPointF>& flow,
        const QVector<QPointF>& weight,
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

    // Profile knowledge base (change: restructure-kb-as-validated-json).
    // Parsed from the validated resources/ai/profile_knowledge.json.
    // Consumer-facing field names (name/content/analysisFlags/skipCatalog/
    // ugs/ugsInferred) are kept stable so the 14 KB consumer APIs and the
    // existing test contract are byte-unchanged; `id`/`family`/`expertBand`
    // are added for the structured model.
    struct ProfileKnowledge {
        QString id;         // Stable kebab-case identity — the ONLY key.
        QString name;       // displayName (human label; never a key).
        QString content;    // Assembled LLM blob (re-authored prose + the
                            // struct-rendered cited band sentence, D9).
        QStringList analysisFlags;     // suppression flags (analyzeShot)
        bool skipCatalog = false;      // cross-cutting reference, not a profile
        QString family;                // validated-enum cluster tag ("" if skipCatalog)
        double ugs = std::numeric_limits<double>::quiet_NaN();
        bool ugsInferred = false;
        // Cited expert band (absent → std::nullopt → strict no-op, D6).
        std::optional<ShotAnalysis::ExpertBand> expertBand;
    };
    // Keyed by `id` — exactly one entry per canonical identity.
    static QMap<QString, ProfileKnowledge> s_profileKnowledge;
    // Normalized alias → id (displayName + alsoMatches + editor-type
    // defaults). The resolver's sole lookup; exact-match-or-unresolved
    // (the order-dependent greedy startsWith/contains scan is deleted).
    static QMap<QString, QString> s_aliasToId;
    static bool s_knowledgeLoaded;
    static void loadProfileKnowledge();
    static QString matchProfileKey(const QMap<QString, ProfileKnowledge>& knowledge,
                                   const QString& profileTitle, const QString& editorTypeHint);
    // Resolve any caller kbId (a current `id` OR a legacy normalized
    // title/alias persisted on old shot records, D14a) to a canonical
    // `id`; "" when unresolved. Exact-match-or-unresolved, never fuzzy.
    static QString resolveKbInput(const QString& kbId);

    // Profile catalog (compact one-liner per KB profile for cross-profile awareness)
    static QString s_profileCatalog;
    static void buildProfileCatalog();

    // Dial-in reference tables (shared between in-app AI and MCP)
    static QString s_dialInReference;
    static bool s_dialInReferenceLoaded;
    static void loadDialInReference();

    // Cross-profile reference content cache (Skip-Catalog sections)
    static QString s_crossProfileReference;
    static bool s_crossProfileReferenceLoaded;
};
