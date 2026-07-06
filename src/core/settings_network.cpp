#include "settings_network.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QDesktopServices>
#include <QHash>
#include <QSet>
#include <QUrl>

#ifdef Q_OS_IOS
#include "../screensaver/iosbrightness.h"
#include "SafariViewHelper.h"
#endif

SettingsNetwork::SettingsNetwork(QObject* parent)
    : QObject(parent)
    , m_settings("DecentEspresso", "DE1Qt")
{
}

// Saved searches

QStringList SettingsNetwork::savedSearches() const {
    return m_settings.value("shotHistory/savedSearches").toStringList();
}

void SettingsNetwork::setSavedSearches(const QStringList& searches) {
    if (savedSearches() != searches) {
        m_settings.setValue("shotHistory/savedSearches", searches);
        emit savedSearchesChanged();
    }
}

void SettingsNetwork::addSavedSearch(const QString& search) {
    QString trimmed = search.trimmed();
    if (trimmed.isEmpty()) return;
    QStringList current = savedSearches();
    if (!current.contains(trimmed)) {
        if (current.size() >= 30) return;  // Cap at 30 saved searches
        current.append(trimmed);
        m_settings.setValue("shotHistory/savedSearches", current);
        emit savedSearchesChanged();
    }
}

void SettingsNetwork::removeSavedSearch(const QString& search) {
    QStringList current = savedSearches();
    if (current.removeAll(search) > 0) {
        m_settings.setValue("shotHistory/savedSearches", current);
        emit savedSearchesChanged();
    }
}

// Shot history sort

QString SettingsNetwork::shotHistorySortField() const {
    return m_settings.value("shotHistory/sortField", "timestamp").toString();
}

void SettingsNetwork::setShotHistorySortField(const QString& field) {
    if (shotHistorySortField() != field) {
        m_settings.setValue("shotHistory/sortField", field);
        emit shotHistorySortFieldChanged();
    }
}

QString SettingsNetwork::shotHistorySortDirection() const {
    return m_settings.value("shotHistory/sortDirection", "DESC").toString();
}

void SettingsNetwork::setShotHistorySortDirection(const QString& direction) {
    if (shotHistorySortDirection() != direction) {
        m_settings.setValue("shotHistory/sortDirection", direction);
        emit shotHistorySortDirectionChanged();
    }
}

// Shot server

bool SettingsNetwork::shotServerEnabled() const {
    return m_settings.value("shotServer/enabled", false).toBool();
}

void SettingsNetwork::setShotServerEnabled(bool enabled) {
    if (shotServerEnabled() != enabled) {
        m_settings.setValue("shotServer/enabled", enabled);
        emit shotServerEnabledChanged();
    }
}

QString SettingsNetwork::shotServerHostname() const {
    return m_settings.value("shotServer/hostname", "").toString();
}

void SettingsNetwork::setShotServerHostname(const QString& hostname) {
    if (shotServerHostname() != hostname) {
        m_settings.setValue("shotServer/hostname", hostname);
        emit shotServerHostnameChanged();
    }
}

int SettingsNetwork::shotServerPort() const {
    return m_settings.value("shotServer/port", 8888).toInt();
}

void SettingsNetwork::setShotServerPort(int port) {
    if (shotServerPort() != port) {
        m_settings.setValue("shotServer/port", port);
        emit shotServerPortChanged();
    }
}

bool SettingsNetwork::webSecurityEnabled() const {
    return m_settings.value("shotServer/webSecurityEnabled", false).toBool();
}

void SettingsNetwork::setWebSecurityEnabled(bool enabled) {
    if (webSecurityEnabled() != enabled) {
        m_settings.setValue("shotServer/webSecurityEnabled", enabled);
        emit webSecurityEnabledChanged();
    }
}

// Auto-favorites

QString SettingsNetwork::autoFavoritesGroupBy() const {
    return m_settings.value("autoFavorites/groupBy", "bean_profile").toString();
}

void SettingsNetwork::setAutoFavoritesGroupBy(const QString& groupBy) {
    if (autoFavoritesGroupBy() != groupBy) {
        m_settings.setValue("autoFavorites/groupBy", groupBy);
        emit autoFavoritesGroupByChanged();
    }
}

