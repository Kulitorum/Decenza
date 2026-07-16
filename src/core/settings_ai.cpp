#include "settings_ai.h"
#include "settings.h"

SettingsAI::SettingsAI(QObject* parent)
    : QObject(parent)
#ifdef DECENZA_TESTING
    , m_settings(Settings::testQSettingsPath(), QSettings::IniFormat)
#else
    , m_settings("DecentEspresso", "DE1Qt")
#endif
{
}

QString SettingsAI::aiProvider() const {
    return m_settings.value("ai/provider", "openai").toString();
}

void SettingsAI::setAiProvider(const QString& provider) {
    if (aiProvider() != provider) {
        m_settings.setValue("ai/provider", provider);
        emit aiProviderChanged();
        emit configurationChanged();
    }
}

QString SettingsAI::openaiApiKey() const {
    return m_settings.value("ai/openaiKey", "").toString();
}

void SettingsAI::setOpenaiApiKey(const QString& key) {
    if (openaiApiKey() != key) {
        m_settings.setValue("ai/openaiKey", key);
        emit openaiApiKeyChanged();
        emit configurationChanged();
    }
}

QString SettingsAI::anthropicApiKey() const {
    return m_settings.value("ai/anthropicKey", "").toString();
}

void SettingsAI::setAnthropicApiKey(const QString& key) {
    if (anthropicApiKey() != key) {
        m_settings.setValue("ai/anthropicKey", key);
        emit anthropicApiKeyChanged();
        emit configurationChanged();
    }
}

QString SettingsAI::geminiApiKey() const {
    return m_settings.value("ai/geminiKey", "").toString();
}

void SettingsAI::setGeminiApiKey(const QString& key) {
    if (geminiApiKey() != key) {
        m_settings.setValue("ai/geminiKey", key);
        emit geminiApiKeyChanged();
        emit configurationChanged();
    }
}

QString SettingsAI::ollamaEndpoint() const {
    return m_settings.value("ai/ollamaEndpoint", "").toString();
}

void SettingsAI::setOllamaEndpoint(const QString& endpoint) {
    if (ollamaEndpoint() != endpoint) {
        m_settings.setValue("ai/ollamaEndpoint", endpoint);
        emit ollamaEndpointChanged();
        emit configurationChanged();
    }
}

QString SettingsAI::ollamaModel() const {
    return m_settings.value("ai/ollamaModel", "").toString();
}

void SettingsAI::setOllamaModel(const QString& model) {
    if (ollamaModel() != model) {
        m_settings.setValue("ai/ollamaModel", model);
        emit ollamaModelChanged();
        emit configurationChanged();
    }
}

QString SettingsAI::openrouterApiKey() const {
    return m_settings.value("ai/openrouterKey", "").toString();
}

void SettingsAI::setOpenrouterApiKey(const QString& key) {
    if (openrouterApiKey() != key) {
        m_settings.setValue("ai/openrouterKey", key);
        emit openrouterApiKeyChanged();
        emit configurationChanged();
    }
}

QString SettingsAI::openrouterModel() const {
    return m_settings.value("ai/openrouterModel", "anthropic/claude-sonnet-4").toString();
}

void SettingsAI::setOpenrouterModel(const QString& model) {
    if (openrouterModel() != model) {
        m_settings.setValue("ai/openrouterModel", model);
        emit openrouterModelChanged();
        emit configurationChanged();
    }
}

bool SettingsAI::tasteIntakeOnAsk() const {
    return m_settings.value("ai/tasteIntakeOnAsk", true).toBool();
}

void SettingsAI::setTasteIntakeOnAsk(bool enabled) {
    if (tasteIntakeOnAsk() != enabled) {
        m_settings.setValue("ai/tasteIntakeOnAsk", enabled);
        emit tasteIntakeOnAskChanged();
    }
}

bool SettingsAI::tasteIntakeSeen(qint64 shotId) const {
    if (shotId <= 0) return false;
    return m_settings.value(QString("ai/tasteIntakeSeen/%1").arg(shotId), false).toBool();
}

void SettingsAI::markTasteIntakeSeen(qint64 shotId) {
    if (shotId <= 0) return;
    m_settings.setValue(QString("ai/tasteIntakeSeen/%1").arg(shotId), true);
}

QString SettingsAI::providerModel(const QString& providerId) const {
    if (providerId.isEmpty()) return QString();
    return m_settings.value("ai/model/" + providerId, "").toString();
}

void SettingsAI::setProviderModel(const QString& providerId, const QString& modelId) {
    if (providerId.isEmpty()) return;
    if (providerModel(providerId) != modelId) {
        m_settings.setValue("ai/model/" + providerId, modelId);
        emit providerModelChanged();
        emit configurationChanged();
    }
}
