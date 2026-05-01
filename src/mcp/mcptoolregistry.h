#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QFile>
#include <QByteArray>
#include <functional>

// Synchronous tool handler: takes arguments, returns result immediately.
using McpToolHandler = std::function<QJsonObject(const QJsonObject& arguments)>;

// Async tool handler: takes arguments and a respond callback.
// The handler runs the work on a background thread and calls respond(result)
// on the main thread when done. The respond callback sends the HTTP response.
using McpAsyncToolHandler = std::function<void(const QJsonObject& arguments,
                                               std::function<void(QJsonObject)> respond)>;

struct McpToolDefinition {
    QString name;
    QString description;
    QJsonObject inputSchema;    // JSON Schema for the tool's parameters
    McpToolHandler handler;     // sync handler (null for async tools)
    McpAsyncToolHandler asyncHandler; // async handler (null for sync tools)
    QString category;           // "read", "control", or "settings"
    bool isAsync = false;
};

namespace McpRegistryHelpers {
    // snake_case → "Snake Case" for an auto-derived `title` per MCP 2025-06-18.
    inline QString deriveTitle(const QString& name) {
        QStringList parts = name.split(QLatin1Char('_'), Qt::SkipEmptyParts);
        for (QString& p : parts) {
            if (p.isEmpty()) continue;
            p[0] = p[0].toUpper();
        }
        return parts.join(QLatin1Char(' '));
    }

    // Read an SVG from qrc and encode as a data: URI suitable for the
    // MCP `icons[].src` field (2025-11-25). Returns an empty string on miss.
    inline QString iconDataUri(const QString& qrcPath) {
        QFile f(qrcPath);
        if (!f.open(QIODevice::ReadOnly)) return QString();
        const QByteArray svg = f.readAll();
        return QStringLiteral("data:image/svg+xml;base64,")
            + QString::fromLatin1(svg.toBase64());
    }

    // Map a tool name's leading namespace (`scale_*`, `machine_*`, `shots_*`,
    // `profiles_*`, etc.) to a qrc icon path. Falls back to a generic asset.
    inline QString iconQrcForTool(const QString& name) {
        const QString prefix = name.section(QLatin1Char('_'), 0, 0);
        static const QHash<QString, QString> map = {
            {"machine",  ":/icons/decent-de1.svg"},
            {"shots",    ":/icons/Graph.svg"},
            {"profiles", ":/icons/coffeebeans.svg"},
            {"settings", ":/icons/settings.svg"},
            {"scale",    ":/icons/scale.svg"},
            {"steam",    ":/icons/steam.svg"},
            {"devices",  ":/icons/bluetooth.svg"},
            {"agent",    ":/icons/sparkle.svg"},
            {"dialing",  ":/icons/grind.svg"},
            {"debug",    ":/icons/list.svg"},
        };
        auto it = map.constFind(prefix);
        return it != map.cend() ? *it : QStringLiteral(":/icons/decent-de1.svg");
    }

    // Map a resource URI scheme path to a qrc icon path.
    inline QString iconQrcForResource(const QString& uri) {
        if (uri.startsWith(QStringLiteral("decenza://machine"))) return ":/icons/decent-de1.svg";
        if (uri.startsWith(QStringLiteral("decenza://shots")))   return ":/icons/Graph.svg";
        if (uri.startsWith(QStringLiteral("decenza://profiles"))) return ":/icons/coffeebeans.svg";
        if (uri.startsWith(QStringLiteral("decenza://dialing"))) return ":/icons/grind.svg";
        if (uri.startsWith(QStringLiteral("decenza://debug")))   return ":/icons/list.svg";
        return ":/icons/decent-de1.svg";
    }

    inline QJsonArray iconsArrayFromQrc(const QString& qrcPath) {
        const QString uri = iconDataUri(qrcPath);
        if (uri.isEmpty()) return {};
        QJsonObject icon;
        icon["src"] = uri;
        icon["mimeType"] = "image/svg+xml";
        icon["sizes"] = QJsonArray{ QStringLiteral("any") };  // SVG scales freely; MCP schema requires string[]
        return QJsonArray{ icon };
    }