int SettingsNetwork::autoFavoritesMaxItems() const {
    return m_settings.value("autoFavorites/maxItems", 10).toInt();
}

void SettingsNetwork::setAutoFavoritesMaxItems(int maxItems) {
    if (autoFavoritesMaxItems() != maxItems) {
        m_settings.setValue("autoFavorites/maxItems", maxItems);
        emit autoFavoritesMaxItemsChanged();
    }
}

bool SettingsNetwork::autoFavoritesOpenBrewSettings() const {
    return m_settings.value("autoFavorites/openBrewSettings", false).toBool();
}

void SettingsNetwork::setAutoFavoritesOpenBrewSettings(bool open) {
    if (autoFavoritesOpenBrewSettings() != open) {
        m_settings.setValue("autoFavorites/openBrewSettings", open);
        emit autoFavoritesOpenBrewSettingsChanged();
    }
}

bool SettingsNetwork::autoFavoritesHideUnrated() const {
    return m_settings.value("autoFavorites/hideUnrated", false).toBool();
}

void SettingsNetwork::setAutoFavoritesHideUnrated(bool hide) {
    if (autoFavoritesHideUnrated() != hide) {
        m_settings.setValue("autoFavorites/hideUnrated", hide);
        emit autoFavoritesHideUnratedChanged();
    }
}

// Shot export

bool SettingsNetwork::exportShotsToFile() const {
    return m_settings.value("export/shotsToFile", false).toBool();
}

void SettingsNetwork::setExportShotsToFile(bool enabled) {
    if (exportShotsToFile() != enabled) {
        m_settings.setValue("export/shotsToFile", enabled);
        emit exportShotsToFileChanged();
    }
}

// Discuss Shot

int SettingsNetwork::discussShotApp() const {
    return m_settings.value("ai/discussShotApp", 0).toInt();
}

void SettingsNetwork::setDiscussShotApp(int app) {
    if (discussShotApp() != app) {
        m_settings.setValue("ai/discussShotApp", app);
        emit discussShotAppChanged();
    }
}

QString SettingsNetwork::discussShotCustomUrl() const {
    return m_settings.value("ai/discussShotCustomUrl", "").toString();
}

void SettingsNetwork::setDiscussShotCustomUrl(const QString& url) {
    if (discussShotCustomUrl() != url) {
        m_settings.setValue("ai/discussShotCustomUrl", url);
        emit discussShotCustomUrlChanged();
    }
}

QString SettingsNetwork::claudeRcSessionUrl() const {
    return m_settings.value("ai/claudeRcSessionUrl").toString();
}

void SettingsNetwork::setClaudeRcSessionUrl(const QString& url) {
    if (claudeRcSessionUrl() != url) {
        m_settings.setValue("ai/claudeRcSessionUrl", url);
        emit claudeRcSessionUrlChanged();
    }
}

QString SettingsNetwork::discussShotUrl() const {
    static const QStringList urls = {
        "claude://",
        "https://claude.ai/new",
        "https://chatgpt.com/",
        "https://gemini.google.com/app",
        "https://grok.com/"
    };
    int app = discussShotApp();
    if (app == 5) return discussShotCustomUrl();
    if (app == discussAppNone()) return QString();
    if (app == discussAppClaudeDesktop()) return claudeRcSessionUrl();
    if (app >= 0 && app < urls.size()) return urls[app];
    return urls[0];
}

void SettingsNetwork::openDiscussUrl(const QString& url) {
    if (url.isEmpty()) return;

#ifdef Q_OS_IOS
    if (discussShotApp() == discussAppClaudeDesktop()) {
        if (openInSafariView(url)) return;
    }
#endif

    QDesktopServices::openUrl(QUrl(url));
}

void SettingsNetwork::dismissDiscussOverlay() {
#ifdef Q_OS_IOS
    dismissSafariView();
#endif
}

// Layout configuration

