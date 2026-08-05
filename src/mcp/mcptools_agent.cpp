#include "mcpserver.h"
#include "mcpagentdocs.h"
#include "mcptoolregistry.h"
#include "version.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringLiteral>

void registerAgentTools(McpToolRegistry* registry)
{
    // get_agent_file
    // Returns the current Decenza dialing-assistant system prompt and a version string tied to
    // the Decenza app version. Any MCP client (Claude Desktop, Claude mobile, Claude Code, etc.)
    // should call this at session start to load behavioral guidance for dialing assistance, so
    // agent instructions evolve with app updates without manual user intervention. Claude Code
    // Remote Control sessions additionally use the `version` to self-update a `CLAUDE.md` file in
    // the working directory; other clients can simply read and follow the returned `content`.
    registry->registerTool(
        "get_agent_file",
        "Returns the Decenza dialing-assistant system prompt and version. Any MCP client should "
        "call this at session start and follow the returned `content` for the rest of the "
        "session. Pass `topic` instead to read one tool's long-form documentation — argument "
        "semantics, output fields and gotchas that are too long to carry in tools/list. Call it "
        "with an unknown topic to list the available ones.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"topic", QJsonObject{{"type", "string"},
                    {"description", "Tool documentation topic (e.g. \"flow_calibration\"). Omit for the session prompt"}}}
            }}
        },
        [](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            result["version"] = QStringLiteral(VERSION_STRING);

            const QString topic = args.value("topic").toString().trimmed();
            if (!topic.isEmpty()) {
                // Topics are whatever is in the resource directory — never a list
                // in code, which is one more thing to forget to update. A name
                // with a path separator in it cannot escape the directory because
                // the listing is what validates it, not the string.
                const QStringList topics = McpAgentDocs::availableTopics();
                if (!topics.contains(topic)) {
                    result["error"] = QStringLiteral("Unknown topic \"%1\"").arg(topic);
                    result["availableTopics"] = QJsonArray::fromStringList(topics);
                    return result;
                }
                QFile doc(McpAgentDocs::topicPath(topic));
                if (!doc.open(QIODevice::ReadOnly)) {
                    result["error"] = QStringLiteral("Documentation for \"%1\" could not be read").arg(topic);
                    return result;
                }
                result["topic"] = topic;
                result["content"] = QString::fromUtf8(doc.readAll());
                return result;
            }

            QFile f(QStringLiteral(":/ai/claude_agent.md"));
            if (!f.open(QIODevice::ReadOnly)) {
                result["error"] = QStringLiteral("claude_agent.md resource not found");
                result["content"] = QString();
                return result;
            }

            QString content = QString::fromUtf8(f.readAll());
            content.replace(QStringLiteral("{{VERSION}}"), QStringLiteral(VERSION_STRING));
            result["content"] = content;
            result["availableTopics"] = QJsonArray::fromStringList(McpAgentDocs::availableTopics());
            return result;
        },
        "read", McpTierCore);
}
