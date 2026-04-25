#pragma once

#include <QObject>
#include <QSettings>
#include <QString>

// MCP server settings: enable, access level, confirmation level, API key.
class SettingsMcp : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool mcpEnabled READ mcpEnabled WRITE setMcpEnabled NOTIFY mcpEnabledChanged)
    Q_PROPERTY(int mcpAccessLevel READ mcpAccessLevel WRITE setMcpAccessLevel NOTIFY mcpAccessLevelChanged)
    Q_PROPERTY(int mcpConfirmationLevel READ mcpConfirmationLevel WRITE setMcpConfirmationLevel NOTIFY mcpConfirmationLevelChanged)
    Q_PROPERTY(QString mcpApiKey READ mcpApiKey NOTIFY mcpApiKeyChanged)

public:
    explicit SettingsMcp(QObject* parent = nullptr);

    bool mcpEnabled() const;
    void setMcpEnabled(bool enabled);

    int mcpAccessLevel() const;
    void setMcpAccessLevel(int level);

    int mcpConfirmationLevel() const;
    void setMcpConfirmationLevel(int level);

    QString mcpApiKey() const;
    Q_INVOKABLE void regenerateMcpApiKey();

signals:
    void mcpEnabledChanged();
    void mcpAccessLevelChanged();
    void mcpConfirmationLevelChanged();
    void mcpApiKeyChanged();

private:
    mutable QSettings m_settings;
};
