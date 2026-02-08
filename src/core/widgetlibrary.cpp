#include "widgetlibrary.h"
#include "settings.h"
#include "version.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUuid>
#include <QDateTime>
#include <QRegularExpression>
#include <QDebug>

WidgetLibrary::WidgetLibrary(Settings* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
{
    ensureDirectories();
    loadIndex();
}

// --- Properties ---

QVariantList WidgetLibrary::entries() const
{
    return m_index;
}

int WidgetLibrary::count() const
{
    return m_index.size();
}

QString WidgetLibrary::selectedEntryId() const
{
    return m_selectedEntryId;
}

void WidgetLibrary::setSelectedEntryId(const QString& id)
{
    if (m_selectedEntryId != id) {
        m_selectedEntryId = id;
        emit selectedEntryIdChanged();
    }
}

// --- Save from layout ---

QString WidgetLibrary::addItemFromLayout(const QString& itemId)
{
    QVariantMap props = m_settings->getItemProperties(itemId);
    if (props.isEmpty()) {
        qWarning() << "WidgetLibrary: Item not found:" << itemId;
        return QString();
    }

    // Strip the layout-specific ID (will be regenerated on apply)
    QJsonObject itemObj = QJsonObject::fromVariantMap(props);
    itemObj.remove("id");

    QJsonObject data;
    data["item"] = itemObj;

    QJsonObject envelope = buildEnvelope("item", data);

    // Extract tags
    QStringList tags = extractTagsFromItem(itemObj);
    envelope["tags"] = QJsonArray::fromStringList(tags);

    QString entryId = saveEntryFile(envelope);
    if (!entryId.isEmpty()) {
        emit entryAdded(entryId);
    }
    return entryId;
}

QString WidgetLibrary::addZoneFromLayout(const QString& zoneName)
{
    QVariantList zoneItems = m_settings->getZoneItems(zoneName);
    if (zoneItems.isEmpty()) {
        qWarning() << "WidgetLibrary: Zone empty or not found:" << zoneName;
        return QString();
    }

    QJsonArray itemsArray;
    QStringList allTags;
    for (const QVariant& v : zoneItems) {
        QJsonObject item = QJsonObject::fromVariantMap(v.toMap());
        item.remove("id");  // Strip layout IDs
        itemsArray.append(item);
        allTags.append(extractTagsFromItem(item));
    }
    allTags.removeDuplicates();

    QJsonObject data;
    data["zoneName"] = zoneName;
    data["yOffset"] = m_settings->getZoneYOffset(zoneName);
    data["items"] = itemsArray;

    QJsonObject envelope = buildEnvelope("zone", data);
    envelope["tags"] = QJsonArray::fromStringList(allTags);

    QString entryId = saveEntryFile(envelope);
    if (!entryId.isEmpty()) {
        emit entryAdded(entryId);
    }
    return entryId;
}

QString WidgetLibrary::addCurrentLayout(bool includeTheme)
{
    QJsonObject layoutObj = QJsonDocument::fromJson(
        m_settings->layoutConfiguration().toUtf8()).object();

    if (layoutObj.isEmpty()) {
        qWarning() << "WidgetLibrary: Current layout is empty";
        return QString();
    }

    // Strip all item IDs from the layout
    QJsonObject zones = layoutObj["zones"].toObject();
    QStringList allTags;
    for (const QString& zoneName : zones.keys()) {
        QJsonArray items = zones[zoneName].toArray();
        QJsonArray cleanedItems;
        for (const QJsonValue& val : items) {
            QJsonObject item = val.toObject();
            item.remove("id");
            cleanedItems.append(item);
            allTags.append(extractTagsFromItem(item));
        }
        zones[zoneName] = cleanedItems;
    }
    layoutObj["zones"] = zones;
    allTags.removeDuplicates();

    QJsonObject data;
    data["layout"] = layoutObj;
    data["theme"] = includeTheme
        ? QJsonObject::fromVariantMap(m_settings->customThemeColors())
        : QJsonValue(QJsonValue::Null);

    QJsonObject envelope = buildEnvelope("layout", data);
    envelope["tags"] = QJsonArray::fromStringList(allTags);

    QString entryId = saveEntryFile(envelope);
    if (!entryId.isEmpty()) {
        emit entryAdded(entryId);
    }
    return entryId;
}

// --- Manage entries ---

bool WidgetLibrary::removeEntry(const QString& entryId)
{
    if (!deleteEntryFile(entryId))
        return false;

    // Remove thumbnail if exists
    QString thumbPath = thumbnailPath(entryId);
    if (QFile::exists(thumbPath))
        QFile::remove(thumbPath);

    // Remove from index
    for (int i = 0; i < m_index.size(); ++i) {
        if (m_index[i].toMap()["id"].toString() == entryId) {
            m_index.removeAt(i);
            break;
        }
    }
    saveIndex();
    emit entriesChanged();
    emit entryRemoved(entryId);

    if (m_selectedEntryId == entryId) {
        setSelectedEntryId(QString());
    }
    return true;
}



QVariantMap WidgetLibrary::getEntry(const QString& entryId) const
{
    // Return metadata from index (fast, no file I/O)
    for (const QVariant& v : m_index) {
        QVariantMap meta = v.toMap();
        if (meta["id"].toString() == entryId)
            return meta;
    }
    return QVariantMap();
}

QVariantMap WidgetLibrary::getEntryData(const QString& entryId) const
{
    // Read full entry from file (includes data payload)
    QJsonObject entry = readEntryFile(entryId);
    return entry.toVariantMap();
}

// --- Apply to layout ---

bool WidgetLibrary::applyItem(const QString& entryId, const QString& targetZone)
{
    QJsonObject entry = readEntryFile(entryId);
    if (entry.isEmpty() || entry["type"].toString() != "item") {
        qWarning() << "WidgetLibrary: Invalid item entry:" << entryId;
        return false;
    }

    QJsonObject data = entry["data"].toObject();
    QJsonObject item = data["item"].toObject();
    QString type = item["type"].toString();

    // Add new item to the zone
    m_settings->addItem(type, targetZone);

    // The newly added item gets the last position - find it
    QVariantList zoneItems = m_settings->getZoneItems(targetZone);
    if (zoneItems.isEmpty())
        return false;

    QString newItemId = zoneItems.last().toMap()["id"].toString();

    // Apply all custom properties from the library entry
    QStringList skipKeys = {"type", "id"};
    for (auto it = item.begin(); it != item.end(); ++it) {
        if (!skipKeys.contains(it.key())) {
            m_settings->setItemProperty(newItemId, it.key(), it.value().toVariant());
        }
    }

    qDebug() << "WidgetLibrary: Applied item" << entryId
             << "to zone" << targetZone << "as" << newItemId;
    return true;
}

bool WidgetLibrary::applyZone(const QString& entryId, const QString& targetZone)
{
    QJsonObject entry = readEntryFile(entryId);
    if (entry.isEmpty() || entry["type"].toString() != "zone") {
        qWarning() << "WidgetLibrary: Invalid zone entry:" << entryId;
        return false;
    }

    QJsonObject data = entry["data"].toObject();
    QJsonArray items = data["items"].toArray();

    // Clear the target zone first - remove all existing items
    QVariantList existingItems = m_settings->getZoneItems(targetZone);
    for (int i = existingItems.size() - 1; i >= 0; --i) {
        QString itemId = existingItems[i].toMap()["id"].toString();
        m_settings->removeItem(itemId, targetZone);
    }

    // Add each item from the library zone
    for (const QJsonValue& val : items) {
        QJsonObject item = val.toObject();
        QString type = item["type"].toString();

        m_settings->addItem(type, targetZone);

        // Find the newly added item
        QVariantList zoneItems = m_settings->getZoneItems(targetZone);
        if (zoneItems.isEmpty()) continue;
        QString newItemId = zoneItems.last().toMap()["id"].toString();

        // Apply properties
        QStringList skipKeys = {"type", "id"};
        for (auto it = item.begin(); it != item.end(); ++it) {
            if (!skipKeys.contains(it.key())) {
                m_settings->setItemProperty(newItemId, it.key(), it.value().toVariant());
            }
        }
    }

    // Apply Y offset if available
    if (data.contains("yOffset")) {
        m_settings->setZoneYOffset(targetZone, data["yOffset"].toInt());
    }

    qDebug() << "WidgetLibrary: Applied zone" << entryId
             << "to" << targetZone << "with" << items.size() << "items";
    return true;
}

bool WidgetLibrary::applyLayout(const QString& entryId, bool applyTheme)
{
    QJsonObject entry = readEntryFile(entryId);
    if (entry.isEmpty() || entry["type"].toString() != "layout") {
        qWarning() << "WidgetLibrary: Invalid layout entry:" << entryId;
        return false;
    }

    QJsonObject data = entry["data"].toObject();
    QJsonObject layoutObj = data["layout"].toObject();

    if (layoutObj.isEmpty())
        return false;

    // Regenerate IDs for all items in the layout
    QJsonObject zones = layoutObj["zones"].toObject();
    int counter = 1;
    for (const QString& zoneName : zones.keys()) {
        QJsonArray items = zones[zoneName].toArray();
        QJsonArray itemsWithIds;
        for (const QJsonValue& val : items) {
            QJsonObject item = val.toObject();
            item["id"] = item["type"].toString() + QString::number(counter++);
            itemsWithIds.append(item);
        }
        zones[zoneName] = itemsWithIds;
    }
    layoutObj["zones"] = zones;

    // Apply the full layout
    QString layoutJson = QString::fromUtf8(QJsonDocument(layoutObj).toJson(QJsonDocument::Compact));
    m_settings->setLayoutConfiguration(layoutJson);

    // Apply theme if requested and available
    if (applyTheme && !data["theme"].isNull()) {
        QVariantMap themeColors = data["theme"].toObject().toVariantMap();
        if (!themeColors.isEmpty()) {
            m_settings->setCustomThemeColors(themeColors);
        }
    }

    qDebug() << "WidgetLibrary: Applied layout" << entryId
             << "(theme:" << applyTheme << ")";
    return true;
}

// --- Import/Export ---

QString WidgetLibrary::importEntry(const QByteArray& json)
{
    QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) {
        qWarning() << "WidgetLibrary: Invalid JSON for import";
        return QString();
    }

    QJsonObject entry = doc.object();

    // Validate required fields
    if (!entry.contains("type") || !entry.contains("data")) {
        qWarning() << "WidgetLibrary: Missing required fields in import";
        return QString();
    }

    // Use server ID if present, otherwise generate one
    if (!entry.contains("id") || entry["id"].toString().isEmpty()) {
        entry["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    entry["importedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    return saveEntryFile(entry);
}

QByteArray WidgetLibrary::exportEntry(const QString& entryId) const
{
    QJsonObject entry = readEntryFile(entryId);
    if (entry.isEmpty())
        return QByteArray();

    return QJsonDocument(entry).toJson(QJsonDocument::Compact);
}

// --- Thumbnails ---

void WidgetLibrary::saveThumbnail(const QString& entryId, const QImage& image)
{
    QString path = thumbnailPath(entryId);
    if (image.save(path, "PNG")) {
        qDebug() << "WidgetLibrary: Saved thumbnail for" << entryId;
    } else {
        qWarning() << "WidgetLibrary: Failed to save thumbnail:" << path;
    }
}

QString WidgetLibrary::thumbnailPath(const QString& entryId) const
{
    return thumbnailsPath() + "/" + entryId + ".png";
}

bool WidgetLibrary::hasThumbnail(const QString& entryId) const
{
    return QFile::exists(thumbnailPath(entryId));
}

void WidgetLibrary::triggerThumbnailCapture(const QString& entryId)
{
    if (entryId.isEmpty()) return;
    QVariantMap entry = getEntry(entryId);
    if (entry.isEmpty()) return;
    emit requestThumbnailCapture(entryId);
}

// --- Tag extraction ---

QStringList WidgetLibrary::extractTags(const QVariantMap& entryData) const
{
    QJsonObject data = QJsonObject::fromVariantMap(entryData);
    QString type = data["type"].toString();
    QJsonObject dataPayload = data["data"].toObject();

    QStringList tags;

    if (type == "item") {
        tags = extractTagsFromItem(dataPayload["item"].toObject());
    } else if (type == "zone") {
        QJsonArray items = dataPayload["items"].toArray();
        for (const QJsonValue& val : items) {
            tags.append(extractTagsFromItem(val.toObject()));
        }
    } else if (type == "layout") {
        QJsonObject layout = dataPayload["layout"].toObject();
        QJsonObject zones = layout["zones"].toObject();
        for (const QString& zoneName : zones.keys()) {
            QJsonArray items = zones[zoneName].toArray();
            for (const QJsonValue& val : items) {
                tags.append(extractTagsFromItem(val.toObject()));
            }
        }
    }

    tags.removeDuplicates();
    return tags;
}

QStringList WidgetLibrary::extractTagsFromItem(const QJsonObject& item) const
{
    QStringList tags;

    // Add item type as a tag
    QString type = item["type"].toString();
    if (!type.isEmpty())
        tags.append("type:" + type);

    // Extract variables from content field (%VAR% patterns)
    QString content = item["content"].toString();
    if (!content.isEmpty()) {
        static QRegularExpression varRegex("%([A-Z_]+)%");
        QRegularExpressionMatchIterator it = varRegex.globalMatch(content);
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            tags.append("var:" + match.captured(0));  // e.g., "var:%TEMP%"
        }
    }

    // Extract actions
    QStringList actionFields = {"action", "longPressAction", "doubleclickAction"};
    for (const QString& field : actionFields) {
        QString action = item[field].toString();
        if (!action.isEmpty()) {
            tags.append("action:" + action);  // e.g., "action:navigate:settings"
        }
    }

    return tags;
}

// --- Filtering ---

QVariantList WidgetLibrary::entriesByType(const QString& type) const
{
    QVariantList filtered;
    for (const QVariant& v : m_index) {
        if (v.toMap()["type"].toString() == type)
            filtered.append(v);
    }
    return filtered;
}

// --- Private helpers ---

QString WidgetLibrary::libraryPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/library";
}