QString SettingsNetwork::defaultLayoutJson() const {
    QJsonObject layout;
    layout["version"] = 1;

    QJsonObject zones;

    zones["topLeft"] = QJsonArray();
    zones["topRight"] = QJsonArray();
    // Center status readouts are intentionally empty by default: temperature,
    // waterLevel and machineStatus all live in the status bar below, so showing
    // them again here would just duplicate the status bar. Users can still add
    // readouts to this zone if they want the larger center display.
    zones["centerStatus"] = QJsonArray();
    zones["centerTop"] = QJsonArray({
        QJsonObject({{"type", "espresso"}, {"id", "espresso1"}}),
        QJsonObject({{"type", "steam"}, {"id", "steam1"}}),
        QJsonObject({{"type", "hotwater"}, {"id", "hotwater1"}}),
        QJsonObject({{"type", "flush"}, {"id", "flush1"}}),
    });
    zones["centerMiddle"] = QJsonArray({
        QJsonObject({{"type", "shotPlan"}, {"id", "plan1"}}),
    });
    zones["bottomLeft"] = QJsonArray({
        QJsonObject({{"type", "sleep"}, {"id", "sleep1"}}),
    });
    zones["bottomRight"] = QJsonArray({
        QJsonObject({{"type", "history"}, {"id", "history1"}}),
        QJsonObject({{"type", "spacer"}, {"id", "spacer2"}}),
        QJsonObject({{"type", "beans"}, {"id", "beans1"}}),
        QJsonObject({{"type", "equipment"}, {"id", "equipment1"}}),
        QJsonObject({{"type", "autofavorites"}, {"id", "autofavorites1"}}),
        QJsonObject({{"type", "settings"}, {"id", "settings1"}}),
    });
    // Status bar uses icon display mode for its readouts — more compact and
    // legible in the bar than text labels.
    zones["statusBar"] = QJsonArray({
        QJsonObject({{"type", "pageTitle"}, {"id", "pagetitle1"}}),
        QJsonObject({{"type", "spacer"}, {"id", "spacer_sb1"}}),
        QJsonObject({{"type", "temperature"}, {"id", "temp_sb1"}, {"displayMode", "icon"}}),
        QJsonObject({{"type", "separator"}, {"id", "sep_sb1"}}),
        QJsonObject({{"type", "waterLevel"}, {"id", "water_sb1"}, {"displayMode", "icon"}}),
        QJsonObject({{"type", "separator"}, {"id", "sep_sb2"}}),
        QJsonObject({{"type", "scaleWeight"}, {"id", "scale_sb1"}, {"displayMode", "icon"}}),
        QJsonObject({{"type", "separator"}, {"id", "sep_sb3"}}),
        QJsonObject({{"type", "machineStatus"}, {"id", "conn_sb1"}, {"displayMode", "icon"}}),
        QJsonObject({{"type", "separator"}, {"id", "sep_sb4"}}),
        QJsonObject({{"type", "clock"}, {"id", "clock_sb1"}, {"displayMode", "icon"}}),
    });
    // Lower-mid bar: optional, general-purpose full-width band above the bottom
    // action bar. Empty by default so it reserves no space and changes nothing
    // until a user adds widgets to it (composable-brew-bar).
    zones["lowerMidBar"] = QJsonArray();

    layout["zones"] = zones;
    return QString::fromUtf8(QJsonDocument(layout).toJson(QJsonDocument::Compact));
}

