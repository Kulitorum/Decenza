#pragma once

#include "core/settings.h"

#include <QVariantMap>

// The settings portion of the remotely published AI snapshot. Keep this
// projection small and explicit: credential values are represented only by
// presence flags, never by their stored strings.
namespace AISettingsSnapshot {

inline QVariantMap fromSettings(const Settings* settings)
{
    QVariantMap result;
    if (!settings)
        return result;

    const auto* ai = settings->ai();
    const auto* mcp = settings->mcp();
    const auto* network = settings->network();
    result.insert(QStringLiteral("provider"), ai->aiProvider());
    result.insert(QStringLiteral("hasOpenaiApiKey"), !ai->openaiApiKey().isEmpty());
    result.insert(QStringLiteral("hasAnthropicApiKey"), !ai->anthropicApiKey().isEmpty());
    result.insert(QStringLiteral("hasGeminiApiKey"), !ai->geminiApiKey().isEmpty());
    result.insert(QStringLiteral("hasOpenrouterApiKey"), !ai->openrouterApiKey().isEmpty());
    result.insert(QStringLiteral("ollamaEndpoint"), ai->ollamaEndpoint());
    result.insert(QStringLiteral("ollamaModel"), ai->ollamaModel());
    result.insert(QStringLiteral("openaiEndpoint"), ai->openaiEndpoint());
    result.insert(QStringLiteral("anthropicEndpoint"), ai->anthropicEndpoint());
    result.insert(QStringLiteral("tasteIntakeOnAsk"), ai->tasteIntakeOnAsk());
    result.insert(QStringLiteral("hasMcpApiKey"), !mcp->mcpApiKey().isEmpty());
    result.insert(QStringLiteral("mcpEnabled"), mcp->mcpEnabled());
    result.insert(QStringLiteral("mcpAccessLevel"), mcp->mcpAccessLevel());
    result.insert(QStringLiteral("mcpConfirmationLevel"), mcp->mcpConfirmationLevel());
    result.insert(QStringLiteral("remoteMcpEnabled"), mcp->remoteMcpEnabled());
    result.insert(QStringLiteral("remoteMcpMode"), mcp->remoteMcpMode());
    result.insert(QStringLiteral("remoteMcpPort"), mcp->remoteMcpPort());
    result.insert(QStringLiteral("remoteMcpCustomBaseUrl"), mcp->remoteMcpCustomBaseUrl());
    result.insert(QStringLiteral("hasClaudeRcSessionUrl"), !network->claudeRcSessionUrl().isEmpty());
    result.insert(QStringLiteral("discussShotApp"), network->discussShotApp());
    result.insert(QStringLiteral("discussShotCustomUrl"), network->discussShotCustomUrl());
    result.insert(QStringLiteral("discussAppNone"), network->discussAppNone());
    result.insert(QStringLiteral("discussAppClaudeDesktop"), network->discussAppClaudeDesktop());
    result.insert(QStringLiteral("shotServerPort"), network->shotServerPort());
    return result;
}

} // namespace AISettingsSnapshot