QString WidgetLibrary::thumbnailsPath() const
{
    return libraryPath() + "/thumbnails";
}

void WidgetLibrary::ensureDirectories()
{
    QDir().mkpath(libraryPath());
    QDir().mkpath(thumbnailsPath());
}

void WidgetLibrary::loadIndex()
{
    QString indexPath = libraryPath() + "/index.json";
    QFile file(indexPath);
    if (!file.open(QIODevice::ReadOnly)) {
        // No index yet - try to rebuild from files
        rebuildIndex();
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isArray()) {
        qWarning() << "WidgetLibrary: Invalid index, rebuilding";
        rebuildIndex();
        return;
    }

    m_index.clear();
    QJsonArray arr = doc.array();
    bool needsRebuild = false;
    for (const QJsonValue& val : arr) {
        QJsonObject obj = val.toObject();
        if (!obj.contains("data"))
            needsRebuild = true;
        m_index.append(obj.toVariantMap());
    }

    if (needsRebuild && !arr.isEmpty()) {
        qDebug() << "WidgetLibrary: Index missing data fields, rebuilding";
        rebuildIndex();
        return;
    }

    qDebug() << "WidgetLibrary: Loaded index with" << m_index.size() << "entries";
}

void WidgetLibrary::saveIndex()
{
    QJsonArray arr;
    for (const QVariant& v : m_index) {
        arr.append(QJsonObject::fromVariantMap(v.toMap()));
    }

    QString indexPath = libraryPath() + "/index.json";
    QFile file(indexPath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "WidgetLibrary: Failed to save index:" << indexPath;
        return;
    }
    file.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    file.close();
}