QJsonObject SettingsNetwork::getLayoutObject() const {
    if (m_layoutCacheValid)
        return m_layoutCache;

    QString stored = m_settings.value("layout/configuration").toString();
    QJsonObject layout;
    if (stored.isEmpty()) {
        layout = QJsonDocument::fromJson(defaultLayoutJson().toUtf8()).object();
    } else {
        QJsonDocument doc = QJsonDocument::fromJson(stored.toUtf8());
        if (doc.isNull() || !doc.isObject()) {
            layout = QJsonDocument::fromJson(defaultLayoutJson().toUtf8()).object();
        } else {
            layout = doc.object();
        }
    }

    // Migration: ensure statusBar zone exists for configs created before this feature
    QJsonObject zones = layout["zones"].toObject();
    if (!zones.contains("statusBar")) {
        zones["statusBar"] = QJsonArray({
            QJsonObject({{"type", "pageTitle"}, {"id", "pagetitle1"}}),
            QJsonObject({{"type", "spacer"}, {"id", "spacer_sb1"}}),
            QJsonObject({{"type", "temperature"}, {"id", "temp_sb1"}}),
            QJsonObject({{"type", "separator"}, {"id", "sep_sb1"}}),
            QJsonObject({{"type", "waterLevel"}, {"id", "water_sb1"}}),
            QJsonObject({{"type", "separator"}, {"id", "sep_sb2"}}),
            QJsonObject({{"type", "scaleWeight"}, {"id", "scale_sb1"}}),
            QJsonObject({{"type", "separator"}, {"id", "sep_sb3"}}),
            QJsonObject({{"type", "machineStatus"}, {"id", "conn_sb1"}}),
        });
        layout["zones"] = zones;
    }

    // Migration: ensure the (empty) lowerMidBar zone exists for older configs so
    // the editor exposes it; empty means it renders nothing (composable-brew-bar).
    if (!zones.contains("lowerMidBar")) {
        zones["lowerMidBar"] = QJsonArray();
        layout["zones"] = zones;
    }

    // Migration: rename "text" type to "custom"
    bool textMigrated = false;
    for (const QString& zoneName : zones.keys()) {
        QJsonArray items = zones[zoneName].toArray();
        for (int i = 0; i < items.size(); ++i) {
            QJsonObject item = items[i].toObject();
            if (item["type"].toString() == "text") {
                item["type"] = "custom";
                items[i] = item;
                textMigrated = true;
            }
        }
        if (textMigrated)
            zones[zoneName] = items;
    }

    // Migration: merge "connectionStatus" into "machineStatus" (composable-status-bar).
    // The machine-status widget shows the phase and "Disconnected" when offline,
    // subsuming the old Online/Offline widget; they are now one widget.
    bool connMigrated = false;
    for (const QString& zoneName : zones.keys()) {
        QJsonArray items = zones[zoneName].toArray();
        bool changed = false;
        for (qsizetype i = 0; i < items.size(); ++i) {
            QJsonObject item = items[i].toObject();
            if (item["type"].toString() == "connectionStatus") {
                item["type"] = "machineStatus";
                items[i] = item;
                changed = true;
                connMigrated = true;
            }
        }
        if (changed)
            zones[zoneName] = items;
    }
    if (connMigrated)
        layout["zones"] = zones;

    // Migration: ensure an Equipment idle button exists (add-equipment-packages).
    // Inject it immediately after the beans item so every upgraded user gets it
    // in a sensible default place regardless of their custom layout (fall back to
    // appending to bottomRight if beans was removed). Idempotent + persisted once
    // so it never duplicates.
    bool equipmentInjected = false;
    {
        bool hasEquipment = false;
        for (const QString& zoneName : zones.keys()) {
            const QJsonArray items = zones[zoneName].toArray();
            for (const QJsonValue& v : items) {
                if (v.toObject()["type"].toString() == "equipment") {
                    hasEquipment = true;
                    break;
                }
            }
            if (hasEquipment)
                break;
        }
        if (!hasEquipment) {
            const QJsonObject equipmentItem{{"type", "equipment"}, {"id", "equipment1"}};
            bool placed = false;
            for (const QString& zoneName : zones.keys()) {
                QJsonArray items = zones[zoneName].toArray();
                for (qsizetype i = 0; i < items.size(); ++i) {
                    if (items[i].toObject()["type"].toString() == "beans") {
                        items.insert(i + 1, equipmentItem);
                        zones[zoneName] = items;
                        placed = true;
                        break;
                    }
                }
                if (placed)
                    break;
            }
            if (!placed) {
                QJsonArray br = zones.value("bottomRight").toArray();
                br.append(equipmentItem);
                zones["bottomRight"] = br;
            }
            equipmentInjected = true;
        }
    }

    if (textMigrated || equipmentInjected || connMigrated) {
        layout["zones"] = zones;
        // Persist the migration so it only runs once
        const_cast<SettingsNetwork*>(this)->saveLayoutObject(layout);
    }

    m_layoutCache = layout;
    m_layoutJsonCache = QString::fromUtf8(QJsonDocument(layout).toJson(QJsonDocument::Compact));
    m_layoutCacheValid = true;
    return m_layoutCache;
}

