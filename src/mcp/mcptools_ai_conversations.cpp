// ai_conversations_list / ai_conversation_get MCP tools. Lives in its own
// translation unit (not mcptools_ai.cpp) so tests can link it against a
// real AIManager without dragging in MainController/ShotHistoryStorage/
// BeanBaseClient — the dependencies ai_advisor_invoke and bag_extract_details
// pull in. Same rationale as mcptools_beansearch.cpp.
#include "mcptoolregistry.h"
#include "core/appsettings.h"
#include "../ai/aimanager.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QString>

// ai_conversations_list — read tier. Enumerates the persisted multi-shot
// AI dialing conversations (AIManager::MAX_CONVERSATIONS = 5, oldest
// evicted first), most recently active first. Mirrors the web UI's
// /ai-conversations page (ShotServer::generateAIConversationsPage) so
// MCP clients can discover and export the same conversations — e.g. to
// collect real transcripts for prompt-quality work (#639).
//
// Reads the "ai/conversations/<key>/*" QSettings keys directly rather
// than adding another AIConversation static helper — matching the raw
// read already duplicated in shotserver_ai.cpp. If that key format
// ever changes, grep for "ai/conversations/" across the codebase.
//
// ai_conversation_get — read tier. Returns the full transcript for one
// conversation key from ai_conversations_list: system prompt + every
// user/assistant turn (with shotId/structuredNext when present). Same
// underlying QSettings data as ShotServer::handleAIConversationDownload
// (the web UI's JSON export), returned as structured JSON instead of a
// file download.
void registerAIConversationTools(McpToolRegistry* registry, AIManager* aiManager)
{
    registry->registerTool(
        "ai_conversations_list",
        "List saved multi-shot AI dialing conversations, most recently active first. "
        "Each entry is a bean+profile conversation thread the in-app Dialing Assistant "
        "keeps continuity across shots for — up to 5 are retained, oldest evicted first. "
        "An entry carries `corrupted: true` (omitted otherwise) when its stored transcript "
        "failed to parse — `messageCount` is unreliable for that entry; fetch it via "
        "ai_conversation_get to see the actual parse error. "
        "Pass the returned `key` to ai_conversation_get to fetch the full transcript.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [aiManager](const QJsonObject&) -> QJsonObject {
            if (!aiManager) return QJsonObject{{"error", "AI advisor not available"}};

            AppSettings settings;
            QJsonArray conversations;
            for (const auto& entry : aiManager->conversationIndex()) {
                const QString prefix = "ai/conversations/" + entry.key + "/";
                const QByteArray messagesJson = settings.value(prefix + "messages").toByteArray();
                int msgCount = 0;
                bool corrupted = false;
                if (!messagesJson.isEmpty()) {
                    QJsonParseError parseError;
                    const QJsonDocument doc = QJsonDocument::fromJson(messagesJson, &parseError);
                    if (parseError.error != QJsonParseError::NoError || !doc.isArray())
                        corrupted = true;
                    else
                        msgCount = static_cast<int>(doc.array().size());
                }

                QStringList labelParts;
                if (!entry.beanBrand.isEmpty()) labelParts << entry.beanBrand;
                if (!entry.beanType.isEmpty()) labelParts << entry.beanType;
                QString label = labelParts.isEmpty() ? QStringLiteral("Unknown beans") : labelParts.join(" ");
                if (!entry.profileName.isEmpty()) label += " / " + entry.profileName;

                QString lastUpdated;
                if (entry.timestamp > 0) {
                    const QDateTime dt = QDateTime::fromSecsSinceEpoch(entry.timestamp);
                    lastUpdated = dt.toOffsetFromUtc(dt.offsetFromUtc()).toString(Qt::ISODate);
                }

                QJsonObject convObj{
                    {"key", entry.key},
                    {"label", label},
                    {"beanBrand", entry.beanBrand},
                    {"beanType", entry.beanType},
                    {"profileName", entry.profileName},
                    {"messageCount", msgCount},
                    {"lastUpdated", lastUpdated}
                };
                if (corrupted) convObj["corrupted"] = true;
                conversations.append(convObj);
            }
            return QJsonObject{{"conversations", conversations}};
        },
        "read");

    registry->registerTool(
        "ai_conversation_get",
        "Get the full transcript of one saved AI dialing conversation: system prompt plus "
        "every user/assistant turn, in order. Assistant turns that made a concrete "
        "recommendation carry a `structuredNext` object; turns tied to a specific shot carry "
        "`shotId`. Use ai_conversations_list first to find the `key`.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"key", QJsonObject{{"type", "string"},
                    {"description", "Conversation key from ai_conversations_list."}}}
            }},
            {"required", QJsonArray{"key"}}
        },
        [aiManager](const QJsonObject& args) -> QJsonObject {
            if (!aiManager) return QJsonObject{{"error", "AI advisor not available"}};

            const QString key = args.value("key").toString().trimmed();
            if (key.isEmpty()) return QJsonObject{{"error", "key is required"}};

            AppSettings settings;
            const QString prefix = "ai/conversations/" + key + "/";
            const QByteArray messagesJson = settings.value(prefix + "messages").toByteArray();
            if (messagesJson.isEmpty())
                return QJsonObject{{"error", "Conversation not found: " + key}};

            QJsonParseError parseError;
            const QJsonDocument msgDoc = QJsonDocument::fromJson(messagesJson, &parseError);
            if (parseError.error != QJsonParseError::NoError || !msgDoc.isArray())
                return QJsonObject{{"error", "Corrupted conversation data for key " + key}};

            // Bean/profile identity only lives in the index — a key with no
            // matching index entry (evicted, or a legacy conversation predating
            // the index) leaves these blank, which is an honest "unknown"
            // rather than an error: the transcript itself is still valid.
            QString beanBrand, beanType, profileName;
            qint64 indexTimestampSecs = 0;
            for (const auto& entry : aiManager->conversationIndex()) {
                if (entry.key == key) {
                    beanBrand = entry.beanBrand;
                    beanType = entry.beanType;
                    profileName = entry.profileName;
                    indexTimestampSecs = entry.timestamp;
                    break;
                }
            }

            // Prefer the per-conversation stored timestamp (always written
            // alongside `messages` by saveToStorage/appendAssistantTurnForKey,
            // so it survives even when the key has no index entry); fall back
            // to the index's timestamp otherwise. Same fallback order as
            // ShotServer::generateAIConversationsPage.
            QString lastUpdated;
            const QDateTime storedDt = QDateTime::fromString(
                settings.value(prefix + "timestamp").toString(), Qt::ISODate);
            if (storedDt.isValid()) {
                lastUpdated = storedDt.toOffsetFromUtc(storedDt.offsetFromUtc()).toString(Qt::ISODate);
            } else if (indexTimestampSecs > 0) {
                const QDateTime dt = QDateTime::fromSecsSinceEpoch(indexTimestampSecs);
                lastUpdated = dt.toOffsetFromUtc(dt.offsetFromUtc()).toString(Qt::ISODate);
            }

            return QJsonObject{
                {"key", key},
                {"metadata", QJsonObject{
                    {"beanBrand", beanBrand},
                    {"beanType", beanType},
                    {"profileName", profileName},
                    {"lastUpdated", lastUpdated}
                }},
                {"systemPrompt", settings.value(prefix + "systemPrompt").toString()},
                {"messages", msgDoc.array()}
            };
        },
        "read");
}
