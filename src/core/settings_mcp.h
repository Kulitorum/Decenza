#pragma once

#include <QObject>
#include <QSettings>
#include <QString>

// MCP server settings: enable, access level, confirmation level, API key.
//
// Remote-connector settings live here too (not in settings_network): they sit
// next to mcpAccessLevel/mcpConfirmationLevel — which remote sessions reuse
// unchanged — and QML already addresses everything MCP as `Settings.mcp.*`.
class SettingsMcp : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool mcpEnabled READ mcpEnabled WRITE setMcpEnabled NOTIFY mcpEnabledChanged)
    Q_PROPERTY(int mcpAccessLevel READ mcpAccessLevel WRITE setMcpAccessLevel NOTIFY mcpAccessLevelChanged)
    Q_PROPERTY(int mcpConfirmationLevel READ mcpConfirmationLevel WRITE setMcpConfirmationLevel NOTIFY mcpConfirmationLevelChanged)
    Q_PROPERTY(QString mcpApiKey READ mcpApiKey NOTIFY mcpApiKeyChanged)

    // Remote MCP connector (public-internet reachability for Claude/ChatGPT
    // mobile connectors). See docs/CLAUDE_MD/MCP_SERVER.md and the
    // add-remote-mcp-connector change. Defaults off; opt-in.
    Q_PROPERTY(bool remoteMcpEnabled READ remoteMcpEnabled WRITE setRemoteMcpEnabled NOTIFY remoteMcpEnabledChanged)
    Q_PROPERTY(QString remoteMcpMode READ remoteMcpMode WRITE setRemoteMcpMode NOTIFY remoteMcpModeChanged)
    Q_PROPERTY(int remoteMcpPort READ remoteMcpPort WRITE setRemoteMcpPort NOTIFY remoteMcpPortChanged)
    Q_PROPERTY(QString remoteMcpCustomBaseUrl READ remoteMcpCustomBaseUrl WRITE setRemoteMcpCustomBaseUrl NOTIFY remoteMcpCustomBaseUrlChanged)
    // The capability token appears only in the composed connector URL; the raw
    // value is not exposed as a QML-readable property to keep it out of casual
    // logs/bindings. Rotation is the revocation story.
    Q_PROPERTY(QString remoteMcpToken READ remoteMcpToken NOTIFY remoteMcpTokenChanged)

public:
    explicit SettingsMcp(QObject* parent = nullptr);

    // Remote MCP mode identifiers (stored as strings for forward-compatibility
    // and human-readable QSettings).
    static constexpr const char* ModeCustom = "custom";
    static constexpr const char* ModeTailscale = "tailscale";
    static constexpr const char* ModeNgrok = "ngrok";

    bool mcpEnabled() const;
    void setMcpEnabled(bool enabled);

    int mcpAccessLevel() const;
    void setMcpAccessLevel(int level);

    int mcpConfirmationLevel() const;
    void setMcpConfirmationLevel(int level);

    QString mcpApiKey() const;
    Q_INVOKABLE void regenerateMcpApiKey();

    // --- Remote MCP connector ---
    bool remoteMcpEnabled() const;
    void setRemoteMcpEnabled(bool enabled);

    QString remoteMcpMode() const;
    void setRemoteMcpMode(const QString& mode);

    int remoteMcpPort() const;
    void setRemoteMcpPort(int port);

    QString remoteMcpCustomBaseUrl() const;
    void setRemoteMcpCustomBaseUrl(const QString& url);

    // Capability token: 128-bit CSPRNG, base64url, generated lazily on first
    // read when absent. rotateRemoteMcpToken() replaces it (old URL dies).
    QString remoteMcpToken() const;
    Q_INVOKABLE void rotateRemoteMcpToken();

private:
    // Generate a fresh 128-bit base64url token (no padding).
    static QString generateToken();

    mutable QSettings m_settings;

signals:
    void mcpEnabledChanged();
    void mcpAccessLevelChanged();
    void mcpConfirmationLevelChanged();
    void mcpApiKeyChanged();
    void remoteMcpEnabledChanged();
    void remoteMcpModeChanged();
    void remoteMcpPortChanged();
    void remoteMcpCustomBaseUrlChanged();
    void remoteMcpTokenChanged();
};