void SettingsNetwork::invalidateLayoutCache() {
    m_layoutCacheValid = false;
}

void SettingsNetwork::saveLayoutObject(const QJsonObject& layout) {
    m_layoutCache = layout;
    m_layoutJsonCache = QString::fromUtf8(QJsonDocument(layout).toJson(QJsonDocument::Compact));
    m_layoutCacheValid = true;
    m_settings.setValue("layout/configuration", m_layoutJsonCache);
    emit layoutConfigurationChanged();
}

QString SettingsNetwork::generateItemId(const QString& type) const {
    QJsonObject layout = getLayoutObject();
    QJsonObject zones = layout["zones"].toObject();

    int maxNum = 0;
    for (const QString& zoneName : zones.keys()) {
        QJsonArray items = zones[zoneName].toArray();
        for (const QJsonValue& val : items) {
            QJsonObject item = val.toObject();
            if (item["type"].toString() == type) {
                QString id = item["id"].toString();
                qsizetype i = id.length() - 1;
                while (i >= 0 && id[i].isDigit()) --i;
                int num = id.mid(i + 1).toInt();
                if (num > maxNum) maxNum = num;
            }
        }
    }
    return type + QString::number(maxNum + 1);
}

QString SettingsNetwork::layoutConfiguration() const {
    getLayoutObject();
    return m_layoutJsonCache;
}

void SettingsNetwork::setLayoutConfiguration(const QString& json) {
    invalidateLayoutCache();
    m_settings.setValue("layout/configuration", json);
    emit layoutConfigurationChanged();
}

QVariantList SettingsNetwork::getZoneItems(const QString& zoneName) const {
    QJsonObject layout = getLayoutObject();
    QJsonObject zones = layout["zones"].toObject();
    QJsonArray items = zones[zoneName].toArray();

    QVariantList result;
    for (const QJsonValue& val : items) {
        result.append(val.toObject().toVariantMap());
    }
    return result;
}

void SettingsNetwork::moveItem(const QString& itemId, const QString& fromZone, const QString& toZone, int toIndex) {
    QJsonObject layout = getLayoutObject();
    QJsonObject zones = layout["zones"].toObject();

    QJsonArray fromItems = zones[fromZone].toArray();
    QJsonObject movedItem;
    bool found = false;
    for (int i = 0; i < fromItems.size(); ++i) {
        if (fromItems[i].toObject()["id"].toString() == itemId) {
            movedItem = fromItems[i].toObject();
            fromItems.removeAt(i);
            found = true;
            break;
        }
    }
    if (!found) return;

    zones[fromZone] = fromItems;

    QJsonArray toItems = zones[toZone].toArray();
    if (toIndex < 0 || toIndex >= toItems.size()) {
        toItems.append(movedItem);
    } else {
        toItems.insert(toIndex, movedItem);
    }
    zones[toZone] = toItems;

    layout["zones"] = zones;
    saveLayoutObject(layout);
}

void SettingsNetwork::addItem(const QString& type, const QString& zone, int index) {
    QJsonObject layout = getLayoutObject();
    QJsonObject zones = layout["zones"].toObject();

    QString id = generateItemId(type);
    QJsonObject newItem;
    newItem["type"] = type;
    newItem["id"] = id;

    QJsonArray items = zones[zone].toArray();
    if (index < 0 || index >= items.size()) {
        items.append(newItem);
    } else {
        items.insert(index, newItem);
    }
    zones[zone] = items;

    layout["zones"] = zones;
    saveLayoutObject(layout);
}