    // Stamp a tool/resource input schema with the JSON Schema 2020-12 dialect
    // declaration (2025-11-25). No-op if the schema already declares `$schema`.
    inline QJsonObject withJsonSchemaDialect(QJsonObject schema) {
        if (!schema.contains(QStringLiteral("$schema")))
            schema[QStringLiteral("$schema")] = QStringLiteral("https://json-schema.org/draft/2020-12/schema");
        return schema;
    }
}

class McpToolRegistry : public QObject {
    Q_OBJECT
public:
    explicit McpToolRegistry(QObject* parent = nullptr) : QObject(parent) {}

    // Tool input schemas are stored in their registered form. The 2025-11-25
    // `$schema` dialect declaration is stamped per-request in listTools() so
    // it can be gated on the negotiated protocol version.
    void registerTool(const QString& name, const QString& description,
                      const QJsonObject& inputSchema, McpToolHandler handler,
                      const QString& category)
    {
        McpToolDefinition tool;
        tool.name = name;
        tool.description = description;
        tool.inputSchema = inputSchema;
        tool.handler = handler;
        tool.category = category;
        m_tools[name] = tool;
    }

    void registerAsyncTool(const QString& name, const QString& description,
                           const QJsonObject& inputSchema, McpAsyncToolHandler handler,
                           const QString& category)
    {
        McpToolDefinition tool;
        tool.name = name;
        tool.description = description;
        tool.inputSchema = inputSchema;
        tool.asyncHandler = handler;
        tool.isAsync = true;
        tool.category = category;
        m_tools[name] = tool;
    }

    // List all tools. Tools above the current access level are still listed
    // (so the AI knows they exist) but their descriptions note the required level.
    // Access is enforced in callTool — restricted tools return an error when called.
    // 0 = Monitor (read only), 1 = Control (read + control), 2 = Full (all)
    //
    // protocolVersion gates spec-versioned optional fields. Strict clients reject
    // tools/list responses containing fields from a newer spec than was negotiated,
    // which surfaces as the server connecting with zero tools.
    QJsonArray listTools(int accessLevel, const QString& protocolVersion) const
    {
        static const char* levelNames[] = {"Monitor", "Control", "Full"};
        const bool emitTitle = protocolVersion >= QStringLiteral("2025-06-18");
        const bool emitIcons = protocolVersion >= QStringLiteral("2025-11-25");
        const bool emitSchemaDialect = protocolVersion >= QStringLiteral("2025-11-25");

        QJsonArray result;
        for (auto it = m_tools.constBegin(); it != m_tools.constEnd(); ++it) {
            const auto& tool = it.value();
            int required = categoryMinLevel(tool.category);

            QJsonObject toolJson;
            toolJson["name"] = tool.name;
            if (emitTitle) {
                // MCP 2025-06-18: human-readable display name distinct from the
                // programmatic `name`. Auto-derived from snake_case.
                toolJson["title"] = McpRegistryHelpers::deriveTitle(tool.name);
            }
            if (required > accessLevel) {
                int reqClamped = qBound(0, required, 2);
                toolJson["description"] = QString("[DISABLED — requires '%1' access level in Settings > AI > MCP] ")
                    .arg(levelNames[reqClamped]) + tool.description;
            } else {
                toolJson["description"] = tool.description;
            }
            toolJson["inputSchema"] = emitSchemaDialect
                ? McpRegistryHelpers::withJsonSchemaDialect(tool.inputSchema)
                : tool.inputSchema;

            if (emitIcons) {
                // MCP 2025-11-25: optional icons for client UIs. Derived from the
                // tool's name prefix so each tool gets a category-appropriate SVG
                // without having to thread icons through every registration site.
                QJsonArray icons = McpRegistryHelpers::iconsArrayFromQrc(
                    McpRegistryHelpers::iconQrcForTool(tool.name));
                if (!icons.isEmpty())
                    toolJson["icons"] = icons;
            }

            result.append(toolJson);
        }
        return result;
    }

