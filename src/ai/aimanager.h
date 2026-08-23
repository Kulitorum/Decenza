#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonArray>
#include <QJsonObject>
#include <QVariantMap>
#include <QVariantList>
#include <QPair>
#include <memory>
#include <optional>

#include "../history/shotprojection.h"
#include "../history/shothistory_types.h"

#include <QtQml/qqmlregistration.h>
class QNetworkAccessManager;
class AIProvider;
class AIConversation;
class ShotSummarizer;
class ShotDataModel;
class Profile;
class Settings;
class ShotHistoryStorage;
class TranslationManager;
class ProfileManager;

class AIManager : public QObject {
    Q_OBJECT

    // Compile-time QML registration, so qmllint, qmlcachegen and the language server can
    // follow MainController's property through to this class. A runtime qmlRegister* call is
    // invisible to all three. Full rationale in src/controllers/maincontroller.h.
    QML_ELEMENT
    QML_UNCREATABLE("AIManager is created in C++ and reached via MainController")

    Q_PROPERTY(QString selectedProvider READ selectedProvider WRITE setSelectedProvider NOTIFY providerChanged)
    Q_PROPERTY(QStringList availableProviders READ availableProviders CONSTANT)
    Q_PROPERTY(bool isConfigured READ isConfigured NOTIFY configurationChanged)
    Q_PROPERTY(bool isAnalyzing READ isAnalyzing NOTIFY analyzingChanged)
    Q_PROPERTY(QString lastRecommendation READ lastRecommendation NOTIFY recommendationReceived)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorOccurred)
    Q_PROPERTY(QString lastTestResult READ lastTestResult NOTIFY testResultChanged)
    Q_PROPERTY(bool lastTestSuccess READ lastTestSuccess NOTIFY testResultChanged)
    Q_PROPERTY(QStringList ollamaModels READ ollamaModels NOTIFY ollamaModelsChanged)
    Q_PROPERTY(QString currentModelName READ currentModelName NOTIFY providerChanged)
    Q_PROPERTY(AIConversation* conversation READ conversation CONSTANT)
    Q_PROPERTY(bool hasAnyConversation READ hasAnyConversation NOTIFY conversationIndexChanged)

