#include "layoutsnapshot.h"

#include "layoutcatalog.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <utility>

LayoutSnapshot::LayoutSnapshot(QJsonObject layout)
    : m_layout(std::move(layout))
{
}

LayoutSnapshot::LayoutSnapshot(const QString& compactJson)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(compactJson.toUtf8(), &error);
    if (error.error == QJsonParseError::NoError && document.isObject())
        m_layout = document.object();
}

QVariantList LayoutSnapshot::zoneItems(const QString& zoneName) const
{
    QVariantList result;
    const auto items = m_layout.value(QStringLiteral("zones")).toObject()
                           .value(zoneName).toArray();
    result.reserve(items.size());
    for (const auto& value : items)
        result.append(value.toObject().toVariantMap());
    return result;
}

int LayoutSnapshot::zoneYOffset(const QString& zoneName) const
{
    const int defaultOffset = zoneName == QLatin1String("centerStatus") ? -65 : 0;
    return m_layout.value(QStringLiteral("offsets")).toObject()
        .value(zoneName).toInt(defaultOffset);
}

double LayoutSnapshot::zoneScale(const QString& zoneName) const
{
    return m_layout.value(QStringLiteral("scales")).toObject()
        .value(zoneName).toDouble(1.0);
}

QString LayoutSnapshot::zoneOption(const QString& zoneName, const QString& key,
                                  const QString& defaultValue) const
{
    const auto options = m_layout.value(QStringLiteral("zoneOptions")).toObject()
                             .value(zoneName).toObject();
    return options.contains(key) ? options.value(key).toString() : defaultValue;
}

QVariantMap LayoutSnapshot::itemProperties(const QString& itemId) const
{
    if (itemId.isEmpty())
        return {};
    const auto zones = m_layout.value(QStringLiteral("zones")).toObject();
    for (const auto& zoneValue : zones) {
        for (const auto& itemValue : zoneValue.toArray()) {
            const auto item = itemValue.toObject();
            if (item.value(QStringLiteral("id")).toString() == itemId)
                return item.toVariantMap();
        }
    }
    return {};
}

bool LayoutSnapshot::itemIsConfigured(const QString& itemId) const
{
    const auto props = itemProperties(itemId);
    if (props.isEmpty())
        return false;
    if (LayoutCatalog::typeHasOptions(props.value(QStringLiteral("type")).toString()))
        return true;
    for (auto it = props.cbegin(); it != props.cend(); ++it) {
        if (it.key() != QLatin1String("type") && it.key() != QLatin1String("id"))
            return true;
    }
    return false;
}
