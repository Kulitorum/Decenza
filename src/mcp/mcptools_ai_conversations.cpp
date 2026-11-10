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

// List persisted conversations and return complete transcripts from QSettings.
void registerAIConversationTools(McpToolRegistry* registry, AIManager* aiManager)
{
    const QVector<McpToolAction> conversationActions{
        McpRegistryHelpers::syncAction("list", "read",
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

                const QString label = entry.label().isEmpty()
                    ? QStringLiteral("Unknown beans") : entry.label();

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
                    {"equipment", entry.equipmentLabel},
                    {"messageCount", msgCount},
                    {"lastUpdated", lastUpdated}
                };
                if (corrupted) convObj["corrupted"] = true;
                conversations.append(convObj);
            }
            return QJsonObject{{"conversations", conversations}};
        }),
        McpRegistryHelpers::syncAction("get", "read",
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

            // Identity only lives in the index — a key with no matching entry
            // (evicted, or a legacy conversation predating the index) leaves
            // these blank, which is an honest "unknown" rather than an error:
            // the transcript itself is still valid.
            const auto entry = aiManager->conversationEntry(key);
            const qint64 indexTimestampSecs = entry.timestamp;

            // Prefer the per-conversation stored timestamp (always written
            // alongside `messages` by saveToStorage/appendAssistantTurnForKey,
            // so it survives even when the key has no index entry); fall back
            // to the index's timestamp otherwise.
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
                    {"beanBrand", entry.beanBrand},
                    {"beanType", entry.beanType},
                    {"profileName", entry.profileName},
                    {"equipment", entry.equipmentLabel},
                    {"lastUpdated", lastUpdated}
                }},
                {"systemPrompt", settings.value(prefix + "systemPrompt").toString()},
                {"messages", msgDoc.array()}
            };
        }),
    };

    registry->registerActionTool(
        "ai_conversations",
        "Saved multi-shot AI dialing conversations. action=list returns the retained threads "
        "(up to 5, most recently active first, each with a `key`); action=get returns one "
        "thread's full transcript — system prompt plus every user/assistant turn, in order. "
        "An entry marked `corrupted` failed to parse; fetch it with action=get for the error.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"key", QJsonObject{{"type", "string"},
                    {"description", "get only: conversation key from action=list"}}}
            }}
        },
        conversationActions,
        McpTierNiche);
}
