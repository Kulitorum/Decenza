#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../version.h"

#include <QFile>
#include <QJsonObject>
#include <QString>
#include <QStringLiteral>

void registerAgentTools(McpToolRegistry* registry)
{
    // get_agent_file
    // Returns the current Decenza CLAUDE.md content and a version string tied to the Decenza app
    // version. Claude Code Remote Control sessions call this at session start to self-update the
    // CLAUDE.md in their working directory, so agent instructions evolve with app updates without
    // any manual user intervention.
    registry->registerTool(
        "get_agent_file",
        "Returns the current Decenza CLAUDE.md content and version. "
        "Claude Code Remote Control sessions should call this at session start: "
        "if the returned `version` is newer than the version header in the existing CLAUDE.md "
        "in the working directory, overwrite it with the returned `content` and reload.",
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
