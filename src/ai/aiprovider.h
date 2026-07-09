#pragma once

#include <QObject>
#include <QString>
#include <QJsonArray>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <functional>

// Abstract base class for AI providers
class AIProvider : public QObject {
    Q_OBJECT

public:
    enum class Status { Ready, Busy, Error };
    Q_ENUM(Status)

    // One selectable model offered by a provider. `id` is the wire model
    // string sent to the API; `displayName` is the short label shown in the UI.
    struct ModelOption {
        QString id;
        QString displayName;
    };

    explicit AIProvider(QNetworkAccessManager* networkManager, QObject* parent = nullptr);
    virtual ~AIProvider() = default;

    virtual QString name() const = 0;
    virtual QString id() const = 0;  // "openai", "anthropic", "gemini", "ollama"
    virtual QString modelName() const = 0;
    virtual QString shortModelName() const { return modelName(); }
    virtual bool isConfigured() const = 0;
    virtual bool isLocal() const { return false; }

    // Models the user may pick between for this provider. Default empty = the
    // provider has a single fixed model and shows no model picker. Providers
    // override this to opt into user-selectable models; the catalog is the
    // single source of truth for both the UI list and `shortModelName()`.
    virtual QList<ModelOption> availableModels() const { return {}; }

    // One-line guidance comparing the catalog's models, shown under the model
    // picker in both the in-app AI settings tab and the ShotServer web page.
    // Lives next to availableModels() so the catalog and its guidance share a
    // single source and can't drift between UIs. Empty = no hint.
    virtual QString modelHint() const { return {}; }

    Status status() const { return m_status; }

    // Main analysis method
    virtual void analyze(const QString& systemPrompt, const QString& userPrompt) = 0;

    // Multi-turn conversation method (messages = array of {role, content} objects)
    virtual void analyzeConversation(const QString& systemPrompt, const QJsonArray& messages);

    // Server-side URL retrieval (add-recipe-wizard-tea, stage-2 extraction):
    // analyze() with the provider's web-fetch tool enabled, so the PROVIDER
    // fetches a URL named in the user prompt — the fallback when the local
    // page fetch got nothing (JS-rendered shops). Anthropic (web_fetch),
    // OpenAI (Responses API web_search), and Gemini (url_context) support
    // it; providers without a server-side fetch tool (Ollama, OpenRouter)
    // keep the default (unsupported).
    virtual bool supportsUrlAnalysis() const { return false; }
    virtual void analyzeUrl(const QString& systemPrompt, const QString& userPrompt) {
        Q_UNUSED(systemPrompt); Q_UNUSED(userPrompt);
        emit analysisFailed(QStringLiteral("URL analysis not supported by this provider"));
    }

    // Test connection
    virtual void testConnection() = 0;

signals:
    void analysisComplete(const QString& response);
    void analysisFailed(const QString& error);
    void statusChanged(Status status);
    void testResult(bool success, const QString& message);

protected:
    void setStatus(Status status);

    // Map Qt network errors to user-friendly messages
    static QString friendlyNetworkError(QNetworkReply* reply);

    // Build OpenAI-compatible messages array: system message + conversation messages
    static QJsonArray buildOpenAIMessages(const QString& systemPrompt, const QJsonArray& messages);

    // Retry support: each sendRequest() assigns m_retryFn; each onAnalysisReply() calls tryScheduleRetry()
    bool tryScheduleRetry(QNetworkReply* reply);  // returns true if retry was scheduled

    static constexpr int ANALYSIS_TIMEOUT_MS = 60000;   // 60s for cloud AI analysis
    static constexpr int TEST_TIMEOUT_MS = 15000;        // 15s for connection tests
    static constexpr int MAX_RETRIES = 3;                // max retries for 429/502/503/504

    QNetworkAccessManager* m_networkManager = nullptr;
    Status m_status = Status::Ready;
    std::function<void()> m_retryFn;  // set by each sendRequest() to re-send the pending request
    int m_retryCount = 0;             // reset to 0 before each new analyze() / analyzeConversation() call
    int m_reqGen = 0;                 // incremented on each new request; guards against stale retry timers

private:
    static bool isRetryableHttpStatus(int httpStatus, int retryCount);
    static int computeRetryDelayMs(int retryCount, QNetworkReply* reply);
};

// OpenAI provider
class OpenAIProvider : public AIProvider {
    Q_OBJECT

public:
    explicit OpenAIProvider(QNetworkAccessManager* networkManager,
                            const QString& apiKey,
                            QObject* parent = nullptr);

