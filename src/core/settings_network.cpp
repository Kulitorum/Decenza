#include "core/diagnosticlogging.h"
#include "settings_network.h"
#include "capabilitytoken.h"
#include "layoutcatalog.h"
#include "layoutsnapshot.h"
#include "settings.h"
#include "history/shotcomparisontext.h"
#include "history/coffeebagstorage.h"
#include "core/puckprep.h"
#include "profile/profiledialintext.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QDesktopServices>
#include <QHash>
#include <QUrl>
#include <QVector>

#ifdef Q_OS_IOS
#include "../screensaver/iosbrightness.h"
#include "SafariViewHelper.h"
#endif

SettingsNetwork::SettingsNetwork(QObject* parent)
    : QObject(parent)
{
}

SettingsNetwork::SettingsNetwork(const QJsonObject& layoutPreview, QObject* parent)
    : QObject(parent), m_layoutPreview(true)
{
    saveLayoutObject(layoutPreview);
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

// Recipes page sort (recipe-list-organization). Defaults reproduce the page's
// prior order: most-recently-used first.

QString SettingsNetwork::recipeSortField() const {
    return m_settings.value("recipes/sortField", "dateUsed").toString();
}

void SettingsNetwork::setRecipeSortField(const QString& field) {
    if (recipeSortField() != field) {
        m_settings.setValue("recipes/sortField", field);
        emit recipeSortFieldChanged();
    }
}

QString SettingsNetwork::recipeSortDirection() const {
    return m_settings.value("recipes/sortDirection", "DESC").toString();
}

void SettingsNetwork::setRecipeSortDirection(const QString& direction) {
    if (recipeSortDirection() != direction) {
        m_settings.setValue("recipes/sortDirection", direction);
        emit recipeSortDirectionChanged();
    }
}

// Beans page sort. Defaults reproduce the page's prior order: most-recently-used first.

QString SettingsNetwork::bagSortField() const {
    return m_settings.value("bags/sortField", "dateUsed").toString();
}

void SettingsNetwork::setBagSortField(const QString& field) {
    if (bagSortField() != field) {
        m_settings.setValue("bags/sortField", field);
        emit bagSortFieldChanged();
    }
}

QString SettingsNetwork::bagSortDirection() const {
    return m_settings.value("bags/sortDirection", "DESC").toString();
}

void SettingsNetwork::setBagSortDirection(const QString& direction) {
    if (bagSortDirection() != direction) {
        m_settings.setValue("bags/sortDirection", direction);
        emit bagSortDirectionChanged();
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

bool SettingsNetwork::remoteWebEnabled() const {
    return m_settings.value("shotServer/remoteWebEnabled", false).toBool();
}

void SettingsNetwork::setRemoteWebEnabled(bool enabled) {
    if (remoteWebEnabled() != enabled) {
        m_settings.setValue("shotServer/remoteWebEnabled", enabled);
        emit remoteWebEnabledChanged();
    }
}

QString SettingsNetwork::remoteWebToken() const {
    QString token = m_settings.value("shotServer/remoteWebToken").toString();
    if (token.isEmpty()) {
        token = CapabilityToken::generate();
        m_settings.setValue("shotServer/remoteWebToken", token);
    }
    return token;
}

void SettingsNetwork::rotateRemoteWebToken() {
    m_settings.setValue("shotServer/remoteWebToken", CapabilityToken::generate());
    emit remoteWebTokenChanged();
}

// Auto-favorites

double SettingsNetwork::autoFavoriteGraphHeight() const {
    return m_settings.value("autoFavoriteInfo/graphHeight", 0).toDouble();
}

void SettingsNetwork::setAutoFavoriteGraphHeight(double height) {
    if (autoFavoriteGraphHeight() != height) {
        m_settings.setValue("autoFavoriteInfo/graphHeight", height);
        emit autoFavoriteGraphHeightChanged();
    }
}

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

bool SettingsNetwork::discussAppReadsShot(int app, bool localMcpServing, bool remoteConnectorLive)
{
    // Indices as in discussShotUrl(): 0 Claude app, 1 Claude web, 2 ChatGPT.
    if (app == 0 || app == 1 || app == 2)
        return remoteConnectorLive;
    if (app == 7)  // discussAppClaudeDesktop()
        return localMcpServing;
    return false;
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
        QJsonObject({{"type", "recipes"}, {"id", "recipes1"}}),
        QJsonObject({{"type", "beans"}, {"id", "beans1"}}),
        QJsonObject({{"type", "steam"}, {"id", "steam1"}}),
        QJsonObject({{"type", "hotwater"}, {"id", "hotwater1"}}),
    });
    zones["centerMiddle"] = QJsonArray({
        QJsonObject({{"type", "shotPlan"}, {"id", "plan1"}}),
    });
    zones["bottomLeft"] = QJsonArray({
        QJsonObject({{"type", "sleep"}, {"id", "sleep1"}}),
    });
    zones["bottomRight"] = QJsonArray({
        QJsonObject({{"type", "flush"}, {"id", "flush1"}}),
        QJsonObject({{"type", "history"}, {"id", "history1"}}),
        QJsonObject({{"type", "equipment"}, {"id", "equipment1"}}),
        QJsonObject({{"type", "espresso"}, {"id", "espresso1"}}),
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
    if (m_layoutPreview)
        return;
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
    if (m_layoutPreview) {
        saveLayoutObject(QJsonDocument::fromJson(json.toUtf8()).object());
        return;
    }
    invalidateLayoutCache();
    m_settings.setValue("layout/configuration", json);
    emit layoutConfigurationChanged();
}

QVariantList SettingsNetwork::getZoneItems(const QString& zoneName) const {
    return LayoutSnapshot(getLayoutObject()).zoneItems(zoneName);
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
    if (m_layoutPreview) {
        saveLayoutObject(QJsonDocument::fromJson(defaultLayoutJson().toUtf8()).object());
        return;
    }
    invalidateLayoutCache();
    m_settings.remove("layout/configuration");
    emit layoutConfigurationChanged();
}

bool SettingsNetwork::typeHasOptions(const QString& type) { return LayoutCatalog::typeHasOptions(type); }
QStringList SettingsNetwork::optionKeysForType(const QString& type) { return LayoutCatalog::optionKeysForType(type); }
QJsonObject SettingsNetwork::readoutCapabilitiesJson() { return LayoutCatalog::readoutCapabilitiesJson(); }
QString SettingsNetwork::defaultDisplayModeForType(const QString& type) { return LayoutCatalog::defaultDisplayModeForType(type); }
QJsonObject SettingsNetwork::displayModeDefaultsJson() { return LayoutCatalog::displayModeDefaultsJson(); }
QVariantMap SettingsNetwork::sleepOptionDefaults() { return LayoutCatalog::sleepOptionDefaults(); }
QJsonObject SettingsNetwork::sleepOptionDefaultsJson() { return LayoutCatalog::sleepOptionDefaultsJson(); }
QVariantList SettingsNetwork::widgetCatalog() { return LayoutCatalog::widgetCatalog(); }
QVariantMap SettingsNetwork::widgetChipNames() { return LayoutCatalog::widgetChipNames(); }
QVariantList SettingsNetwork::widgetCategoryNames() { return LayoutCatalog::widgetCategoryNames(); }
QJsonObject SettingsNetwork::widgetCatalogJson() { return LayoutCatalog::widgetCatalogJson(); }
QVariantList SettingsNetwork::layoutActionCatalog() { return LayoutCatalog::layoutActionCatalog(); }
QVariantMap SettingsNetwork::layoutActionLabels() { return LayoutCatalog::layoutActionLabels(); }
QString SettingsNetwork::gestureReservedActionForType(const QString& type) { return LayoutCatalog::gestureReservedActionForType(type); }
bool SettingsNetwork::typeSupportsGestureOverrides(const QString& type) { return LayoutCatalog::typeSupportsGestureOverrides(type); }
QJsonObject SettingsNetwork::layoutActionCatalogJson() { return LayoutCatalog::layoutActionCatalogJson(); }

QVector<QPair<QString, QString>> SettingsNetwork::layoutCatalogTranslationStrings() {
    QVector<QPair<QString, QString>> out = LayoutCatalog::translationStrings();
    // The comparison wording, profile-diff fields, puck-prep flags, and coffee
    // storage hints are separate domains and remain aggregated here.
    out.append(ShotComparisonText::translationStrings());
    for (const auto& e : ProfileDialInText::entries())
        out.append({ QString::fromLatin1(e.key), QString::fromLatin1(e.english) });
    for (const auto& f : PuckPrep::flagLabels())
        out.append({ QString::fromLatin1(f.translationKey), QString::fromLatin1(f.english) });
    for (const auto& o : CoffeeBag::kStorageHintOptions)
        out.append({ QString::fromLatin1(o.labelKey), QString::fromLatin1(o.label) });
    return out;
}

bool SettingsNetwork::itemIsConfigured(const QString& itemId) const {
    return LayoutSnapshot(getLayoutObject()).itemIsConfigured(itemId);
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
    return LayoutSnapshot(getLayoutObject()).zoneYOffset(zoneName);
}

void SettingsNetwork::setZoneYOffset(const QString& zoneName, int offset) {
    QJsonObject layout = getLayoutObject();
    QJsonObject offsets = layout["offsets"].toObject();
    offsets[zoneName] = offset;
    layout["offsets"] = offsets;
    saveLayoutObject(layout);
}

double SettingsNetwork::getZoneScale(const QString& zoneName) const {
    return LayoutSnapshot(getLayoutObject()).zoneScale(zoneName);
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
    return LayoutSnapshot(getLayoutObject()).zoneOption(zoneName, key, defaultValue);
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

void SettingsNetwork::applyZonePreset(const QString& zoneName, const QVariantList& items,
                                      const QVariantMap& options) {
    QJsonObject layout = getLayoutObject();
    QJsonObject zones = layout["zones"].toObject();
    QJsonArray arr;
    for (const QVariant& value : items)
        arr.append(QJsonObject::fromVariantMap(value.toMap()));
    zones[zoneName] = arr;
    layout["zones"] = zones;

    QJsonObject zoneOptions = layout["zoneOptions"].toObject();
    QJsonObject opts = zoneOptions[zoneName].toObject();
    for (auto it = options.cbegin(); it != options.cend(); ++it)
        opts[it.key()] = it.value().toString();
    zoneOptions[zoneName] = opts;
    layout["zoneOptions"] = zoneOptions;
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

void SettingsNetwork::ensureSettingsAccessible() {
    // Keep in sync with the (now-delegating) QML copy in
    // qml/pages/settings/SettingsLayoutTab.qml::ensureSettingsAccessible().
    static const QStringList kZones = {
        QStringLiteral("statusBar"), QStringLiteral("topLeft"), QStringLiteral("topRight"),
        QStringLiteral("centerStatus"), QStringLiteral("centerTop"), QStringLiteral("centerMiddle"),
        QStringLiteral("lowerMidBar"), QStringLiteral("bottomLeft"), QStringLiteral("bottomRight")
    };

    for (const QString& zone : kZones) {
        const QVariantList items = getZoneItems(zone);
        for (const QVariant& itemVar : items) {
            const QVariantMap item = itemVar.toMap();
            const QString type = item.value("type").toString();
            if (type == QStringLiteral("settings")) return;
            if (type == QStringLiteral("custom")) {
                const QVariantMap props = getItemProperties(item.value("id").toString());
                if (props.value("action").toString() == QStringLiteral("navigate:settings")) return;
            }
        }
    }

    // No settings access found — add a settings widget to bottom right.
    addItem(QStringLiteral("settings"), QStringLiteral("bottomRight"));
    if (!m_layoutPreview)
        DIAG_DEBUG(APP, "SettingsNetwork") << "Added settings widget to bottomRight (no settings access found)";
}

bool SettingsNetwork::setItemProperty(const QString& itemId, const QString& key, const QVariant& value) {
    // A JS array/object passed from QML reaches a QVariant parameter as a
    // wrapped QJSValue, and a JS `undefined` as an invalid QVariant — both of
    // which QJsonValue::fromVariant silently turns into null, so the property
    // would be SAVED as null and read back as absent. Refuse the write instead
    // of corrupting the stored value; arrays must come through the typed
    // setItemPropertyList (typed parameters are converted by the engine — the
    // setZoneItems pattern), and JS objects have no storable mapping here.
    if (key.isEmpty()) return false;
    return setItemProperties(itemId, {{key, value}});
}

bool SettingsNetwork::setItemProperties(const QString& itemId, const QVariantMap& properties) {
    if (itemId.isEmpty() || properties.isEmpty()) return false;
    for (auto it = properties.cbegin(); it != properties.cend(); ++it) {
        if (it.key().isEmpty() || !it.value().isValid()
            || qstrcmp(it.value().typeName(), "QJSValue") == 0) {
            DIAG_WARN(APP, "settings_network") << "setItemProperties: refusing invalid or unserializable property"
                                               << it.key();
            return false;
        }
    }

    QJsonObject layout = getLayoutObject();
    QJsonObject zones = layout["zones"].toObject();

    for (const QString& zoneName : zones.keys()) {
        QJsonArray items = zones[zoneName].toArray();
        for (int i = 0; i < items.size(); ++i) {
            QJsonObject item = items[i].toObject();
            if (item["id"].toString() == itemId) {
                for (auto it = properties.cbegin(); it != properties.cend(); ++it)
                    item[it.key()] = QJsonValue::fromVariant(it.value());
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
    DIAG_WARN(APP, "settings_network") << "setItemProperties: no layout item with id" << itemId
                                       << "- patch dropped";
    return false;
}

bool SettingsNetwork::setItemPropertyList(const QString& itemId, const QString& key, const QVariantList& value) {
    // The typed parameter makes the QML engine convert a JS array to a real
    // QVariantList; from here the generic path stores it as a JSON array.
    return setItemProperties(itemId, {{key, QVariant(value)}});
}

QVariantMap SettingsNetwork::getItemProperties(const QString& itemId) const {
    return LayoutSnapshot(getLayoutObject()).itemProperties(itemId);
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
