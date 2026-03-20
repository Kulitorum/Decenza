#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <functional>

using McpResourceReader = std::function<QJsonObject()>;

struct McpResourceDefinition {
    QString uri;
    QString name;
    QString description;
    QString mimeType;
    McpResourceReader reader;
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

    QJsonArray listResources() const
    {
        QJsonArray result;
        for (auto it = m_resources.constBegin(); it != m_resources.constEnd(); ++it) {
            const auto& res = it.value();
            QJsonObject resJson;
            resJson["uri"] = res.uri;
            resJson["name"] = res.name;
            resJson["description"] = res.description;
            resJson["mimeType"] = res.mimeType;
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
        return it.value().reader();
    }

    bool hasResource(const QString& uri) const { return m_resources.contains(uri); }

private:
    QHash<QString, McpResourceDefinition> m_resources;
};
