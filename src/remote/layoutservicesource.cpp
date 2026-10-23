#include "layoutservicesource.h"

#include "core/layoutcatalog.h"
#include "core/layoutsnapshot.h"
#include "core/settings_network.h"
#include "servicevalue.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QSet>

namespace {
constexpr qint64 kMaximumSafeRevision = 9007199254740991LL;
constexpr qsizetype kMaximumConfigurationBytes = 256 * 1024;
constexpr qsizetype kMaximumOperationBytes = 64 * 1024;
constexpr qsizetype kMaximumItemsPerZone = 128;
bool exactKeys(const QVariantMap& fields, const QStringList& keys)
{
    if (fields.size() != keys.size()) return false;
    for (const auto& key : keys) if (!fields.contains(key)) return false;
    return true;
}

bool boundedString(const QVariant& value, QString* output, qsizetype maxLength = 128)
{
    if (value.metaType().id() != QMetaType::QString) return false;
    const QString text = value.toString();
    if (text.isEmpty() || text.size() > maxLength || text.contains(QChar::Null)) return false;
    if (output) *output = text;
    return true;
}

bool strictInteger(const QVariant& value, int minimum, int maximum, int* output)
{
    return ServiceValue::boundedInteger(value, minimum, maximum, output);
}

bool strictFiniteDouble(const QVariant& value, double minimum, double maximum, double* output)
{
    double number = 0.0;
    if (!ServiceValue::finiteNumber(value, &number) || number < minimum || number > maximum) return false;
    if (output) *output = number;
    return true;
}

bool validJsonValue(const QVariant& value, int depth = 0)
{
    if (depth > 12 || !value.isValid())
        return false;
    if (ServiceValue::isNumeric(value) && !ServiceValue::finiteNumber(value, nullptr))
        return false;
    if (value.metaType().id() == QMetaType::QVariantMap) {
        const auto map = value.toMap();
        if (map.size() > 128) return false;
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            if (it.key().isEmpty() || it.key().size() > 128 || !validJsonValue(it.value(), depth + 1))
                return false;
        }
    } else if (value.metaType().id() == QMetaType::QVariantList) {
        const auto list = value.toList();
        if (list.size() > 512) return false;
        for (const auto& item : list) if (!validJsonValue(item, depth + 1)) return false;
    } else if (value.metaType().id() != QMetaType::QString
               && value.metaType().id() != QMetaType::Bool
               && value.metaType().id() != QMetaType::Int
               && value.metaType().id() != QMetaType::UInt
               && value.metaType().id() != QMetaType::LongLong
               && value.metaType().id() != QMetaType::ULongLong
               && value.metaType().id() != QMetaType::Double
               && value.metaType().id() != QMetaType::QVariantMap
               && value.metaType().id() != QMetaType::QVariantList) {
        return false;
    }
    return true;
}

bool validFieldsSize(const QVariantMap& fields)
{
    if (!validJsonValue(QVariant::fromValue(fields))) return false;
    return QJsonDocument(QJsonObject::fromVariantMap(fields)).toJson(QJsonDocument::Compact).size()
        <= kMaximumOperationBytes;
}

bool validZone(const QVariantMap& fields, const QString& key, QString* value,
               const LayoutSnapshot& snapshot)
{
    if (!boundedString(fields.value(key), value, 32)) return false;
    return snapshot.object().value(QStringLiteral("zones")).toObject().contains(*value);
}

bool itemExists(const LayoutSnapshot& snapshot, const QString& id, const QString& zone = {})
{
    if (zone.isEmpty()) return !snapshot.itemProperties(id).isEmpty();
    const auto items = snapshot.zoneItems(zone);
    for (const auto& v : items)
        if (v.toMap().value(QStringLiteral("id")).toString() == id) return true;
    return false;
}

bool widgetTypeExists(const QString& type)
{
    for (const auto& item : LayoutCatalog::widgetCatalog())
        if (item.toMap().value(QStringLiteral("type")).toString() == type) return true;
    return false;
}

