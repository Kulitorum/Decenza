#pragma once

#include <QObject>
#include <QSettings>
#include <QString>

// AI Dialing Assistant settings: provider selection, API keys, endpoints.
class SettingsAI : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString aiProvider READ aiProvider WRITE setAiProvider NOTIFY aiProviderChanged)
    Q_PROPERTY(QString openaiApiKey READ openaiApiKey WRITE setOpenaiApiKey NOTIFY openaiApiKeyChanged)
    Q_PROPERTY(QString anthropicApiKey READ anthropicApiKey WRITE setAnthropicApiKey NOTIFY anthropicApiKeyChanged)
    Q_PROPERTY(QString geminiApiKey READ geminiApiKey WRITE setGeminiApiKey NOTIFY geminiApiKeyChanged)
    Q_PROPERTY(QString ollamaEndpoint READ ollamaEndpoint WRITE setOllamaEndpoint NOTIFY ollamaEndpointChanged)
    Q_PROPERTY(QString ollamaModel READ ollamaModel WRITE setOllamaModel NOTIFY ollamaModelChanged)
    Q_PROPERTY(QString openaiEndpoint READ openaiEndpoint WRITE setOpenaiEndpoint NOTIFY openaiEndpointChanged)
    Q_PROPERTY(QString anthropicEndpoint READ anthropicEndpoint WRITE setAnthropicEndpoint NOTIFY anthropicEndpointChanged)
    Q_PROPERTY(QString openrouterApiKey READ openrouterApiKey WRITE setOpenrouterApiKey NOTIFY openrouterApiKeyChanged)
    Q_PROPERTY(QString openrouterModel READ openrouterModel WRITE setOpenrouterModel NOTIFY openrouterModelChanged)
    // When true (default), tapping AI Advice opens the tap-only taste intake
    // picker before the conversation; when false, it opens the conversation
    // directly (pre-existing behavior). See add-ai-taste-intake.
    Q_PROPERTY(bool tasteIntakeOnAsk READ tasteIntakeOnAsk WRITE setTasteIntakeOnAsk NOTIFY tasteIntakeOnAskChanged)

public:
    explicit SettingsAI(QObject* parent = nullptr);

    QString aiProvider() const;
    void setAiProvider(const QString& provider);

    QString openaiApiKey() const;
    void setOpenaiApiKey(const QString& key);

    QString anthropicApiKey() const;
    void setAnthropicApiKey(const QString& key);

    QString geminiApiKey() const;
    void setGeminiApiKey(const QString& key);

    QString ollamaEndpoint() const;
    void setOllamaEndpoint(const QString& endpoint);

    QString ollamaModel() const;
    void setOllamaModel(const QString& model);

    QString openaiEndpoint() const;
    void setOpenaiEndpoint(const QString& endpoint);

    QString anthropicEndpoint() const;
    void setAnthropicEndpoint(const QString& endpoint);

    QString openrouterApiKey() const;
    void setOpenrouterApiKey(const QString& key);

    QString openrouterModel() const;
    void setOpenrouterModel(const QString& model);

    bool tasteIntakeOnAsk() const;
    void setTasteIntakeOnAsk(bool enabled);

    // Per-provider selected model, stored generically under ai/model/<providerId>.
    // Works for any provider that exposes multiple models (see
    // AIProvider::availableModels). Empty string = unset → the provider uses its
    // own default. OpenRouter/Ollama keep their dedicated free-text/list fields
    // above; this covers fixed-catalog cloud providers (Gemini today, others later).
    Q_INVOKABLE QString providerModel(const QString& providerId) const;
    Q_INVOKABLE void setProviderModel(const QString& providerId, const QString& modelId);

signals:
    void aiProviderChanged();
    void openaiApiKeyChanged();
    void anthropicApiKeyChanged();
    void geminiApiKeyChanged();
    void ollamaEndpointChanged();
    void ollamaModelChanged();
    void openaiEndpointChanged();
    void anthropicEndpointChanged();
    void openrouterApiKeyChanged();
    void openrouterModelChanged();
    void tasteIntakeOnAskChanged();
    void providerModelChanged();

    // Aggregate signal — emitted whenever any AI setting changes. Lets consumers
    // (e.g. AIManager) refresh all providers without subscribing to each signal.
    void configurationChanged();

private:
    mutable QSettings m_settings;
};