public:
    explicit AIManager(QNetworkAccessManager* networkManager, Settings* settings, QObject* parent = nullptr);
    ~AIManager();

    static constexpr int MAX_CONVERSATIONS = 5;

    struct ConversationEntry {
        QString key;
        QString beanBrand;
        QString beanType;
        QString profileName;
        // Equipment package the thread belongs to (0 = none recorded). Carried
        // so a conversation list can tell apart two threads that share a bean
        // and profile but were pulled on different gear — without it they are
        // indistinguishable rows. Reported as `equipmentPackageId` by
        // `ai_conversations list`. Absent on entries saved before equipment
        // became part of the key; those read as 0.
        //
        // DEVICE-LOCAL, and kept that way on import. Package ids are renumbered
        // when a database is imported, so AIConversation::importConversationsStatic
        // remaps this field through the equipment package map AND recomputes the
        // QSettings key from the mapped id — the key is a hash that includes it,
        // so moving one without the other would leave a thread nothing can
        // address. When no map is available (conversations restored without an
        // equipment import), the entry keeps its source key and this field is
        // reset to 0 rather than holding a foreign id: it is reported over MCP as
        // `equipmentPackageId`, where a source-device id would name whichever
        // unrelated local package happens to hold that integer.
        qint64 equipmentId = 0;
        qint64 timestamp;

        QJsonObject toJson() const;
        static ConversationEntry fromJson(const QJsonObject& obj);
    };

    // Properties
    QString selectedProvider() const;
    void setSelectedProvider(const QString& provider);
    QStringList availableProviders() const;
    bool isConfigured() const;
    bool isAnalyzing() const { return m_analyzing; }
    QString lastRecommendation() const { return m_lastRecommendation; }
    QString lastError() const { return m_lastError; }
    QString lastTestResult() const { return m_lastTestResult; }
    bool lastTestSuccess() const { return m_lastTestSuccess; }
    QStringList ollamaModels() const { return m_ollamaModels; }
    QString currentModelName() const;
    Q_INVOKABLE QString modelDisplayName(const QString& providerId) const;
    // Selectable models for a provider as a list of { "id", "name" } maps, in UI
    // order (first = recommended default). Empty when the provider has a single
    // fixed model — the UI hides the model picker in that case.
    Q_INVOKABLE QVariantList availableModels(const QString& providerId) const;
    // One-line guidance comparing the provider's catalog models (see
    // AIProvider::modelHint). Empty when the provider has no hint.
    Q_INVOKABLE QString modelHint(const QString& providerId) const;
    // Running-cost estimate (see AIProvider::costHintFor). Pass a modelId to
    // price a specific model, or leave it empty for the provider's current
    // selection. Depends on the model, so re-read it when the selection changes
    // — a per-provider figure is wrong across a catalog that spans 10x. Empty
    // when the provider has no catalog to price.
    Q_INVOKABLE QString costHint(const QString& providerId,
                                 const QString& modelId = QString()) const;
    AIConversation* conversation() const { return m_conversation; }
    bool hasAnyConversation() const { return !m_conversationIndex.isEmpty(); }
    QList<ConversationEntry> conversationIndex() const { return m_conversationIndex; }

    // Conversation routing
    // `equipmentId` is the shot's equipment package (0 = none recorded). It is
    // part of the conversation IDENTITY, not merely context: a saved thread
    // replays its stored turns to the model on every request, so a thread that
    // spans two baskets keeps feeding the model shots from gear the user is no
    // longer using. Threading on the package keeps a transcript describing one
    // equipment set for its whole life — and, because every pre-existing key
    // stops matching, gives every user a clean thread on first use after
    // upgrading with no migration step.
    Q_INVOKABLE QString switchConversation(const QString& beanBrand, const QString& beanType,
                                           const QString& profileName, qint64 equipmentId);
    Q_INVOKABLE void loadMostRecentConversation();
    Q_INVOKABLE void clearCurrentConversation();
    // Accepts QVariant (not const ShotProjection&) so QML can pass either a
    // real ShotProjection or the plain-JS edit clone from
    // PostShotReviewPage.clonePersistedShot — the latter can't bind to a
    // ShotProjection parameter and threw at runtime (#1298). Coerced via
    // coerceShot() in the .cpp.
    Q_INVOKABLE bool isMistakeShot(const QVariant& shotData) const;
    Q_INVOKABLE bool isSupportedBeverageType(const QString& beverageType) const;
    // No default on `equipmentId`: omitting it for a shot that IS on a package
    // would silently file the thread under the unpackaged bucket's key, mixing
    // it with every shot the user has that carries no equipment at all. Every
    // caller has the value; make them say it.
    static QString conversationKey(const QString& beanBrand, const QString& beanType,
                                   const QString& profileName, qint64 equipmentId);

    // Builds the AI user-prompt envelope for a finished / historical shot,
    // returned as a `QJsonObject` so DB-scoped callers (`ai_advisor_invoke`'s
    // bg-thread closure) can append the four dialing-context blocks before
    // serializing. Returns an empty object when summarization fails. The live
    // advisor + conversation flows summarize from the ShotProjection directly
    // (this and buildShotAnalysisProseForShot below) — there is no separate
    // QVariantMap/ShotMetadata analyze path.
    QJsonObject buildUserPromptObjectForShot(const ShotProjection& shotData);

    // Prose-only shot analysis — no JSON envelope, no double-shipped
    // structured fields. Used by `dialing_get_context` to populate
    // `result.shotAnalysis` (the structured fields already live at the
    // top level of the response), and by the in-app conversation
    // overlay's QML to seed change-detection prose for the AI Advice
    // button (qml/components/ConversationOverlay.qml). The prose is
    // identical to the `shotAnalysis` field inside
    // `buildUserPromptObjectForShot(shot)` when that envelope is built
    // in `Standalone` mode — both paths call
    // `ShotSummarizer::renderShotAnalysisProse` with `RenderMode::Standalone`.
    // QVariant param (not const ShotProjection&) — same reason as
    // isMistakeShot above: QML may pass the plain-JS edit clone. C++ callers
    // (e.g. mcptools_dialing) wrap with QVariant::fromValue(shot).
    Q_INVOKABLE QString buildShotAnalysisProseForShot(const QVariant& shotData);

    // Merge the four dialing-context blocks into a user-prompt envelope.
    // Both the in-app advisor and `ai_advisor_invoke` call this on the
    // main-thread continuation of their bg-thread DB closures, after they
    // produce `dialInSessions` / `bestRecentShot` / `grinderContext` from
    // their own DB connections. The SAW block is built here (it touches
    // `Settings::calibration()` and `ProfileManager`, both main-thread
    // only). Empty blocks are suppressed — no key, no null placeholder.
    //
    // Single source of truth for the merge step, so the in-app and MCP
    // surfaces cannot drift on which blocks land where.
    void enrichUserPromptObject(QJsonObject& payload,
                                const ShotProjection& shotData,
                                const QJsonArray& dialInSessions,
                                const QJsonObject& bestRecentShot,
                                const QJsonObject& grinderContext,
                                const QJsonArray& recentAdvice = QJsonArray(),
                                const QJsonObject& grinderCalibration = QJsonObject()) const;

    // Shot history access for contextual recommendations
    void setShotHistoryStorage(ShotHistoryStorage* storage);
    // Null until wired (and in tests that never wire it) — callers must check.
    // True while a metadata write THIS class started is awaiting its outcome.
    //
    // main.qml uses it to suppress ShotHistoryStorage's generic "please try
    // again" toast, which is emitted immediately before shotMetadataCaptureFailed
    // and would otherwise be ANNOUNCED in full to a screen-reader user before the
    // advisor-specific message replaced it on screen. Retrying cannot help when
    // the shot does not exist, so the generic advice is worse than silence.
    //
    // Approximate by design: errorOccurred carries no shot id, so a different
    // subsystem's failure arriving while one of ours is in flight is suppressed
    // too. The window is one queued block wide.
    Q_INVOKABLE bool hasPendingShotMetadataWrite() const { return !m_pendingMetadataWrites.isEmpty(); }

    ShotHistoryStorage* shotHistoryStorage() const { return m_shotHistory; }

    // Inject the TranslationManager so user-visible error strings localize.
    // Forwards to every owned provider and the conversation. Wired directly in
    // main.cpp (after MainController::setAiManager, since MainController's own
    // setTranslationManager runs before the AIManager is attached).
    void setTranslationManager(TranslationManager* tm);
    // ProfileManager hookup for the SAW prediction block (needs
    // baseProfileName + profile target metadata at user-prompt enrichment
    // time). Wired from MainController::setAiManager. Optional — falls
    // back to omitting the SAW block when null.
    void setProfileManager(ProfileManager* profileManager) { m_profileManager = profileManager; }
    Q_INVOKABLE void requestRecentShotContext(const QString& beanBrand, const QString& beanType, const QString& profileName, int excludeShotId);

    // Provider testing
    Q_INVOKABLE void testConnection();

    // Generic analysis - sends system prompt and user prompt to current provider
    Q_INVOKABLE void analyze(const QString& systemPrompt, const QString& userPrompt);

    // Extract structured coffee-bag details from a roaster product page's
    // plain text (add-bag-detail-editing "Get info"). Same provider plumbing
    // as analyze(), but completes via bagDetailsExtracted / -Failed so the
    // advisor's recommendationReceived listeners never see extraction JSON.
    // requestToken (the page URL) is echoed on both completion signals so the
    // caller can discard a stale extraction — an LLM call takes long enough
    // that the user may have moved on to a different bag by the time it lands.
    // Guard failures use stable codes ("busy", "notConfigured", "unreadable")
    // the QML layer translates; provider errors pass through as text.
    // `kind` selects the extraction vocabulary: "coffee" (default) or "tea"
    // (add-recipe-wizard-tea) — tea pages yield teaType/garden/cultivar/flush
    // plus structured brewing fields (brewTempC normalized to Celsius,
    // leafGramsPer100Ml normalized from per-cup wordings, steepTime).
    Q_INVOKABLE void extractCoffeeBagDetails(const QString& requestToken, const QString& pageText,
                                             const QString& kind = QStringLiteral("coffee"));
    // Stage-2 extraction fallback: the local page fetch got nothing (a
    // JS-rendered shop), so the provider fetches the URL itself via its
    // server-side web tool (Anthropic web_fetch, OpenAI Responses web_search,
    // Gemini url_context; Ollama/OpenRouter report "urlFetchUnsupported").
    // Same signals + JSON contract as stage 1, plus an imageUrl key (SPA
    // pages have no og:image for the photo pipeline). Gate calls on
    // supportsUrlExtraction().
    Q_INVOKABLE bool supportsUrlExtraction() const;
    Q_INVOKABLE void extractCoffeeBagDetailsFromUrl(const QString& requestToken, const QString& url,
                                                    const QString& kind = QStringLiteral("coffee"));
    // Response JSON -> whitelisted blob-vocabulary fields (coffee: origin,
    // region, farm, producer, variety, elevation, process, harvest,
    // roastLevel, tastingNotes; tea adds teaType, garden, cultivar, flush,
    // brewTempC, leafGramsPer100Ml, steepTime). Tolerates markdown fences;
    // string-array values are joined ", "; object values are skipped;
    // values capped at 500 chars.
    // ok=false when nothing parses OR the object had content but no usable
    // whitelisted values ("couldn't read it" is distinct from the honest
    // empty-object "the page states nothing"). Static + public for tests.
    static QVariantMap parseBagExtraction(const QString& response, bool* ok = nullptr);

    // Multi-turn conversation - sends system prompt and full message array to current provider
    void analyzeConversation(const QString& systemPrompt, const QJsonArray& messages);

    // Extract the trailing fenced ```json block from an assistant message.
    // The shot-analysis system prompt asks the model to append a `nextShot`
    // JSON object at end-of-message when its response makes a concrete
    // parameter recommendation. Returns the parsed object when found,
    // std::nullopt when absent or unparseable. Mid-message fenced blocks
    // are intentionally ignored — only a block whose closing ``` is the
    // last non-whitespace content qualifies.
    //
    // Pure / static so callers without an AIManager (test harnesses,
    // ai_advisor_invoke before the provider hop) can use it.
    static std::optional<QJsonObject> parseStructuredNext(const QString& assistantMessage);

    // Strip Decenza-internal per-turn keys (shotId, structuredNext) from a
    // stored conversation, leaving only the {role, content} pair every
    // chat-completion provider accepts. Applied before dispatching to a
    // provider so internal bookkeeping never leaks into the API request —
    // the Anthropic Messages API 400s on unknown per-message fields. Pure /
    // static so the test harness can assert the invariant directly.
    static QJsonArray sanitizeApiMessages(const QJsonArray& messages);

    // Parsed numeric score + remaining notes from a user's conversational
    // reply (issue #1055 Layer 1). When the advisor asks "how did this
    // taste?" and the user answers with a number 1-100, we persist the
    // score back to ShotProjection.enjoyment0to100 + remaining text to
    // espressoNotes — closes the rating loop without forcing the user
    // into the metadata editor.
    struct UserRatingReply {
        int score = 0;     // 1-100
        QString notes;     // remaining text after the score token, trimmed
    };

    // Permissive but conservative parser. A bare integer in [1, 100] is
    // a score; optional suffixes `/100`, `out of 100`, `%` are consumed.
    // Decimal scores round to nearest int. Non-numeric replies ("really
    // good") do NOT yield a score. Multiple numeric tokens → first
    // in-range wins. Static + pure for test isolation.
    static std::optional<UserRatingReply> parseUserRatingReply(const QString& reply);

    // Issue #1055 Layer 1: when the advisor's prior assistant message
    // asked about taste AND the user's reply contains a parseable score,
    // persist the rating + remaining-text notes back to the shot via
    // ShotHistoryStorage. No-op when ANY of:
    //   - shotId is 0 (no shot is paired with the turn — typical for a
    //     legacy conversation or a free-form follow-up),
    //   - m_shotHistory is unset (no DB wired),
    //   - priorAssistantMessage doesn't contain a taste-question marker
    //     (the model wasn't asking; rating writeback would be spurious),
    //   - parseUserRatingReply returns std::nullopt (the user replied
    //     in prose without a numeric score).
    // Called by AIConversation::followUp before the request is dispatched.
    void maybePersistRatingFromReply(const QString& userReply,
                                     const QString& priorAssistantMessage,
                                     qint64 shotId);

    // Conversational bean-metadata corrections (capability
    // shot-metadata-capture). When the recorded bean info on a shot is
    // wrong (e.g. roastLevel saved as "Medium-Dark" but the user clarifies
    // mid-conversation that the coffee is dark), the parser pulls the
    // correction out of the user's reply so the app can write it back to
    // ShotProjection. Sparse: only fields the user explicitly corrected
    // are populated.
    struct BeanCorrection {
        std::optional<QString> roastLevel;  // canonical: Light/Medium-Light/Medium/Medium-Dark/Dark
        std::optional<QString> beanBrand;
        std::optional<QString> roastDate;   // ISO yyyy-MM-dd
        bool isEmpty() const {
            return !roastLevel && !beanBrand && !roastDate;
        }
    };

    // Conservative parser. Returns std::nullopt when the reply contains no
    // recognisable bean-correction patterns. Compound phrases like "dark
    // chocolate notes" or "light citrus" do NOT yield a roastLevel; the
    // parser requires a context word ("the coffee/bean/roast is X",
    // "actually X") to bind the adjective to roast level. Static + pure
    // for test isolation.
    static std::optional<BeanCorrection> parseBeanCorrectionsFromReply(const QString& reply);

    // Persist a parsed BeanCorrection back to the anchored shot via
    // ShotHistoryStorage. No-op when ANY of:
    //   - shotId is 0,
    //   - m_shotHistory is unset,
    //   - parser returns std::nullopt,
    //   - neither the user reply carries explicit corrective phrasing
    //     ("actually...", "the coffee/bean/roast is...") NOR the prior
    //     assistant message asked about beans (the gating mirrors
    //     maybePersistRatingFromReply's "must be answering a question"
    //     stance to keep false positives low).
    // Called by AIConversation::followUp alongside the rating-write hook.
    void maybePersistBeanCorrectionFromReply(const QString& userReply,
                                              const QString& priorAssistantMessage,
                                              qint64 shotId);

    // Ollama-specific
    Q_INVOKABLE void refreshOllamaModels();