bool validItemArray(const QVariantList& values)
{
    if (values.size() > kMaximumItemsPerZone) return false;
    QSet<QString> ids;
    for (const auto& value : values) {
        if (value.metaType().id() != QMetaType::QVariantMap) return false;
        const auto item = value.toMap();
        QString id, type;
        if (!boundedString(item.value(QStringLiteral("id")), &id)
            || !boundedString(item.value(QStringLiteral("type")), &type, 64)
            || ids.contains(id) || !validJsonValue(value)) return false;
        // Do not require widgetTypeExists() here: setZoneItems also applies
        // saved/imported zones, which can contain legacy widget types no
        // longer offered by the current palette. addItem remains catalog-bound.
        ids.insert(id);
    }
    return true;
}

bool idsDoNotCollideOutsideZone(const LayoutSnapshot& snapshot,
                                const QString& targetZone, const QVariantList& replacement)
{
    QSet<QString> replacementIds;
    for (const auto& value : replacement)
        replacementIds.insert(value.toMap().value(QStringLiteral("id")).toString());
    const auto zones = snapshot.object().value(QStringLiteral("zones")).toObject();
    for (auto zoneIt = zones.cbegin(); zoneIt != zones.cend(); ++zoneIt) {
        if (zoneIt.key() == targetZone) continue;
        for (const auto& itemValue : zoneIt.value().toArray()) {
            if (replacementIds.contains(itemValue.toObject().value(QStringLiteral("id")).toString()))
                return false;
        }
    }
    return true;
}

