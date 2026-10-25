#include "aisettingsservicesource.h"
#include "aisettingssnapshot.h"

#include "ai/aimanager.h"
#include "core/settings.h"
#include "core/settings_ai.h"
#include "core/settings_mcp.h"
#include "core/settings_network.h"
#include "network/remoteaccess.h"
#include "mcp/mcpserver.h"
#include "network/shotserver.h"

#include <QUuid>
#include <utility>

namespace {
QStringList boundedModelList(QStringList models)
{
    constexpr qsizetype MaxModels = 64;
    constexpr qsizetype MaxModelName = 256;
    if (models.size() > MaxModels)
        models = models.mid(0, MaxModels);
    for (QString& model : models) {
        if (model.size() > MaxModelName)
            model.truncate(MaxModelName);
    }
    return models;
}

QString boundedStatusText(QString text)
{
    constexpr qsizetype MaxStatusText = 2048;
    if (text.size() > MaxStatusText)
        text.truncate(MaxStatusText);
    return text;
}
}

NativeAISettingsSource::NativeAISettingsSource(Settings* settings, AIManager* manager,
                                               RemoteAccess* remoteMcp,
                                               McpServer* mcpServer, ShotServer* shotServer,
                                               QObject* parent)
    : AISettingsStateSimpleSource(parent)
    , m_settings(settings)
    , m_manager(manager)
    , m_remoteMcp(remoteMcp)
    , m_mcpServer(mcpServer)
    , m_shotServer(shotServer)
{
    if (m_settings) {
        auto* ai = m_settings->ai();
        connect(ai, &SettingsAI::configurationChanged, this, &NativeAISettingsSource::refresh);
        connect(ai, &SettingsAI::tasteIntakeOnAskChanged, this, &NativeAISettingsSource::refresh);
        auto* mcp = m_settings->mcp();
        connect(mcp, &SettingsMcp::mcpEnabledChanged, this, &NativeAISettingsSource::refresh);
        connect(mcp, &SettingsMcp::mcpAccessLevelChanged, this, &NativeAISettingsSource::refresh);
        connect(mcp, &SettingsMcp::mcpConfirmationLevelChanged, this, &NativeAISettingsSource::refresh);
        connect(mcp, &SettingsMcp::mcpApiKeyChanged, this, &NativeAISettingsSource::refresh);
        connect(mcp, &SettingsMcp::remoteMcpEnabledChanged, this, &NativeAISettingsSource::refresh);
        connect(mcp, &SettingsMcp::remoteMcpModeChanged, this, &NativeAISettingsSource::refresh);
        connect(mcp, &SettingsMcp::remoteMcpPortChanged, this, &NativeAISettingsSource::refresh);
        connect(mcp, &SettingsMcp::remoteMcpCustomBaseUrlChanged, this, &NativeAISettingsSource::refresh);
        auto* network = m_settings->network();
        connect(network, &SettingsNetwork::discussShotAppChanged, this, &NativeAISettingsSource::refresh);
        connect(network, &SettingsNetwork::discussShotCustomUrlChanged, this, &NativeAISettingsSource::refresh);
        connect(network, &SettingsNetwork::claudeRcSessionUrlChanged, this, &NativeAISettingsSource::refresh);
    }
    if (m_manager) {
        connect(m_manager, &AIManager::configurationChanged, this, &NativeAISettingsSource::refresh);
        connect(m_manager, &AIManager::providerChanged, this, &NativeAISettingsSource::refresh);
        connect(m_manager, &AIManager::modelSummaryChanged, this, &NativeAISettingsSource::refresh);
        connect(m_manager, &AIManager::conversationIndexChanged, this, &NativeAISettingsSource::refresh);
        connect(m_manager, &AIManager::ollamaModelsChanged, this, [this] {
            refresh();
            if (!m_pendingOllamaRefresh.isEmpty()) {
                m_ollamaRefreshModelsArrived = true;
                m_ollamaRefreshModels = m_manager ? boundedModelList(m_manager->ollamaModels()) : QStringList();
                finishOllamaRefreshWhenTerminal();
            }
        });
        connect(m_manager, &AIManager::testResultChanged, this, [this] {
            refresh();
            if (!m_pendingTest.isEmpty()) {
                const QString id = std::exchange(m_pendingTest, {});
                finish(id, QStringLiteral("testConnection"), m_manager && m_manager->lastTestSuccess(),
                       {{QStringLiteral("message"), boundedStatusText(m_manager ? m_manager->lastTestResult() : QString())}},
                       m_manager && m_manager->lastTestSuccess() ? QString() : QStringLiteral("providerError"));
            } else if (!m_pendingOllamaRefresh.isEmpty()) {
                m_ollamaRefreshResultArrived = true;
                m_ollamaRefreshSuccess = m_manager && m_manager->lastTestSuccess();
                m_ollamaRefreshMessage = boundedStatusText(m_manager ? m_manager->lastTestResult() : QString());
                finishOllamaRefreshWhenTerminal();
            }
        });
    }
    if (m_remoteMcp) {
        connect(m_remoteMcp, &RemoteAccess::statusChanged, this, &NativeAISettingsSource::refresh);
        connect(m_remoteMcp, &RemoteAccess::tailscaleSetupNeededChanged, this, &NativeAISettingsSource::refresh);
        connect(m_remoteMcp, &RemoteAccess::urlRevisionChanged, this, &NativeAISettingsSource::refresh);
    }
    if (m_mcpServer)
        connect(m_mcpServer, &McpServer::activeSessionCountChanged, this, &NativeAISettingsSource::refresh);
    if (m_shotServer) {
        connect(m_shotServer, &ShotServer::runningChanged, this, &NativeAISettingsSource::refresh);
        connect(m_shotServer, &ShotServer::urlChanged, this, &NativeAISettingsSource::refresh);
    }
    refresh();
}