void SettingsNetwork::removeItem(const QString& itemId, const QString& zone) {
    QJsonObject layout = getLayoutObject();
    QJsonObject zones = layout["zones"].toObject();

    QJsonArray items = zones[zone].toArray();
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].toObject()["id"].toString() == itemId) {
            items.removeAt(i);
            break;
        }
    }
    zones[zone] = items;

    layout["zones"] = zones;
    saveLayoutObject(layout);
}

void SettingsNetwork::reorderItem(const QString& zoneName, int fromIndex, int toIndex) {
    QJsonObject layout = getLayoutObject();
    QJsonObject zones = layout["zones"].toObject();

    QJsonArray items = zones[zoneName].toArray();
    if (fromIndex < 0 || fromIndex >= items.size() || toIndex < 0 || toIndex >= items.size() || fromIndex == toIndex) {
        return;
    }

    QJsonValue item = items[fromIndex];
    items.removeAt(fromIndex);
    items.insert(toIndex, item);
    zones[zoneName] = items;

    layout["zones"] = zones;
    saveLayoutObject(layout);
}

void SettingsNetwork::resetLayoutToDefault() {
    invalidateLayoutCache();
    m_settings.remove("layout/configuration");
    emit layoutConfigurationChanged();
}

// Capability schema: the single source of truth for which per-instance option
// keys each widget type supports. Readout types list the sections the unified
// readout options editor shows; types with a dedicated editor (custom, sleep,
// shot plan, last shot, screensavers) are configurable but carry no readout
// keys. The web editor receives this same table as JSON (see
// readoutCapabilitiesJson), so adding a type or key here is the only
// editor-side registration step — the widget's QML item must still read and
// render the keys it declares (modelData.displayMode / modelData.color).
namespace {
const QHash<QString, QStringList>& readoutOptionSchema() {
    static const QHash<QString, QStringList> schema = {
        { QStringLiteral("machineStatus"),    { QStringLiteral("displayMode"), QStringLiteral("color") } },
        { QStringLiteral("temperature"),      { QStringLiteral("displayMode"), QStringLiteral("color") } },
        { QStringLiteral("steamTemperature"), { QStringLiteral("displayMode"), QStringLiteral("color") } },
        { QStringLiteral("waterLevel"),       { QStringLiteral("displayMode"), QStringLiteral("color") } },
        { QStringLiteral("clock"),            { QStringLiteral("displayMode"), QStringLiteral("color") } },
        { QStringLiteral("scaleWeight"),      { QStringLiteral("dataMode"), QStringLiteral("displayMode"),
                                                QStringLiteral("showRatio"), QStringLiteral("color") } },
        { QStringLiteral("batteryLevel"),     { QStringLiteral("displayMode"), QStringLiteral("color") } },
        { QStringLiteral("scaleBattery"),     { QStringLiteral("displayMode"), QStringLiteral("color") } },
        { QStringLiteral("doseWeight"),       { QStringLiteral("displayMode"), QStringLiteral("color") } },
        { QStringLiteral("milkWeight"),       { QStringLiteral("displayMode"), QStringLiteral("color") } },
        { QStringLiteral("profileName"),      { QStringLiteral("color") } },
    };
    return schema;
}

// Types with a dedicated editor popup (no readout option keys). Screensavers
// are covered by prefix in typeHasBespokeEditor, not listed here.
const QSet<QString>& bespokeEditorTypes() {
    static const QSet<QString> kBespoke = {
        QStringLiteral("custom"),
        QStringLiteral("sleep"),
        QStringLiteral("shotPlan"),
        QStringLiteral("lastShot"),
    };
    return kBespoke;
}

bool typeHasBespokeEditor(const QString& type) {
    // Every screensaver opens the screensaver editor (some show only "no
    // settings", but they still route to an editor — keep parity with the
    // open behaviour so the indicator and the gesture agree).
    if (type.startsWith(QLatin1String("screensaver")))
        return true;
    return bespokeEditorTypes().contains(type);
}
} // namespace

bool SettingsNetwork::typeHasOptions(const QString& type) {
    return typeHasBespokeEditor(type) || readoutOptionSchema().contains(type);
}

QStringList SettingsNetwork::optionKeysForType(const QString& type) {
    return readoutOptionSchema().value(type);
}

