#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <functional>

// Tool handler callback: takes arguments JSON, returns result JSON.
// For async tools, the handler should return a "pending" marker and emit
// the result later via McpServer's response mechanism.
using McpToolHandler = std::function<QJsonObject(const QJsonObject& arguments)>;

struct McpToolDefinition {
    QString name;
    QString description;
    QJsonObject inputSchema;    // JSON Schema for the tool's parameters
    McpToolHandler handler;
    QString category;           // "read", "control", or "settings"
};

class McpToolRegistry : public QObject {
    Q_OBJECT
public:
    explicit McpToolRegistry(QObject* parent = nullptr) : QObject(parent) {}

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

    // List tools filtered by access level
    // 0 = Monitor (read only), 1 = Control (read + control), 2 = Full (all)
    QJsonArray listTools(int accessLevel) const
    {
        QJsonArray result;
        for (auto it = m_tools.constBegin(); it != m_tools.constEnd(); ++it) {
            const auto& tool = it.value();
            if (categoryMinLevel(tool.category) > accessLevel) continue;

            QJsonObject toolJson;
            toolJson["name"] = tool.name;
            toolJson["description"] = tool.description;
            toolJson["inputSchema"] = tool.inputSchema;
            result.append(toolJson);
        }
        return result;
    }

    // Call a tool, checking access level
    QJsonObject callTool(const QString& name, const QJsonObject& arguments,
                         int accessLevel, QString& errorOut) const
    {
        auto it = m_tools.constFind(name);
        if (it == m_tools.constEnd()) {
            errorOut = "Unknown tool: " + name;
            return {};
        }
        const auto& tool = it.value();
        if (categoryMinLevel(tool.category) > accessLevel) {
            errorOut = "Access level insufficient";
            return {};
        }
        return tool.handler(arguments);
    }

    bool hasTool(const QString& name) const { return m_tools.contains(name); }

    // Returns the category of a tool ("read", "control", "settings") or empty string
    QString toolCategory(const QString& name) const
    {
        auto it = m_tools.constFind(name);
        return (it != m_tools.constEnd()) ? it.value().category : QString();
    }

private:
    static int categoryMinLevel(const QString& category)
    {
        if (category == "read") return 0;
        if (category == "control") return 1;
        if (category == "settings") return 2;
        return 3; // unknown category — deny
    }

    QHash<QString, McpToolDefinition> m_tools;
};