bool validateAndApply(SettingsNetwork* settings, const QString& operation,
                      const QVariantMap& fields, QString* reason)
{
    if (!settings || !validFieldsSize(fields)) {
        *reason = QStringLiteral("invalidRequest");
        return false;
    }
    const LayoutSnapshot snapshot(settings->layoutConfiguration());
    QString zone, id, key, value;
    int index = 0, from = 0, to = 0;
    double number = 0.0;
    auto reject = [reason] { *reason = QStringLiteral("invalidRequest"); return false; };
    if (operation == QLatin1String("addItem")) {
        QString type;
        if (!exactKeys(fields, {"type", "zone", "index"})
            || !boundedString(fields.value("type"), &type, 64) || !widgetTypeExists(type)
            || !validZone(fields, "zone", &zone, snapshot)
            || !strictInteger(fields.value("index"), -1, kMaximumItemsPerZone, &index)) return reject();
        const auto items = snapshot.zoneItems(zone);
        if (items.size() >= kMaximumItemsPerZone || (index >= 0 && index > items.size())) return reject();
        settings->addItem(type, zone, index);
    } else if (operation == QLatin1String("removeItem")) {
        if (!exactKeys(fields, {"itemId", "zone"}) || !boundedString(fields.value("itemId"), &id)
            || !validZone(fields, "zone", &zone, snapshot) || !itemExists(snapshot, id, zone)) return reject();
        settings->removeItem(id, zone);
    } else if (operation == QLatin1String("reorderItem")) {
        if (!exactKeys(fields, {"zoneName", "fromIndex", "toIndex"})
            || !validZone(fields, "zoneName", &zone, snapshot)
            || !strictInteger(fields.value("fromIndex"), 0, kMaximumItemsPerZone - 1, &from)
            || !strictInteger(fields.value("toIndex"), 0, kMaximumItemsPerZone - 1, &to)) return reject();
        const auto items = snapshot.zoneItems(zone);
        if (from >= items.size() || to >= items.size()) return reject();
        settings->reorderItem(zone, from, to);
    } else if (operation == QLatin1String("moveItem")) {
        QString fromZone, toZone;
        if (!exactKeys(fields, {"itemId", "fromZone", "toZone", "toIndex"})
            || !boundedString(fields.value("itemId"), &id)
            || !validZone(fields, "fromZone", &fromZone, snapshot) || !validZone(fields, "toZone", &toZone, snapshot)
            || !strictInteger(fields.value("toIndex"), -1, kMaximumItemsPerZone, &index)
            || !itemExists(snapshot, id, fromZone)) return reject();
        const auto dest = snapshot.zoneItems(toZone);
        if (dest.size() >= kMaximumItemsPerZone || (index >= 0 && index > dest.size())) return reject();
        settings->moveItem(id, fromZone, toZone, index);
    } else if (operation == QLatin1String("resetLayoutToDefault")) {
        if (!fields.isEmpty()) return reject();
        settings->resetLayoutToDefault();
    } else if (operation == QLatin1String("resetZoneToDefault")) {
        if (!exactKeys(fields, {"zoneName"}) || !validZone(fields, "zoneName", &zone, snapshot)) return reject();
        settings->resetZoneToDefault(zone);
    } else if (operation == QLatin1String("setZoneOption")) {
        if (!exactKeys(fields, {"zoneName", "key", "value"})
            || !validZone(fields, "zoneName", &zone, snapshot)
            || !boundedString(fields.value("key"), &key, 32)
            || !boundedString(fields.value("value"), &value, 64)) return reject();
        const auto allowedValues = LayoutCatalog::zoneOptionValues(key);
        if (allowedValues.isEmpty() || !allowedValues.contains(value)) return reject();
        settings->setZoneOption(zone, key, value);
    } else if (operation == QLatin1String("setZoneItems")) {
        if (!exactKeys(fields, {"zoneName", "items"}) || !validZone(fields, "zoneName", &zone, snapshot)
            || fields.value("items").metaType().id() != QMetaType::QVariantList) return reject();
        const auto items = fields.value("items").toList();
        if (!validItemArray(items) || !idsDoNotCollideOutsideZone(snapshot, zone, items)) return reject();
        settings->setZoneItems(zone, items);
    } else if (operation == QLatin1String("applyZonePreset")) {
        if (!exactKeys(fields, {"zoneName", "items", "options"})
            || !validZone(fields, "zoneName", &zone, snapshot)
            || fields.value("items").metaType().id() != QMetaType::QVariantList
            || fields.value("options").metaType().id() != QMetaType::QVariantMap) return reject();
        const auto items = fields.value("items").toList();
        const auto options = fields.value("options").toMap();
        if (!validItemArray(items) || !idsDoNotCollideOutsideZone(snapshot, zone, items)
            || options.size() > 4) return reject();
        for (auto it = options.cbegin(); it != options.cend(); ++it) {
            if (!boundedString(QVariant(it.key()), &key, 32)
                || !boundedString(it.value(), &value, 64)) return reject();
            const auto allowedValues = LayoutCatalog::zoneOptionValues(key);
            if (allowedValues.isEmpty() || !allowedValues.contains(value)) return reject();
        }
        settings->applyZonePreset(zone, items, options);
    } else if (operation == QLatin1String("setZoneScale")) {
        if (!exactKeys(fields, {"zoneName", "scale"}) || !validZone(fields, "zoneName", &zone, snapshot)
            || !strictFiniteDouble(fields.value("scale"), 0.5, 2.0, &number)) return reject();
        settings->setZoneScale(zone, number);
    } else if (operation == QLatin1String("setZoneYOffset")) {
        if (!exactKeys(fields, {"zoneName", "offset"}) || !validZone(fields, "zoneName", &zone, snapshot)
            || !strictInteger(fields.value("offset"), -1000, 1000, &index)) return reject();
        settings->setZoneYOffset(zone, index);
    } else if (operation == QLatin1String("setItemProperty")
               || operation == QLatin1String("setItemPropertyList")) {
        if (!exactKeys(fields, {"itemId", "key", "value"})
            || !boundedString(fields.value("itemId"), &id)
            || !boundedString(fields.value("key"), &key, 64)
            || !itemExists(snapshot, id) || !validJsonValue(fields.value("value"))) return reject();
        const auto property = fields.value("value");
        if (operation == QLatin1String("setItemPropertyList")) {
            if (property.metaType().id() != QMetaType::QVariantList
                || !settings->setItemPropertyList(id, key, property.toList())) return reject();
        } else {
            if (property.metaType().id() == QMetaType::QVariantMap
                || property.metaType().id() == QMetaType::QVariantList
                || !settings->setItemProperty(id, key, property)) return reject();
        }
    } else if (operation == QLatin1String("setItemProperties")) {
        if (!exactKeys(fields, {"itemId", "properties"})
            || !boundedString(fields.value("itemId"), &id)
            || fields.value("properties").metaType().id() != QMetaType::QVariantMap
            || !itemExists(snapshot, id)) return reject();
        const auto properties = fields.value("properties").toMap();
        if (properties.isEmpty() || properties.size() > 64) return reject();
        for (auto it = properties.cbegin(); it != properties.cend(); ++it) {
            if (!boundedString(QVariant(it.key()), &key, 64)
                || !validJsonValue(it.value())
                || it.value().metaType().id() == QMetaType::QVariantMap) return reject();
        }
        if (!settings->setItemProperties(id, properties)) return reject();
    } else if (operation == QLatin1String("ensureSettingsAccessible")) {
        if (!fields.isEmpty()) return reject();
        settings->ensureSettingsAccessible();
    } else if (operation == QLatin1String("applyRecipesFirstUpgrade")) {
        if (!fields.isEmpty()) return reject();
        settings->applyRecipesFirstUpgrade();
    } else {
        *reason = QStringLiteral("unsupportedOperation");
        return false;
    }
    *reason = {};
    return true;
}
} // namespace