QJsonObject SettingsNetwork::readoutCapabilitiesJson() {
    QJsonObject caps;
    const auto& schema = readoutOptionSchema();
    for (auto it = schema.constBegin(); it != schema.constEnd(); ++it)
        caps[it.key()] = QJsonArray::fromStringList(it.value());
    // Bespoke-editor types are configurable but carry no readout keys; the web
    // editor only needs their presence for its has-options behaviour.
    // (Screensavers stay a prefix rule on both sides.)
    for (const QString& type : bespokeEditorTypes())
        caps[type] = QJsonArray();
    return caps;
}

bool SettingsNetwork::itemIsConfigured(const QString& itemId) const {
    QVariantMap props = getItemProperties(itemId);
    if (typeHasOptions(props.value(QStringLiteral("type")).toString()))
        return true;
    // Any stored property beyond the bare type/id means the user customised it.
    for (auto it = props.constBegin(); it != props.constEnd(); ++it) {
        if (it.key() != QLatin1String("type") && it.key() != QLatin1String("id"))
            return true;
    }
    return false;
}

bool SettingsNetwork::hasItemType(const QString& type) const {
    QJsonObject layout = getLayoutObject();
    QJsonObject zones = layout["zones"].toObject();

    for (const QString& zoneName : zones.keys()) {
        QJsonArray items = zones[zoneName].toArray();
        for (const QJsonValue& val : items) {
            if (val.toObject()["type"].toString() == type) {
                return true;
            }
        }
    }
    return false;
}

int SettingsNetwork::getZoneYOffset(const QString& zoneName) const {
    QJsonObject layout = getLayoutObject();
    QJsonObject offsets = layout["offsets"].toObject();
    int defaultOffset = (zoneName == "centerStatus") ? -65 : 0;
    return offsets[zoneName].toInt(defaultOffset);
}

void SettingsNetwork::setZoneYOffset(const QString& zoneName, int offset) {
    QJsonObject layout = getLayoutObject();
    QJsonObject offsets = layout["offsets"].toObject();
    offsets[zoneName] = offset;
    layout["offsets"] = offsets;
    saveLayoutObject(layout);
}

double SettingsNetwork::getZoneScale(const QString& zoneName) const {
    QJsonObject layout = getLayoutObject();
    QJsonObject scales = layout["scales"].toObject();
    return scales[zoneName].toDouble(1.0);
}

void SettingsNetwork::setZoneScale(const QString& zoneName, double scale) {
    scale = qBound(0.5, scale, 2.0);
    QJsonObject layout = getLayoutObject();
    QJsonObject scales = layout["scales"].toObject();
    scales[zoneName] = scale;
    layout["scales"] = scales;
    saveLayoutObject(layout);
}

QString SettingsNetwork::getZoneOption(const QString& zoneName, const QString& key, const QString& defaultValue) const {
    QJsonObject layout = getLayoutObject();
    QJsonObject zoneOptions = layout["zoneOptions"].toObject();
    QJsonObject opts = zoneOptions[zoneName].toObject();
    return opts.contains(key) ? opts[key].toString() : defaultValue;
}

void SettingsNetwork::setZoneOption(const QString& zoneName, const QString& key, const QString& value) {
    QJsonObject layout = getLayoutObject();
    QJsonObject zoneOptions = layout["zoneOptions"].toObject();
    QJsonObject opts = zoneOptions[zoneName].toObject();
    opts[key] = value;
    zoneOptions[zoneName] = opts;
    layout["zoneOptions"] = zoneOptions;
    saveLayoutObject(layout);
}

void SettingsNetwork::setZoneItems(const QString& zoneName, const QVariantList& items) {
    QJsonObject layout = getLayoutObject();
    QJsonObject zones = layout["zones"].toObject();
    QJsonArray arr;
    for (const QVariant& v : items)
        arr.append(QJsonObject::fromVariantMap(v.toMap()));
    zones[zoneName] = arr;
    layout["zones"] = zones;
    saveLayoutObject(layout);
}

