#pragma once

#include "rep_aisettingsservice_source.h"

#include <QPointer>
#include <QStringList>

class AIManager;
class RemoteAccess;
class McpServer;
class Settings;
class ShotServer;

class NativeAISettingsSource final : public AISettingsStateSimpleSource
{
    Q_OBJECT
public:
    NativeAISettingsSource(Settings* settings, AIManager* manager,
                           RemoteAccess* remoteMcp, McpServer* mcpServer,
                           ShotServer* shotServer,
                           QObject* parent = nullptr);

    void refresh() override;
    void testConnection(QString requestId) override;
    void refreshOllamaModels(QString requestId) override;
    QVariantMap requestMcpUrls() override;
    QVariantMap rotateMcpToken() override;

private:
    bool validRequest(const QString& requestId, const QString& operation);
    void finish(const QString& requestId, const QString& operation, bool success,
                QVariantMap result = {}, QString reason = {});
    void finishOllamaRefreshWhenTerminal();
    QVariantMap captureSnapshot() const;

    QPointer<Settings> m_settings;
    QPointer<AIManager> m_manager;
    QPointer<RemoteAccess> m_remoteMcp;
    QPointer<McpServer> m_mcpServer;
    QPointer<ShotServer> m_shotServer;
    QString m_pendingTest;
    QString m_pendingOllamaRefresh;
    bool m_ollamaRefreshModelsArrived = false;
    bool m_ollamaRefreshResultArrived = false;
    bool m_ollamaRefreshSuccess = false;
    QString m_ollamaRefreshMessage;
    QStringList m_ollamaRefreshModels;
};
