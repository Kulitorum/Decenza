#include "settings_mcp.h"

#include <QUuid>
#include <QRandomGenerator>

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

// --- Remote MCP connector ---

bool SettingsMcp::remoteMcpEnabled() const {
    return m_settings.value("mcp/remoteEnabled", false).toBool();
}

void SettingsMcp::setRemoteMcpEnabled(bool enabled) {
    if (remoteMcpEnabled() != enabled) {
        m_settings.setValue("mcp/remoteEnabled", enabled);
        emit remoteMcpEnabledChanged();
    }
}

QString SettingsMcp::remoteMcpMode() const {
    return m_settings.value("mcp/remoteMode", QString::fromLatin1(ModeCustom)).toString();
}

void SettingsMcp::setRemoteMcpMode(const QString& mode) {
    if (remoteMcpMode() != mode) {
        m_settings.setValue("mcp/remoteMode", mode);
        emit remoteMcpModeChanged();
    }
}

int SettingsMcp::remoteMcpPort() const {
    // 8888 = ShotServer, 8889 = UDP discovery; the remote listener defaults to
    // the next free port so it never collides with the main web surface.
    return m_settings.value("mcp/remotePort", 8890).toInt();
}

void SettingsMcp::setRemoteMcpPort(int port) {
    if (remoteMcpPort() != port) {
        m_settings.setValue("mcp/remotePort", port);
        emit remoteMcpPortChanged();
    }
}

QString SettingsMcp::remoteMcpCustomBaseUrl() const {
    return m_settings.value("mcp/remoteCustomBaseUrl", "").toString();
}

void SettingsMcp::setRemoteMcpCustomBaseUrl(const QString& url) {
    if (remoteMcpCustomBaseUrl() != url) {
        m_settings.setValue("mcp/remoteCustomBaseUrl", url);
        emit remoteMcpCustomBaseUrlChanged();
    }
}

QString SettingsMcp::remoteMcpToken() const {
    QString token = m_settings.value("mcp/remoteToken", "").toString();
    if (token.isEmpty()) {
        // Generate lazily so the token exists the first time the connector URL
        // is composed, without forcing a write on every install.
        token = generateToken();
        m_settings.setValue("mcp/remoteToken", token);
    }
    return token;
}

void SettingsMcp::rotateRemoteMcpToken() {
    m_settings.setValue("mcp/remoteToken", generateToken());
    emit remoteMcpTokenChanged();
}

QString SettingsMcp::generateToken() {
    // 128 bits of CSPRNG entropy, base64url without padding → a 22-char
    // unguessable path segment. QRandomGenerator::system() is the OS CSPRNG.
    quint32 words[4];
    QRandomGenerator::system()->fillRange(words);
    QByteArray raw(reinterpret_cast<const char*>(words), sizeof(words));
    return QString::fromLatin1(
        raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}
