#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonArray>
#include <QJsonObject>
#include <QVariantMap>
#include <memory>

class QNetworkAccessManager;
class AIProvider;
class AIConversation;
class ShotSummarizer;
class ShotDataModel;
class Profile;
class Settings;
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
    explicit AIManager(Settings* settings, QObject* parent = nullptr);
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

    // Conversation routing
    Q_INVOKABLE QString switchConversation(const QString& beanBrand, const QString& beanType, const QString& profileName);
    Q_INVOKABLE void loadMostRecentConversation();
    Q_INVOKABLE void clearCurrentConversation();
    Q_INVOKABLE bool isMistakeShot(const QVariantMap& shotData) const;
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
                                  const QString& grinderModel,
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
    Q_INVOKABLE QString generateHistoryShotSummary(const QVariantMap& shotData);

    // Provider testing
    Q_INVOKABLE void testConnection();

    // Generic analysis - sends system prompt and user prompt to current provider
    Q_INVOKABLE void analyze(const QString& systemPrompt, const QString& userPrompt);

    // Multi-turn conversation - sends system prompt and full message array to current provider
    void analyzeConversation(const QString& systemPrompt, const QJsonArray& messages);

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

private slots:
    void onAnalysisComplete(const QString& response);
    void onAnalysisFailed(const QString& error);
    void onTestResult(bool success, const QString& message);
    void onOllamaModelsRefreshed(const QStringList& models);
    void onSettingsChanged();

private:
    void createProviders();
    AIProvider* currentProvider() const;
    ShotMetadata buildMetadata(const QString& beanBrand,
                                const QString& beanType,
                                const QString& roastDate,
                                const QString& roastLevel,
                                const QString& grinderModel,
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

    // Conversation routing
    void loadConversationIndex();
    void saveConversationIndex();
    void touchConversationEntry(const QString& key);
    void evictOldestConversation();
    void migrateFromLegacyConversation();

    // Conversation for multi-turn interactions
    AIConversation* m_conversation = nullptr;
    QList<ConversationEntry> m_conversationIndex;
};
