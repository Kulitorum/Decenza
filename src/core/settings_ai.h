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
    Q_PROPERTY(QString openrouterApiKey READ openrouterApiKey WRITE setOpenrouterApiKey NOTIFY openrouterApiKeyChanged)
    Q_PROPERTY(QString openrouterModel READ openrouterModel WRITE setOpenrouterModel NOTIFY openrouterModelChanged)

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

    QString openrouterApiKey() const;
    void setOpenrouterApiKey(const QString& key);

    QString openrouterModel() const;
    void setOpenrouterModel(const QString& model);

signals:
    void aiProviderChanged();
    void openaiApiKeyChanged();
    void anthropicApiKeyChanged();
    void geminiApiKeyChanged();
    void ollamaEndpointChanged();
    void ollamaModelChanged();
    void openrouterApiKeyChanged();
    void openrouterModelChanged();

    // Aggregate signal — emitted whenever any AI setting changes. Lets consumers
    // (e.g. AIManager) refresh all providers without subscribing to each signal.
    void configurationChanged();

private:
    mutable QSettings m_settings;
};