    QString name() const override { return "OpenAI"; }
    QString id() const override { return "openai"; }
    QString modelName() const override { return m_model; }
    QString shortModelName() const override;  // catalog display for m_model
    bool isConfigured() const override { return !m_apiKey.isEmpty(); }
    QList<ModelOption> availableModels() const override;
    QString modelHint() const override;

    void setApiKey(const QString& key) { m_apiKey = key; }
    // Select the wire model. Ignores empty (keeps current default) and any id
    // not in availableModels(), so a stale/unknown stored value can't break the
    // request.
    void setModel(const QString& modelId);

    void analyze(const QString& systemPrompt, const QString& userPrompt) override;
    void analyzeConversation(const QString& systemPrompt, const QJsonArray& messages) override;
    // OpenAI web search on the Responses API (chat/completions has no general
    // web tool): the model can open a specific URL from the prompt via the
    // tool's open_page action. Uses reasoning effort "low" — web_search needs
    // at least "low"; the gpt-5.4 generation's floor is "none" (it dropped
    // "minimal"), and web_search is rejected at that floor.
    bool supportsUrlAnalysis() const override { return true; }
    void analyzeUrl(const QString& systemPrompt, const QString& userPrompt) override;
    void testConnection() override;

private slots:
    void onAnalysisReply(QNetworkReply* reply);
    void onResponsesReply(QNetworkReply* reply);
    void onTestReply(QNetworkReply* reply);

private:
    void sendRequest(const QJsonObject& requestBody);
    void sendResponsesRequest(const QJsonObject& requestBody);

    QString m_apiKey;
    // Selected wire model. Defaulted in the constructor to the first
    // availableModels() entry (the recommended default), so the C++ default and
    // the UI's "unset → index 0" fallback reference the same fact and can't drift.
    QString m_model;
    static constexpr const char* API_URL = "https://api.openai.com/v1/chat/completions";
    static constexpr const char* RESPONSES_API_URL = "https://api.openai.com/v1/responses";
};

// Anthropic provider
class AnthropicProvider : public AIProvider {
    Q_OBJECT

public:
    explicit AnthropicProvider(QNetworkAccessManager* networkManager,
                               const QString& apiKey,
                               QObject* parent = nullptr);

    QString name() const override { return "Anthropic"; }
    QString id() const override { return "anthropic"; }
    QString modelName() const override { return m_model; }
    QString shortModelName() const override;  // catalog display for m_model
    bool isConfigured() const override { return !m_apiKey.isEmpty(); }
    QList<ModelOption> availableModels() const override;
    QString modelHint() const override;

    void setApiKey(const QString& key) { m_apiKey = key; }
    // Select the wire model. Ignores empty (keeps current default) and any id
    // not in availableModels(), so a stale/unknown stored value can't break the
    // request.
    void setModel(const QString& modelId);

    void analyze(const QString& systemPrompt, const QString& userPrompt) override;
    void analyzeConversation(const QString& systemPrompt, const QJsonArray& messages) override;
    // Anthropic web_fetch server tool (web_fetch_20250910): the API fetches
    // the URL named in the user prompt during the request — no client-side
    // round trip. URL validation requires the URL to appear in the message,
    // which the extraction prompt guarantees.
    bool supportsUrlAnalysis() const override { return true; }
    void analyzeUrl(const QString& systemPrompt, const QString& userPrompt) override;
    void testConnection() override;

private slots:
    void onAnalysisReply(QNetworkReply* reply);
    void onTestReply(QNetworkReply* reply);

private:
    void sendRequest(const QJsonObject& requestBody);
    static QJsonArray buildCachedSystemPrompt(const QString& systemPrompt);

    // Wrap the first user message's content in a structured block carrying
    // cache_control: ephemeral when its content is currently a plain string.
    // Multi-turn conversations on the same shot reuse the cached per-shot
    // payload across follow-up turns within the 5-minute TTL, paying the
    // ~25% cache-write surcharge once and amortizing it across reads.
    static QJsonArray messagesWithCachedFirstUser(const QJsonArray& messages);

    QString m_apiKey;
    // Selected wire model. Defaulted in the constructor to the first
    // availableModels() entry (the recommended default), so the C++ default and
    // the UI's "unset → index 0" fallback reference the same fact and can't drift.
    QString m_model;
    static constexpr const char* API_URL = "https://api.anthropic.com/v1/messages";
};

