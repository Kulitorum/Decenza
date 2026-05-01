#pragma once

#include "mcptoolregistry.h"

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <functional>

using McpResourceReader = std::function<QJsonObject()>;
using McpAsyncResourceReader = std::function<void(std::function<void(QJsonObject)> respond)>;

struct McpResourceDefinition {
    QString uri;
    QString name;
    QString description;
    QString mimeType;
    McpResourceReader reader;           // sync reader (null for async)
    McpAsyncResourceReader asyncReader; // async reader (null for sync)
    bool isAsync = false;
};

class McpResourceRegistry : public QObject {
    Q_OBJECT
public:
    explicit McpResourceRegistry(QObject* parent = nullptr) : QObject(parent) {}

    void registerResource(const QString& uri, const QString& name,
                          const QString& description, const QString& mimeType,
                          McpResourceReader reader)
    {
        McpResourceDefinition res;
        res.uri = uri;
        res.name = name;
        res.description = description;
        res.mimeType = mimeType;
        res.reader = reader;
        m_resources[uri] = res;
    }

    void registerAsyncResource(const QString& uri, const QString& name,
                               const QString& description, const QString& mimeType,
                               McpAsyncResourceReader reader)
    {
        McpResourceDefinition res;
        res.uri = uri;
        res.name = name;
        res.description = description;
        res.mimeType = mimeType;
        res.asyncReader = reader;
        res.isAsync = true;
        m_resources[uri] = res;
    }

    // protocolVersion gates spec-versioned optional fields. Strict clients
    // reject resources/list responses containing fields from a newer spec
    // than was negotiated (matching the same hazard fixed for tools/list).
    QJsonArray listResources(const QString& protocolVersion) const
    {
        const bool emitTitle = protocolVersion >= QStringLiteral("2025-06-18");
        const bool emitIcons = protocolVersion >= QStringLiteral("2025-11-25");

        QJsonArray result;
        for (auto it = m_resources.constBegin(); it != m_resources.constEnd(); ++it) {
            const auto& res = it.value();
            QJsonObject resJson;
            resJson["uri"] = res.uri;
            resJson["name"] = res.name;
            if (emitTitle) {
                // MCP 2025-06-18: separate human-readable `title` from `name`.
                // Existing registrations already use a display-name-y string in
                // `name` (e.g. "Machine State"), so reuse it here.
                resJson["title"] = res.name;
            }
            resJson["description"] = res.description;
            resJson["mimeType"] = res.mimeType;

            if (emitIcons) {
                // MCP 2025-11-25: icons array — derived from URI prefix.
                QJsonArray icons = McpRegistryHelpers::iconsArrayFromQrc(
                    McpRegistryHelpers::iconQrcForResource(res.uri));
                if (!icons.isEmpty())
                    resJson["icons"] = icons;
            }

            result.append(resJson);
        }
        return result;
    }

    QJsonObject readResource(const QString& uri, QString& errorOut) const
    {
        auto it = m_resources.constFind(uri);
        if (it == m_resources.constEnd()) {
            errorOut = "Unknown resource: " + uri;
            return {};
        }
        if (it.value().isAsync) {
            errorOut = "Resource is async, use readAsyncResource(): " + uri;
            return {};
        }
        return it.value().reader();
    }

    // Returns true if the async reader was dispatched. By convention, each
    // reader must invoke respond() on the main thread via
    // QMetaObject::invokeMethod(qApp, ..., Qt::QueuedConnection).
    // The registry does not enforce this — it is the reader's responsibility.
    bool readAsyncResource(const QString& uri, QString& errorOut,
                           std::function<void(QJsonObject)> respond) const
    {
        auto it = m_resources.constFind(uri);
        if (it == m_resources.constEnd()) {
            errorOut = "Unknown resource: " + uri;
            return false;
        }
        if (!it.value().isAsync || !it.value().asyncReader) {
            errorOut = "Resource is not async: " + uri;
            return false;
        }
        it.value().asyncReader(std::move(respond));
        return true;
    }

    bool isAsyncResource(const QString& uri) const
    {
        auto it = m_resources.constFind(uri);
        return (it != m_resources.constEnd()) && it.value().isAsync;
    }

    bool hasResource(const QString& uri) const { return m_resources.contains(uri); }

private:
    QHash<QString, McpResourceDefinition> m_resources;
};