void SettingsNetwork::resetZoneToDefault(const QString& zoneName) {
    QJsonObject def = QJsonDocument::fromJson(defaultLayoutJson().toUtf8()).object();
    QJsonObject defZones = def["zones"].toObject();

    QJsonObject layout = getLayoutObject();
    QJsonObject zones = layout["zones"].toObject();
    // Default items (empty array if the zone isn't in the default layout).
    zones[zoneName] = defZones.value(zoneName).toArray();
    layout["zones"] = zones;

    // Drop this zone's per-zone options/offset/scale so they revert to defaults.
    for (const QString& mapKey : { QStringLiteral("zoneOptions"), QStringLiteral("offsets"), QStringLiteral("scales") }) {
        QJsonObject m = layout[mapKey].toObject();
        if (m.contains(zoneName)) {
            m.remove(zoneName);
            layout[mapKey] = m;
        }
    }
    saveLayoutObject(layout);
}

bool SettingsNetwork::setItemProperty(const QString& itemId, const QString& key, const QVariant& value) {
    // A JS array/object passed from QML reaches a QVariant parameter as a
    // wrapped QJSValue, and a JS `undefined` as an invalid QVariant — both of
    // which QJsonValue::fromVariant silently turns into null, so the property
    // would be SAVED as null and read back as absent. Refuse the write instead
    // of corrupting the stored value; arrays must come through the typed
    // setItemPropertyList (typed parameters are converted by the engine — the
    // setZoneItems pattern), and JS objects have no storable mapping here.
    if (qstrcmp(value.typeName(), "QJSValue") == 0) {
        qWarning() << "setItemProperty: refusing JS array/object for" << key
                   << "- pass arrays via setItemPropertyList";
        return false;
    }
    if (!value.isValid()) {
        qWarning() << "setItemProperty: refusing invalid value (JS undefined?) for" << key;
        return false;
    }

    QJsonObject layout = getLayoutObject();
    QJsonObject zones = layout["zones"].toObject();

    for (const QString& zoneName : zones.keys()) {
        QJsonArray items = zones[zoneName].toArray();
        for (int i = 0; i < items.size(); ++i) {
            QJsonObject item = items[i].toObject();
            if (item["id"].toString() == itemId) {
                item[key] = QJsonValue::fromVariant(value);
                items[i] = item;
                zones[zoneName] = items;
                layout["zones"] = zones;
                saveLayoutObject(layout);
                return true;
            }
        }
    }
    // Stale id (widget deleted since the editor was opened, possibly from
    // another device). Without this warning the edit vanishes with success
    // reported at every layer.
    qWarning() << "setItemProperty: no layout item with id" << itemId << "- write for" << key << "dropped";
    return false;
}

bool SettingsNetwork::setItemPropertyList(const QString& itemId, const QString& key, const QVariantList& value) {
    // The typed parameter makes the QML engine convert a JS array to a real
    // QVariantList; from here the generic path stores it as a JSON array.
    return setItemProperty(itemId, key, QVariant(value));
}

QVariantMap SettingsNetwork::getItemProperties(const QString& itemId) const {
    QJsonObject layout = getLayoutObject();
    QJsonObject zones = layout["zones"].toObject();

    for (const QString& zoneName : zones.keys()) {
        QJsonArray items = zones[zoneName].toArray();
        for (const QJsonValue& val : items) {
            QJsonObject item = val.toObject();
            if (item["id"].toString() == itemId) {
                return item.toVariantMap();
            }
        }
    }
    return QVariantMap();
}

QString SettingsNetwork::wifiScaleIp(const QString& hostname) const {
    if (hostname.isEmpty()) return QString();
    return m_settings.value(QStringLiteral("scale/wifiIp/") + hostname, QString()).toString();
}

void SettingsNetwork::setWifiScaleIp(const QString& hostname, const QString& ip) {
    if (hostname.isEmpty()) return;
    const QString key = QStringLiteral("scale/wifiIp/") + hostname;
    if (ip.isEmpty()) {
        m_settings.remove(key);
    } else if (m_settings.value(key).toString() != ip) {
        m_settings.setValue(key, ip);
    }
}