// Google Gemini provider
class GeminiProvider : public AIProvider {
    Q_OBJECT

public:
    explicit GeminiProvider(QNetworkAccessManager* networkManager,
                            const QString& apiKey,
                            QObject* parent = nullptr);

    QString name() const override { return "Google Gemini"; }
    QString id() const override { return "gemini"; }
    QString modelName() const override { return m_model; }
    QString shortModelName() const override;  // catalog display for m_model
    bool isConfigured() const override { return !m_apiKey.isEmpty(); }
    QList<ModelOption> availableModels() const override;
    QString modelHint() const override;

    void setApiKey(const QString& key) { m_apiKey = key; }
    // Select the wire model. Ignores empty (keeps current default) and any id
    // not in availableModels(), so a stale/unknown stored value can't break the
    // request URL.
    void setModel(const QString& modelId);

    void analyze(const QString& systemPrompt, const QString& userPrompt) override;
    void analyzeConversation(const QString& systemPrompt, const QJsonArray& messages) override;
    // Gemini url_context server tool: the API fetches URLs named in the
    // prompt during generateContent (supported by every catalog model —
    // 2.5 and 3.5 families).
    bool supportsUrlAnalysis() const override { return true; }
    void analyzeUrl(const QString& systemPrompt, const QString& userPrompt) override;
    void testConnection() override;

private slots:
    void onAnalysisReply(QNetworkReply* reply);
    void onTestReply(QNetworkReply* reply);

private:
    void sendRequest(const QJsonObject& requestBody);

    QString m_apiKey;
    // Selected wire model. Defaulted in the constructor to the first
    // availableModels() entry (the recommended default), so the C++ default and
    // the UI's "unset → index 0" fallback reference the same fact and can't drift.
    QString m_model;
    QString apiUrl() const;
};

// OpenRouter provider (multiple models via OpenAI-compatible API)
class OpenRouterProvider : public AIProvider {
    Q_OBJECT

public:
    explicit OpenRouterProvider(QNetworkAccessManager* networkManager,
                                 const QString& apiKey,
                                 const QString& model,
                                 QObject* parent = nullptr);

    QString name() const override { return "OpenRouter"; }
    QString id() const override { return "openrouter"; }
    QString modelName() const override { return m_model; }
    QString shortModelName() const override { return "Multi"; }
    bool isConfigured() const override { return !m_apiKey.isEmpty() && !m_model.isEmpty(); }

    void setApiKey(const QString& key) { m_apiKey = key; }
    void setModel(const QString& model) { m_model = model; }

    void analyze(const QString& systemPrompt, const QString& userPrompt) override;
    void analyzeConversation(const QString& systemPrompt, const QJsonArray& messages) override;
    void testConnection() override;

private slots:
    void onAnalysisReply(QNetworkReply* reply);
    void onTestReply(QNetworkReply* reply);

private:
    void sendRequest(const QJsonObject& requestBody);

    QString m_apiKey;
    QString m_model;
    static constexpr const char* API_URL = "https://openrouter.ai/api/v1/chat/completions";
};

// Ollama local LLM provider
class OllamaProvider : public AIProvider {
    Q_OBJECT

public:
    explicit OllamaProvider(QNetworkAccessManager* networkManager,
                            const QString& endpoint,
                            const QString& model,
                            QObject* parent = nullptr);

    QString name() const override { return "Ollama"; }
    QString id() const override { return "ollama"; }
    QString modelName() const override { return m_model; }
    QString shortModelName() const override { return "Local"; }
    bool isConfigured() const override { return !m_endpoint.isEmpty() && !m_model.isEmpty(); }
    bool isLocal() const override { return true; }

    void setEndpoint(const QString& endpoint) { m_endpoint = endpoint; }
    void setModel(const QString& model) { m_model = model; }

    void analyze(const QString& systemPrompt, const QString& userPrompt) override;
    void analyzeConversation(const QString& systemPrompt, const QJsonArray& messages) override;
    void testConnection() override;

    // Get available models from Ollama
    void refreshModels();

signals:
    void modelsRefreshed(const QStringList& models);

private slots:
    void onAnalysisReply(QNetworkReply* reply);
    void onTestReply(QNetworkReply* reply);
    void onModelsReply(QNetworkReply* reply);

private:
    void sendRequest(const QUrl& url, const QJsonObject& requestBody);

    static constexpr int LOCAL_ANALYSIS_TIMEOUT_MS = 120000;  // 120s for local models
    QString m_endpoint;
    QString m_model;
};
