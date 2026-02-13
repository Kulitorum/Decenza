#pragma once

#include <QObject>
#include <QString>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>

// Abstract base class for AI providers
class AIProvider : public QObject {
    Q_OBJECT

public:
    enum class Status { Ready, Busy, Error };
    Q_ENUM(Status)

    explicit AIProvider(QNetworkAccessManager* networkManager, QObject* parent = nullptr);
    virtual ~AIProvider() = default;

    virtual QString name() const = 0;
    virtual QString id() const = 0;  // "openai", "anthropic", "gemini", "ollama"
    virtual bool isConfigured() const = 0;
    virtual bool isLocal() const { return false; }

    Status status() const { return m_status; }

    // Main analysis method
    virtual void analyze(const QString& systemPrompt, const QString& userPrompt) = 0;

    // Multi-turn conversation method (messages = array of {role, content} objects)
    virtual void analyzeConversation(const QString& systemPrompt, const QJsonArray& messages);

    // Test connection
    virtual void testConnection() = 0;

signals:
    void analysisComplete(const QString& response);
    void analysisFailed(const QString& error);
    void statusChanged(Status status);
    void testResult(bool success, const QString& message);

protected:
    void setStatus(Status status);
    QNetworkAccessManager* m_networkManager = nullptr;
    Status m_status = Status::Ready;
};

// OpenAI GPT-4o provider
class OpenAIProvider : public AIProvider {
    Q_OBJECT

public:
    explicit OpenAIProvider(QNetworkAccessManager* networkManager,
                            const QString& apiKey,
                            QObject* parent = nullptr);

    QString name() const override { return "OpenAI"; }
    QString id() const override { return "openai"; }
    bool isConfigured() const override { return !m_apiKey.isEmpty(); }

    void setApiKey(const QString& key) { m_apiKey = key; }

    void analyze(const QString& systemPrompt, const QString& userPrompt) override;
    void analyzeConversation(const QString& systemPrompt, const QJsonArray& messages) override;
    void testConnection() override;

private slots:
    void onAnalysisReply(QNetworkReply* reply);
    void onTestReply(QNetworkReply* reply);

private:
    QString m_apiKey;
    static constexpr const char* API_URL = "https://api.openai.com/v1/chat/completions";
    static constexpr const char* MODEL = "gpt-4o";
};

// Anthropic Claude Sonnet provider
class AnthropicProvider : public AIProvider {
    Q_OBJECT

public:
    explicit AnthropicProvider(QNetworkAccessManager* networkManager,
                               const QString& apiKey,
                               QObject* parent = nullptr);

    QString name() const override { return "Anthropic"; }
    QString id() const override { return "anthropic"; }
    bool isConfigured() const override { return !m_apiKey.isEmpty(); }

    void setApiKey(const QString& key) { m_apiKey = key; }

    void analyze(const QString& systemPrompt, const QString& userPrompt) override;
    void analyzeConversation(const QString& systemPrompt, const QJsonArray& messages) override;
    void testConnection() override;

private slots:
    void onAnalysisReply(QNetworkReply* reply);
    void onTestReply(QNetworkReply* reply);

private:
    QString m_apiKey;
    static constexpr const char* API_URL = "https://api.anthropic.com/v1/messages";
    static constexpr const char* MODEL = "claude-sonnet-4-5";
};

// Google Gemini Pro provider
class GeminiProvider : public AIProvider {
    Q_OBJECT

public:
    explicit GeminiProvider(QNetworkAccessManager* networkManager,
                            const QString& apiKey,
                            QObject* parent = nullptr);

    QString name() const override { return "Google Gemini"; }
    QString id() const override { return "gemini"; }
    bool isConfigured() const override { return !m_apiKey.isEmpty(); }

    void setApiKey(const QString& key) { m_apiKey = key; }

    void analyze(const QString& systemPrompt, const QString& userPrompt) override;
    void analyzeConversation(const QString& systemPrompt, const QJsonArray& messages) override;
    void testConnection() override;

private slots:
    void onAnalysisReply(QNetworkReply* reply);
    void onTestReply(QNetworkReply* reply);

private:
    QString m_apiKey;
    static constexpr const char* MODEL = "gemini-2.0-flash";
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
    QString m_endpoint;
    QString m_model;
};