signals:
    void providerChanged();
    void configurationChanged();
    void analyzingChanged();
    void recommendationReceived(const QString& recommendation);
    void errorOccurred(const QString& error);
    // "Get info" extraction results (never routed to the advisor signals).
    // requestToken = the value passed to extractCoffeeBagDetails.
    void bagDetailsExtracted(const QString& requestToken, const QVariantMap& fields);
    void bagDetailsExtractionFailed(const QString& requestToken, const QString& error);
    // A metadata write this class made — capturing something the user told the
    // advisor — landed on a shot id that does not exist, so the value was
    // discarded. Emitted so the failure is addressable instead of vanishing
    // into a log line. What to DO about it (re-ask, retry, tell the user) is
    // deliberately not decided here.
    void shotMetadataCaptureFailed(qint64 shotId);
    void testResultChanged();
    void ollamaModelsChanged();
    void conversationIndexChanged();
    void recentShotContextReady(const QString& context);
    void conversationResponseReceived(const QString& response);
    void conversationErrorOccurred(const QString& error);

private slots:
    void onAnalysisComplete(const QString& response);
    void onAnalysisFailed(const QString& error);
    void onTestResult(bool success, const QString& message);
    void onOllamaModelsRefreshed(const QStringList& models);
    void onSettingsChanged();