NativeLayoutServiceSource::NativeLayoutServiceSource(SettingsNetwork* settings, QObject* parent)
    : LayoutStateSimpleSource(parent), m_settings(settings)
{
    if (!settings) {
        setReady(false);
        setErrorMessage(QStringLiteral("Layout settings are unavailable."));
        return;
    }
    m_layoutChanged = connect(settings, &SettingsNetwork::layoutConfigurationChanged,
                              this, [this] { publishSnapshot(true); });
    connect(settings, &QObject::destroyed, this, [this] {
        m_settings.clear();
        setReady(false);
        setErrorMessage(QStringLiteral("Layout settings are unavailable."));
    });
    publishSnapshot(false);
}

void NativeLayoutServiceSource::publishSnapshot(bool advanceRevision)
{
    if (!m_settings) return;
    if (advanceRevision && m_revision < kMaximumSafeRevision) ++m_revision;
    const QString configuration = m_settings->layoutConfiguration();
    if (configuration.toUtf8().size() > kMaximumConfigurationBytes) {
        setReady(false);
        setErrorMessage(QStringLiteral("Layout configuration exceeds 256 KiB."));
        setLayoutConfiguration({});
        return;
    }
    const LayoutSnapshot snapshot(configuration);
    if (!snapshot.object().value(QStringLiteral("zones")).isObject()) {
        setReady(false);
        setErrorMessage(QStringLiteral("Layout configuration is invalid."));
        setLayoutConfiguration({});
        return;
    }
    setLayoutConfiguration(configuration);
    setRevision(m_revision);
    setErrorMessage({});
    setReady(true);
}

void NativeLayoutServiceSource::complete(const QString& requestId, const QString& operation,
                                         bool success, const QString& reason)
{
    QString configuration = m_settings ? m_settings->layoutConfiguration() : QString();
    QString finalReason = reason;
    if (configuration.toUtf8().size() > kMaximumConfigurationBytes) {
        configuration.clear();
        success = false;
        finalReason = QStringLiteral("responseTooLarge");
    }
    m_requests.finish(requestId);
    emit operationCompleted(requestId, operation, success, m_revision,
                            configuration, finalReason);
}

void NativeLayoutServiceSource::execute(QString requestId, qint64 expectedRevision,
                                        QString operation, QVariantMap fields)
{
    const auto start = m_requests.begin(requestId);
    if (start == ServiceRequestIds::Start::DuplicateId) return;
    if (start != ServiceRequestIds::Start::Accepted) {
        emit operationCompleted(requestId, operation, false, m_revision,
                                layoutConfiguration(), QStringLiteral("invalidRequest"));
        return;
    }
    if (!ready() || !m_settings) {
        complete(requestId, operation, false, QStringLiteral("unavailable"));
        return;
    }
    if (expectedRevision < 0 || expectedRevision != m_revision) {
        complete(requestId, operation, false, QStringLiteral("conflict"));
        return;
    }
    const qsizetype currentBytes = layoutConfiguration().toUtf8().size();
    const qsizetype operationBytes = QJsonDocument(QJsonObject::fromVariantMap(fields))
                                         .toJson(QJsonDocument::Compact).size();
    // Reserve space for JSON structure and escaping growth so a successful
    // canonical write cannot publish a snapshot over the wire limit.
    if (currentBytes + operationBytes + 1024 > kMaximumConfigurationBytes) {
        complete(requestId, operation, false, QStringLiteral("tooLarge"));
        return;
    }
    QString reason;
    const bool success = validateAndApply(m_settings, operation, fields, &reason);
    if (success && layoutConfiguration() != m_settings->layoutConfiguration())
        publishSnapshot(false);
    complete(requestId, operation, success, reason);
}
