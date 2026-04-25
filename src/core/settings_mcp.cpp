#include "settings_mcp.h"

#include <QUuid>

SettingsMcp::SettingsMcp(QObject* parent)
    : QObject(parent)
    , m_settings("DecentEspresso", "DE1Qt")
{
}

bool SettingsMcp::mcpEnabled() const {
    return m_settings.value("mcp/enabled", false).toBool();
}

void SettingsMcp::setMcpEnabled(bool enabled) {
    if (mcpEnabled() != enabled) {
        m_settings.setValue("mcp/enabled", enabled);
        emit mcpEnabledChanged();
    }
}

int SettingsMcp::mcpAccessLevel() const {
    return m_settings.value("mcp/accessLevel", 1).toInt();
}

void SettingsMcp::setMcpAccessLevel(int level) {
    if (mcpAccessLevel() != level) {
        m_settings.setValue("mcp/accessLevel", level);
        emit mcpAccessLevelChanged();
    }
}

int SettingsMcp::mcpConfirmationLevel() const {
    return m_settings.value("mcp/confirmationLevel", 1).toInt();
}

void SettingsMcp::setMcpConfirmationLevel(int level) {
    if (mcpConfirmationLevel() != level) {
        m_settings.setValue("mcp/confirmationLevel", level);
        emit mcpConfirmationLevelChanged();
    }
}

QString SettingsMcp::mcpApiKey() const {
    return m_settings.value("mcp/apiKey", "").toString();
}

void SettingsMcp::regenerateMcpApiKey() {
    m_settings.setValue("mcp/apiKey", QUuid::createUuid().toString(QUuid::WithoutBraces));
    emit mcpApiKeyChanged();
}