void WidgetLibrary::rebuildIndex()
{
    m_index.clear();

    QDir dir(libraryPath());
    QStringList jsonFiles = dir.entryList({"*.json"}, QDir::Files);

    for (const QString& filename : jsonFiles) {
        if (filename == "index.json")
            continue;

        QFile file(dir.filePath(filename));
        if (!file.open(QIODevice::ReadOnly))
            continue;

        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();

        if (!doc.isObject())
            continue;

        QJsonObject entry = doc.object();

        // Build metadata for index (includes data for preview rendering)
        QVariantMap meta;
        meta["id"] = entry["id"].toString();
        meta["type"] = entry["type"].toString();
        meta["createdAt"] = entry["createdAt"].toString();
        meta["tags"] = entry["tags"].toArray().toVariantList();
        meta["data"] = entry["data"].toObject().toVariantMap();

        m_index.append(meta);
    }

    saveIndex();
    qDebug() << "WidgetLibrary: Rebuilt index with" << m_index.size() << "entries";
}

QString WidgetLibrary::saveEntryFile(const QJsonObject& entry)
{
    QString entryId = entry["id"].toString();
    if (entryId.isEmpty()) {
        qWarning() << "WidgetLibrary: Entry has no ID";
        return QString();
    }

    QString filePath = libraryPath() + "/" + entryId + ".json";
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "WidgetLibrary: Failed to write:" << filePath;
        return QString();
    }

    file.write(QJsonDocument(entry).toJson(QJsonDocument::Compact));
    file.close();

    // Add to index (includes data for preview rendering in QML)
    QVariantMap meta;
    meta["id"] = entryId;
    meta["type"] = entry["type"].toString();
    meta["createdAt"] = entry["createdAt"].toString();
    meta["tags"] = entry["tags"].toArray().toVariantList();
    meta["data"] = entry["data"].toObject().toVariantMap();

    m_index.append(meta);
    saveIndex();
    emit entriesChanged();

    qDebug() << "WidgetLibrary: Saved" << entry["type"].toString()
             << "entry:" << entryId;
    return entryId;
}

QJsonObject WidgetLibrary::readEntryFile(const QString& entryId) const
{
    QString filePath = libraryPath() + "/" + entryId + ".json";
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "WidgetLibrary: Entry not found:" << filePath;
        return QJsonObject();
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) {
        qWarning() << "WidgetLibrary: Invalid entry file:" << filePath;
        return QJsonObject();
    }

    return doc.object();
}

bool WidgetLibrary::deleteEntryFile(const QString& entryId)
{
    QString filePath = libraryPath() + "/" + entryId + ".json";
    if (!QFile::exists(filePath)) {
        qWarning() << "WidgetLibrary: Entry file not found:" << filePath;
        return false;
    }
    return QFile::remove(filePath);
}

QJsonObject WidgetLibrary::buildEnvelope(const QString& type,
                                          const QJsonObject& data) const
{
    QJsonObject envelope;
    envelope["version"] = 1;
    envelope["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    envelope["type"] = type;
    envelope["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    envelope["appVersion"] = QString(VERSION_STRING);
    envelope["data"] = data;
    envelope["tags"] = QJsonArray();  // Populated by caller

    return envelope;
}