void NativeAISettingsSource::refresh()
{
    setSnapshot(captureSnapshot());
    setReady(bool(m_settings));
}

QVariantMap NativeAISettingsSource::captureSnapshot() const
{
    QVariantMap result = AISettingsSnapshot::fromSettings(m_settings);
    if (m_manager) {
        result.insert(QStringLiteral("availableProviders"), m_manager->availableProviders());
        result.insert(QStringLiteral("configured"), m_manager->isConfigured());
        result.insert(QStringLiteral("currentModelName"), m_manager->currentModelName());
        result.insert(QStringLiteral("modelDisplayNames"), m_manager->modelDisplayNames());
        result.insert(QStringLiteral("selectedCostHint"), m_manager->selectedCostHint());
        result.insert(QStringLiteral("ollamaModels"), boundedModelList(m_manager->ollamaModels()));
        result.insert(QStringLiteral("hasAnyConversation"), m_manager->hasAnyConversation());
        for (const QString& provider : m_manager->availableProviders()) {
            result.insert(QStringLiteral("models.") + provider, m_manager->availableModels(provider));
            result.insert(QStringLiteral("modelHint.") + provider, m_manager->modelHint(provider));
            result.insert(QStringLiteral("modelHintKey.") + provider, m_manager->modelHintKey(provider));
            result.insert(QStringLiteral("costHint.") + provider, m_manager->costHint(provider));
        }
        result.insert(QStringLiteral("testResultMessage"), boundedStatusText(m_manager->lastTestResult()));
        result.insert(QStringLiteral("testResultSuccess"), m_manager->lastTestSuccess());
    }
    if (m_mcpServer)
        result.insert(QStringLiteral("mcpSessionCount"), m_mcpServer->activeSessionCount());
    if (m_shotServer)
        result.insert(QStringLiteral("shotServerUrl"), m_shotServer->isRunning() ? m_shotServer->url() : QString());
    if (m_remoteMcp) {
        result.insert(QStringLiteral("remoteMcpStatus"), m_remoteMcp->statusString());
        result.insert(QStringLiteral("remoteMcpStatusDetail"), boundedStatusText(m_remoteMcp->statusDetail()));
        result.insert(QStringLiteral("remoteMcpListenPort"), m_remoteMcp->listenPort());
        result.insert(QStringLiteral("remoteMcpTunnelAvailable"), m_remoteMcp->tunnelAvailable());
        result.insert(QStringLiteral("remoteMcpSetupNeeded"), m_remoteMcp->tailscaleSetupNeeded());
        result.insert(QStringLiteral("remoteUrlRevision"), m_remoteMcp->urlRevision());
        // connectorUrl carries access material and must never be
        // broadcast as ordinary replica properties.
    }
    return result;
}

