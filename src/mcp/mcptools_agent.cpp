#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "version.h"

#include <QFile>
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
        "Returns the Decenza dialing-assistant system prompt and version. "
        "Any MCP client should call this at session start to load behavioral guidance for "
        "dialing assistance — read the returned `content` and follow it for the rest of the "
        "session. Clients with filesystem access (e.g. Claude Code Remote Control) may "
        "additionally use `version` to self-update a local CLAUDE.md.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{}}
        },
        [](const QJsonObject& /*args*/) -> QJsonObject {
            QJsonObject result;
            result["version"] = QStringLiteral(VERSION_STRING);

            QFile f(QStringLiteral(":/ai/claude_agent.md"));
            if (!f.open(QIODevice::ReadOnly)) {
                result["error"] = QStringLiteral("claude_agent.md resource not found");
                result["content"] = QString();
                return result;
            }

            QString content = QString::fromUtf8(f.readAll());
            content.replace(QStringLiteral("{{VERSION}}"), QStringLiteral(VERSION_STRING));
            result["content"] = content;
            return result;
        },
        "read");
}
