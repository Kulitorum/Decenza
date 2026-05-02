#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonArray>
#include <QJsonObject>
#include <QVariantMap>
#include <QPair>
#include <memory>
#include <optional>

#include "../history/shotprojection.h"
#include "../history/shothistory_types.h"

class QNetworkAccessManager;
class AIProvider;
class AIConversation;
class ShotSummarizer;
class ShotDataModel;
class Profile;
class Settings;
class ShotHistoryStorage;
class ProfileManager;
struct ShotMetadata;

class AIManager : public QObject {
    Q_OBJECT

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
    AIConversation* conversation() const { return m_conversation; }
    bool hasAnyConversation() const { return !m_conversationIndex.isEmpty(); }
    QList<ConversationEntry> conversationIndex() const { return m_conversationIndex; }

    // Conversation routing
    Q_INVOKABLE QString switchConversation(const QString& beanBrand, const QString& beanType, const QString& profileName);
    Q_INVOKABLE void loadMostRecentConversation();
    Q_INVOKABLE void clearCurrentConversation();
    Q_INVOKABLE bool isMistakeShot(const ShotProjection& shotData) const;
    Q_INVOKABLE bool isSupportedBeverageType(const QString& beverageType) const;
    static QString conversationKey(const QString& beanBrand, const QString& beanType, const QString& profileName);

    // Main analysis entry point - simple version for QML
    // Note: metadata must be passed (use {} for empty) to avoid QML overload confusion
    Q_INVOKABLE void analyzeShot(ShotDataModel* shotData,
                                  Profile* profile,
                                  double doseWeight,
                                  double finalWeight,
                                  const QVariantMap& metadata);

    // Full version for C++ callers
    void analyzeShotWithMetadata(ShotDataModel* shotData,
                                  const Profile* profile,
                                  double doseWeight,
                                  double finalWeight,
                                  const QString& beanBrand,
                                  const QString& beanType,
                                  const QString& roastDate,
                                  const QString& roastLevel,
                                  const QString& grinderBrand,
                                  const QString& grinderModel,
                                  const QString& grinderBurrs,
                                  const QString& grinderSetting,
                                  int enjoymentScore,
                                  const QString& tastingNotes);

    // Email fallback - generates prompt for copying
    Q_INVOKABLE QString generateEmailPrompt(ShotDataModel* shotData,
                                             Profile* profile,
                                             double doseWeight,
                                             double finalWeight,
                                             const QVariantMap& metadata);

    // Generate shot summary text for multi-shot conversation
    Q_INVOKABLE QString generateShotSummary(ShotDataModel* shotData,
                                             Profile* profile,
                                             double doseWeight,
                                             double finalWeight,
                                             const QVariantMap& metadata);

    // Generate shot summary from historical shot data (for ShotDetailPage)
    Q_INVOKABLE QString generateHistoryShotSummary(const ShotProjection& shotData);

    // Same envelope `generateHistoryShotSummary` serializes, but returned as
    // a `QJsonObject` so DB-scoped callers (`ai_advisor_invoke`'s bg-thread
    // closure) can append the four dialing-context blocks before
    // serializing. Returns an empty object when summarization fails.
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
    Q_INVOKABLE QString buildShotAnalysisProseForShot(const ShotProjection& shotData);

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
                                const QJsonArray& recentAdvice = QJsonArray()) const;

    // Shot history access for contextual recommendations
    void setShotHistoryStorage(ShotHistoryStorage* storage);
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

    // Ollama-specific
    Q_INVOKABLE void refreshOllamaModels();

signals:
    void providerChanged();
    void configurationChanged();
    void analyzingChanged();
    void recommendationReceived(const QString& recommendation);
    void errorOccurred(const QString& error);
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
    AIProvider* providerById(const QString& providerId) const;
    AIProvider* currentProvider() const;
    ShotMetadata buildMetadata(const QString& beanBrand,
                                const QString& beanType,
                                const QString& roastDate,
                                const QString& roastLevel,
                                const QString& grinderBrand,
                                const QString& grinderModel,
                                const QString& grinderBurrs,
                                const QString& grinderSetting,
                                int enjoymentScore,
                                const QString& tastingNotes) const;

    // Logging
    QString logPath() const;
    void logPrompt(const QString& provider, const QString& systemPrompt, const QString& userPrompt);
    void logResponse(const QString& provider, const QString& response, bool success);

    Settings* m_settings = nullptr;
    QNetworkAccessManager* m_networkManager = nullptr;
    std::unique_ptr<ShotSummarizer> m_summarizer;
    ShotHistoryStorage* m_shotHistory = nullptr;
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
    void touchConversationEntry(const QString& key);
    void evictOldestConversation();
    void migrateFromLegacyConversation();

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
        int serial);

    // Conversation for multi-turn interactions
    AIConversation* m_conversation = nullptr;
    QList<ConversationEntry> m_conversationIndex;
    bool m_isConversationRequest = false;

#ifdef DECENZA_TESTING
    friend class tst_AIManager;
#endif
};