bool NativeAISettingsSource::validRequest(const QString& requestId, const QString& operation)
{
    const QUuid id(requestId);
    if (id.isNull() || id.toString(QUuid::WithoutBraces).toLower() != requestId.toLower()) {
        emit operationCompleted(requestId, operation, false, {}, QStringLiteral("invalidRequest"));
        return false;
    }
    return true;
}

void NativeAISettingsSource::finish(const QString& requestId, const QString& operation,
                                    bool success, QVariantMap result, QString reason)
{
    emit operationCompleted(requestId, operation, success, result, reason);
}

void NativeAISettingsSource::finishOllamaRefreshWhenTerminal()
{
    if (m_pendingOllamaRefresh.isEmpty() || !m_ollamaRefreshModelsArrived
        || !m_ollamaRefreshResultArrived)
        return;
    const QString id = std::exchange(m_pendingOllamaRefresh, {});
    const bool success = m_ollamaRefreshSuccess;
    const QVariantMap result{{QStringLiteral("models"), m_ollamaRefreshModels},
                             {QStringLiteral("message"), m_ollamaRefreshMessage}};
    m_ollamaRefreshModelsArrived = false;
    m_ollamaRefreshResultArrived = false;
    m_ollamaRefreshModels.clear();
    m_ollamaRefreshMessage.clear();
    finish(id, QStringLiteral("refreshOllamaModels"), success, result,
           success ? QString() : QStringLiteral("providerError"));
}

void NativeAISettingsSource::testConnection(QString requestId)
{
    const QString operation = QStringLiteral("testConnection");
    if (!validRequest(requestId, operation)) return;
    if (!m_manager) { finish(requestId, operation, false, {}, QStringLiteral("unavailable")); return; }
    if (!m_pendingTest.isEmpty() || !m_pendingOllamaRefresh.isEmpty()) { finish(requestId, operation, false, {}, QStringLiteral("busy")); return; }
    m_pendingTest = requestId;
    m_manager->testConnection();
}

void NativeAISettingsSource::refreshOllamaModels(QString requestId)
{
    const QString operation = QStringLiteral("refreshOllamaModels");
    if (!validRequest(requestId, operation)) return;
    if (!m_manager || !m_settings) { finish(requestId, operation, false, {}, QStringLiteral("unavailable")); return; }
    if (m_settings->ai()->aiProvider() != QLatin1String("ollama")) {
        finish(requestId, operation, false, {}, QStringLiteral("wrongProvider")); return;
    }
    if (!m_pendingOllamaRefresh.isEmpty() || !m_pendingTest.isEmpty()) { finish(requestId, operation, false, {}, QStringLiteral("busy")); return; }
    m_pendingOllamaRefresh = requestId;
    m_ollamaRefreshModelsArrived = false;
    m_ollamaRefreshResultArrived = false;
    m_ollamaRefreshSuccess = false;
    m_ollamaRefreshMessage.clear();
    m_ollamaRefreshModels.clear();
    m_manager->refreshOllamaModels();
}

QVariantMap NativeAISettingsSource::requestMcpUrls()
{
    if (!m_remoteMcp)
        return {{QStringLiteral("success"), false}, {QStringLiteral("reason"), QStringLiteral("unavailable")}};
    return {{QStringLiteral("success"), true},
            {QStringLiteral("result"), QVariantMap{
                {QStringLiteral("connectorUrl"), m_remoteMcp->connectorUrl()}}}};
}

QVariantMap NativeAISettingsSource::rotateMcpToken()
{
    if (!m_remoteMcp)
        return {{QStringLiteral("success"), false}, {QStringLiteral("reason"), QStringLiteral("unavailable")}};
    m_remoteMcp->rotateToken();
    refresh();
    return {{QStringLiteral("success"), true},
            {QStringLiteral("result"), QVariantMap{
                {QStringLiteral("connectorUrl"), m_remoteMcp->connectorUrl()}}}};
}