private:
    void createProviders();
    // Translate a user-visible string via the injected TranslationManager,
    // falling back to the English source when none is set.
    QString tr_(const char* key, const char* fallback) const;
    AIProvider* providerById(const QString& providerId) const;
    AIProvider* currentProvider() const;

    // Logging
    QString logPath() const;
    void logPrompt(const QString& provider, const QString& systemPrompt, const QString& userPrompt);
    void logResponse(const QString& provider, const QString& response, bool success);

    Settings* m_settings = nullptr;
    QNetworkAccessManager* m_networkManager = nullptr;
    std::unique_ptr<ShotSummarizer> m_summarizer;
    ShotHistoryStorage* m_shotHistory = nullptr;
    // Shot ids this class has a metadata write in flight for, REFCOUNTED: one
    // reply can drive two writes for the same shot, and shotMetadataUpdated
    // carries every subsystem's writes, so without this we would report other
    // people's failures as ours. See setShotHistoryStorage.
    QHash<qint64, int> m_pendingMetadataWrites;
    ProfileManager* m_profileManager = nullptr;

    // Providers
    std::unique_ptr<AIProvider> m_openaiProvider;
    std::unique_ptr<AIProvider> m_anthropicProvider;
    std::unique_ptr<AIProvider> m_geminiProvider;
    std::unique_ptr<AIProvider> m_openrouterProvider;
    std::unique_ptr<AIProvider> m_ollamaProvider;

    // State
    bool m_analyzing = false;
    QString m_lastRecommendation;
    QString m_lastError;
    QString m_lastTestResult;
    bool m_lastTestSuccess = false;
    QStringList m_ollamaModels;

    // For logging - store last prompts to pair with response
    QString m_lastSystemPrompt;
    QString m_lastUserPrompt;

    // Serial counter for requestRecentShotContext (discard stale results)
    int m_contextSerial = 0;