    // Call a tool, checking access level.
    // Arguments are normalized against the tool's input schema before dispatch —
    // MCP clients may send integers as strings (especially after the confirmation
    // round-trip where args are serialized to JSON text and re-parsed).
    QJsonObject callTool(const QString& name, const QJsonObject& arguments,
                         int accessLevel, QString& errorOut) const
    {
        auto it = m_tools.constFind(name);
        if (it == m_tools.constEnd()) {
            errorOut = "Unknown tool: " + name;
            return {};
        }
        const auto& tool = it.value();
        if (tool.isAsync || !tool.handler) {
            errorOut = "Tool is async, use callAsyncTool(): " + name;
            return {};
        }
        if (categoryMinLevel(tool.category) > accessLevel) {
            errorOut = "Access level insufficient";
            return {};
        }
        return tool.handler(normalizeArguments(arguments, tool.inputSchema));
    }

    // Call an async tool, checking access level. Returns true if dispatched.
    // By convention, each handler must invoke respond() on the main thread
    // via QMetaObject::invokeMethod(qApp, ..., Qt::QueuedConnection).
    // The registry does not enforce this — it is the handler's responsibility.
    bool callAsyncTool(const QString& name, const QJsonObject& arguments,
                       int accessLevel, QString& errorOut,
                       std::function<void(QJsonObject)> respond) const
    {
        auto it = m_tools.constFind(name);
        if (it == m_tools.constEnd()) {
            errorOut = "Unknown tool: " + name;
            return false;
        }
        const auto& tool = it.value();
        if (!tool.isAsync || !tool.asyncHandler) {
            errorOut = "Tool is not async: " + name;
            return false;
        }
        if (categoryMinLevel(tool.category) > accessLevel) {
            errorOut = "Access level insufficient";
            return false;
        }
        tool.asyncHandler(normalizeArguments(arguments, tool.inputSchema), std::move(respond));
        return true;
    }

    bool hasTool(const QString& name) const { return m_tools.contains(name); }

    bool isAsyncTool(const QString& name) const
    {
        auto it = m_tools.constFind(name);
        return (it != m_tools.constEnd()) && it.value().isAsync;
    }

    // Returns the category of a tool ("read", "control", "settings") or empty string
    QString toolCategory(const QString& name) const
    {
        auto it = m_tools.constFind(name);
        return (it != m_tools.constEnd()) ? it.value().category : QString();
    }

private:
    // Coerce string-typed values to the type declared in the tool's inputSchema.
    // MCP clients may send "123" instead of 123 after a confirmation round-trip.
    static QJsonObject normalizeArguments(const QJsonObject& args, const QJsonObject& schema)
    {
        QJsonObject properties = schema["properties"].toObject();
        if (properties.isEmpty()) return args;

        QJsonObject normalized = args;
        for (auto it = args.begin(); it != args.end(); ++it) {
            if (!it.value().isString()) continue;  // only coerce strings
            QJsonObject prop = properties[it.key()].toObject();
            QString type = prop["type"].toString();
            if (type == "integer") {
                bool ok;
                qint64 v = it.value().toString().toLongLong(&ok);
                if (ok) normalized[it.key()] = v;
            } else if (type == "number") {
                bool ok;
                double v = it.value().toString().toDouble(&ok);
                if (ok) normalized[it.key()] = v;
            } else if (type == "boolean") {
                QString s = it.value().toString().toLower();
                if (s == "true") normalized[it.key()] = true;
                else if (s == "false") normalized[it.key()] = false;
            }
        }
        return normalized;
    }

    static int categoryMinLevel(const QString& category)
    {
        if (category == "read") return 0;
        if (category == "control") return 1;
        if (category == "settings") return 2;
        return 3; // unknown category — deny
    }

    QHash<QString, McpToolDefinition> m_tools;
};