public:
    void reloadConversations() { loadConversationIndex(); }
private:
    void loadConversationIndex();
    void saveConversationIndex();
    // Move `key`'s index entry to the front of the LRU. No-op when absent —
    // deliberately. An entry for a key with no stored thread is a GHOST: the
    // conversation list and `ai_conversations list` show it with
    // `messageCount: 0` while `get` answers "Conversation not found", and it
    // occupies one of the five LRU slots, so the next real thread evicts a real
    // transcript to make room for it.
    void touchConversationEntry(const QString& key);

    // Move `key`'s entry to the front, CREATING it when absent. Only for a key
    // that has, or is about to have, a stored thread.
    void noteConversationUse(const QString& key, const QString& beanBrand,
                             const QString& beanType, const QString& profileName,
                             qint64 equipmentId);

    // Make sure the index names the live conversation's key, IF that key has a
    // stored thread behind it. Two callers, for two different moments:
    //
    //  - the `conversationPersisted` wiring, for "Clear, then keep talking about
    //    the same shot". `clearCurrentConversation` drops the entry but leaves
    //    the conversation on its key, and the send path calls
    //    `switchConversation` BEFORE `ask()`, so at switch time there is nothing
    //    yet to point at.
    //  - `switchConversation` itself, for a thread the MCP `ai_advisor_invoke`
    //    path wrote directly to QSettings, which the in-app index has never seen.
    //
    // The on-disk gate is what keeps both from manufacturing ghosts.
    void indexStoredConversation();

    // Identity of the conversation `m_conversation` is currently on, remembered
    // so `indexStoredConversation()` can build a complete entry. The key alone
    // is a hash and cannot be reversed into the fields the list displays.
    QString m_liveBeanBrand;
    QString m_liveBeanType;
    QString m_liveProfileName;
    qint64 m_liveEquipmentId = 0;
    // Drop least-recently-used entries until at most `keep` remain, deleting each
    // one's stored transcript. Callers pass MAX_CONVERSATIONS - 1 before adding a
    // new thread, or MAX_CONVERSATIONS to enforce the cap on an index read back
    // from disk.
    void trimConversationsTo(int keep);
    void migrateFromLegacyConversation();
    // One-shot conversation wipe keyed by a migration id. Fires once per
    // device; subsequent launches are no-ops. Call before loadConversationIndex.
    static void clearAllConversationsOnce(const QString& migrationId);

    // Candidate history for the in-app advisor: the shots sharing this shot's
    // bean, profile AND equipment package, within a 21-day window ending at it.
    // Opens its own SQLite connection from `dbPath` — background thread only,
    // never the main thread's connection.
    //
    // A private static member rather than a file-scope helper so
    // `friend class tst_AIManager` can assert the equipment scoping on a real
    // database — the scoping is the whole point of this function, and there is
    // no other seam that reaches its query.
    // `outHistoryReadable` (optional) reports whether the queries actually ran.
    // An empty return means "no qualifying shots" OR "the read failed", and the
    // caller cannot treat those alike: it turns the first into an explicit
    // statement to the model that no prior shots exist on this equipment, which
    // is a fabricated fact if the truth is that the query errored.
    static QList<QPair<qint64, ShotProjection>> loadQualifiedShots(
        const QString& dbPath,
        const QString& beanBrand, const QString& beanType,
        const QString& profileName, int excludeShotId,
        bool* outHistoryReadable = nullptr);

    // Render the recent-shot-context prose from already-loaded data and
    // emit `recentShotContextReady` (or an empty string when stale).
    // `requestRecentShotContext`'s main-thread lambda calls this helper
    // after the background DB work resolves. Extracted so the
    // canonical-source separation logic (Profile/Setup hoisting,
    // HistoryBlock per-shot rendering) can be exercised by tests via
    // `friend class tst_AIManager` without standing up a real DB.
    void emitRecentShotContext(
        const QList<QPair<qint64, ShotProjection>>& qualifiedShots,
        const GrinderContext& grinderCtx,
        const QString& grinderBrand,
        int serial,
        const QJsonObject& grinderCalibration = QJsonObject(),
        const QJsonArray& recentAdvice = QJsonArray(),
        // English description of the current shot's equipment package. Only
        // consulted when the history came back empty, to state which equipment
        // set the shots were matched on instead of emitting nothing. May be
        // empty for a package whose component rows are all missing, which is
        // why it is not the thing the empty-history branch discriminates on.
        const QString& equipmentLabel = QString(),
        // Did the equipment read succeed, and what did it return? The history
        // is scoped on a SEPARATE connection that resolves the bucket itself,
        // so a failure here does NOT mean the history came back unscoped — it
        // means shots were excluded and this side cannot say by what. Passing
        // the fact rather than inferring it from `equipmentLabel` is what keeps
        // "no package, nothing was filtered" apart from "filtered, but the
        // equipment could not be read".
        bool equipmentBucketKnown = false,
        qint64 equipmentBucket = 0,
        // False when the history QUERY failed, as opposed to returning nothing.
        // Separate from equipmentBucketKnown: that one describes the grinder-
        // context connection, this one the history connection, and either can
        // fail alone.
        bool historyReadable = true);

    // Conversation for multi-turn interactions
    AIConversation* m_conversation = nullptr;
    TranslationManager* m_translationManager = nullptr;
    QList<ConversationEntry> m_conversationIndex;
    bool m_isConversationRequest = false;
    bool m_isBagExtractionRequest = false;
    QString m_bagExtractionToken;

#ifdef DECENZA_TESTING
    friend class tst_AIManager;
#endif
};
