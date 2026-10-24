#include "core/diagnosticlogging.h"
#include "settings_network.h"
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
#include <QSet>
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

    // NOTE: the Equipment and Recipes idle buttons are NOT injected here. They
    // are one-time additions tied to the DB schema crossing that introduced each
    // feature (equipment=22, recipes=25), driven from the MainController
    // constructor (maincontroller.cpp, after setupRecipeConnections) via
    // injectEquipmentButtonIfMissing()/injectRecipesButtonIfMissing(). They used
    // to live in this method gated only on "is the widget absent?", which re-ran
    // on every launch and resurrected the button every time a user removed it
    // (issue #1586). Presence is user-editable state and was never a valid
    // once-only gate.

    if (textMigrated || connMigrated) {
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

bool SettingsNetwork::recipesUpgradeOffered() const {
    return m_settings.value("layout/recipesUpgradeOffered", false).toBool();
}

void SettingsNetwork::setRecipesUpgradeOffered(bool offered) {
    if (recipesUpgradeOffered() != offered) {
        m_settings.setValue("layout/recipesUpgradeOffered", offered);
        emit recipesUpgradeOfferedChanged();
    }
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

namespace {
// Frozen composition of the *old* (pre-recipes-idle-layout-upgrade) default
// layout, used only to detect whether an upgrading user's current layout is
// still pristine. defaultLayoutJson() itself changes in this release, so this
// snapshot must not be derived from it.
//
// Every zone here except centerStatus has been structurally invariant across
// every past default (verified against git history back to the layout
// system's introduction in PR Kulitorum/Decenza#855). Two mechanisms bring an older stored layout
// to this exact centerTop/bottomRight composition, and they are NOT equally
// strong — the difference matters for the "recipes"/"equipment" entries below:
//  - connectionStatus->machineStatus is renamed inside getLayoutObject(), so
//    it applies unconditionally on every read.
//  - The equipment/recipes buttons are NOT. They used to be injected on every
//    read, but that is exactly the bug fixed in issue #1586 (a removed button
//    came back on the next launch). They are now injected once, from the
//    MainController constructor, on the launch whose migrations cross schema
//    22 / 25 — earlier in the same startup than any acceptRecipesFirstUpgrade()
//    call, which is user-triggered from a dialog. So an upgrading user's stored
//    layout does still carry both by the time this runs.
// The caveat that buys: if migration 22 or 25 defers (they log "incomplete -
// will retry next launch" and leave schema_version unbumped), the injection
// has not happened yet, a genuinely pristine layout will not match this
// snapshot, and isPristineOldDefault() returns false — so the user gets the
// surgical transform instead of a full resetLayoutToDefault(). Degraded, not
// destructive, and it corrects itself on the launch that completes the
// migration. Do not restore the old "unconditional per-read" wording. centerStatus
// is the one exception — it shipped as {temperature, waterLevel,
// machineStatus} from PR Kulitorum/Decenza#855 through Kulitorum/Decenza#1372, then as empty from Kulitorum/Decenza#1372 ("Layout
// editor: drag-reorder... default cleanups") onward, and nothing ever
// migrates an existing stored centerStatus between those two forms. Both are
// pristine (never-customized), so isPristineOldDefault() accepts either.
QJsonObject oldDefaultTypeSequences() {
    QJsonObject seq;
    seq["topLeft"] = QJsonArray();
    seq["topRight"] = QJsonArray();
    seq["centerTop"] = QJsonArray({"recipes", "espresso", "steam", "hotwater", "flush"});
    seq["centerMiddle"] = QJsonArray({"shotPlan"});
    seq["bottomLeft"] = QJsonArray({"sleep"});
    seq["bottomRight"] = QJsonArray({"history", "spacer", "beans", "equipment", "autofavorites", "settings"});
    seq["lowerMidBar"] = QJsonArray();
    return seq;
}

// The historical centerStatus variants that count as pristine (see comment
// above oldDefaultTypeSequences).
QVector<QJsonArray> oldDefaultCenterStatusVariants() {
    return {
        QJsonArray(),                                              // PR Kulitorum/Decenza#1372 onward
        QJsonArray({"temperature", "waterLevel", "machineStatus"}), // PR Kulitorum/Decenza#855 through Kulitorum/Decenza#1372
    };
}

QJsonArray typeSequenceForZone(const QJsonArray& items) {
    QJsonArray types;
    for (const QJsonValue& v : items)
        types.append(v.toObject()["type"].toString());
    return types;
}

bool isPristineOldDefault(const QJsonObject& zones) {
    const QJsonObject oldDefault = oldDefaultTypeSequences();
    for (auto it = oldDefault.constBegin(); it != oldDefault.constEnd(); ++it) {
        if (typeSequenceForZone(zones.value(it.key()).toArray()) != it.value().toArray())
            return false;
    }
    const QJsonArray centerStatus = typeSequenceForZone(zones.value("centerStatus").toArray());
    return oldDefaultCenterStatusVariants().contains(centerStatus);
}
} // namespace

void SettingsNetwork::applyRecipesFirstUpgrade() {
    QJsonObject layout = getLayoutObject();
    QJsonObject zones = layout["zones"].toObject();

    if (isPristineOldDefault(zones)) {
        resetLayoutToDefault();
        return;
    }

    static const QSet<QString> kCenterZones = {
        QStringLiteral("centerTop"), QStringLiteral("centerMiddle"), QStringLiteral("centerStatus")
    };

    // Does any zone in `zoneNames` hold a widget of `type`?
    auto zoneSetContainsType = [&zones](const QSet<QString>& zoneNames, const QLatin1String& type) {
        for (const QString& zoneName : zoneNames) {
            const QJsonArray items = zones.value(zoneName).toArray();
            for (const QJsonValue& v : items)
                if (v.toObject()["type"].toString() == type)
                    return true;
        }
        return false;
    };
    // Does ANY zone hold a widget of `type` — including the unclassified
    // lowerMidBar/statusBar bands that are neither "center" nor "bar"?
    auto layoutContainsType = [&zones](const QLatin1String& type) {
        for (const QString& zoneName : zones.keys()) {
            const QJsonArray items = zones.value(zoneName).toArray();
            for (const QJsonValue& v : items)
                if (v.toObject()["type"].toString() == type)
                    return true;
        }
        return false;
    };

    const bool espressoInCenter = zoneSetContainsType(kCenterZones, QLatin1String("espresso"));

    // Swap Recipes into the Profiles (espresso) button's center slot and move
    // Profiles down to the bottom bar. This runs only when Profiles actually
    // sits in a center zone; otherwise the user has already placed it (and
    // their Recipes button) elsewhere, so both are left untouched.
    //
    // The swap RELOCATES the existing Recipes button into the Profiles slot
    // rather than only inserting one when none exists — the fix for the former
    // "insert only if !hasRecipes" guard, which was dead code AT THE TIME: back
    // then getLayoutObject() injected a Recipes button on every read (by default
    // immediately left of Profiles), so hasRecipes was always true. That is
    // history — getLayoutObject() no longer injects anything (issue #1586), so
    // the call above returns the stored layout as-is. A user whose Recipes
    // button lived anywhere but the center (e.g. the
    // bottom bar) thus had Profiles pulled out with nothing put in its place.
    // Correctness no longer depends on that injection: the recipesItem default
    // below drops a Recipes button into the slot even if none is found; an
    // existing button is reused only to carry its id/options across.
    if (espressoInCenter) {
        // Pull every existing Recipes item out (dedupe), remembering one to
        // reuse so its id and any per-instance options survive. Removing them
        // all first — rather than overwriting Profiles in place and leaving the
        // others — is what guarantees exactly one Recipes button remains when a
        // Recipes item already shares Profiles' center zone.
        QJsonObject recipesItem{{"type", "recipes"}, {"id", "recipes1"}};
        bool haveRecipesItem = false;
        for (const QString& zoneName : zones.keys()) {
            QJsonArray items = zones.value(zoneName).toArray();
            bool changed = false;
            for (qsizetype i = items.size() - 1; i >= 0; --i) {
                if (items[i].toObject()["type"].toString() == QLatin1String("recipes")) {
                    if (!haveRecipesItem) {
                        recipesItem = items[i].toObject();
                        haveRecipesItem = true;
                    }
                    items.removeAt(i);
                    changed = true;
                }
            }
            if (changed) zones[zoneName] = items;
        }

        // Replace the (still-present) Profiles button in place with Recipes, so
        // Recipes lands at Profiles' exact former center position.
        for (const QString& zoneName : kCenterZones) {
            QJsonArray items = zones.value(zoneName).toArray();
            bool done = false;
            for (qsizetype i = 0; i < items.size(); ++i) {
                if (items[i].toObject()["type"].toString() == QLatin1String("espresso")) {
                    items[i] = recipesItem;
                    zones[zoneName] = items;
                    done = true;
                    break;
                }
            }
            if (done) break;
        }

        // Relocate Profiles to the bottom bar — after Equipment, falling back to
        // before Settings, then to appending — but only if the swap left no
        // Profiles button anywhere else: a bar copy, one the user parked in
        // lowerMidBar/statusBar, or a second center zone all count, so a
        // duplicate is never created.
        if (!layoutContainsType(QLatin1String("espresso"))) {
            const QJsonObject espressoItem{{"type", "espresso"}, {"id", "espresso1"}};
            QJsonArray br = zones.value("bottomRight").toArray();
            int insertAt = -1;
            for (qsizetype i = 0; i < br.size(); ++i) {
                if (br[i].toObject()["type"].toString() == QLatin1String("equipment")) {
                    insertAt = static_cast<int>(i) + 1;
                    break;
                }
            }
            if (insertAt < 0) {
                for (qsizetype i = 0; i < br.size(); ++i) {
                    if (br[i].toObject()["type"].toString() == QLatin1String("settings")) {
                        insertAt = static_cast<int>(i);
                        break;
                    }
                }
            }
            if (insertAt < 0) insertAt = static_cast<int>(br.size());
            br.insert(insertAt, espressoItem);
            zones["bottomRight"] = br;
        }
    }

    // Remove every Auto-Favorites item, wherever the user placed it.
    for (const QString& zoneName : zones.keys()) {
        QJsonArray items = zones.value(zoneName).toArray();
        bool changed = false;
        for (qsizetype i = items.size() - 1; i >= 0; --i) {
            if (items[i].toObject()["type"].toString() == QLatin1String("autofavorites")) {
                items.removeAt(i);
                changed = true;
            }
        }
        if (changed) zones[zoneName] = items;
    }

    layout["zones"] = zones;
    saveLayoutObject(layout);
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

bool SettingsNetwork::saveLayoutObjectVerified(const QJsonObject& layout, const QString& what) {
    saveLayoutObject(layout);
    if (m_layoutPreview)
        return true;
    // QSettings::setValue is fire-and-forget (no return, no throw). On read-only
    // storage, a full disk, or a kill before the deferred flush, the write drops
    // silently. That was survivable while this ran on every launch — it simply
    // re-ran next time — but the crossing-gated callers get exactly one attempt
    // ever, so force the flush and surface a failure instead of reporting a
    // success that did not happen.
    //
    // status() is STICKY: QSettingsPrivate::setStatus only overwrites a stored
    // error when the current value is NoError, so anything earlier in the
    // process (e.g. a FormatError parsing a partly-corrupt store at load) latches
    // forever and would make every later write look failed. Sample it before the
    // sync so an already-broken store is reported as "cannot verify" rather than
    // as a fresh failure this write did not cause.
    const QSettings::Status before = m_settings.status();
    m_settings.sync();
    const QSettings::Status after = m_settings.status();

    if (before != QSettings::NoError) {
        DIAG_WARN(APP, "SettingsNetwork").noquote()
            << "cannot verify the write of" << what
            << "— the settings store was already in error state"
            << static_cast<int>(before)
            << "before this write, and QSettings::status() never clears. The value"
            << "may or may not have persisted; treat a missing widget as unpersisted.";
        return false;
    }
    if (after != QSettings::NoError) {
        DIAG_WARN(APP, "SettingsNetwork").noquote()
            << "FAILED to persist" << what
            << "(QSettings status" << static_cast<int>(after)
            << ") — its one-time schema gate is already consumed, so this will NOT be retried;"
            << "add the widget from Settings -> Layout if it is missing";
        return false;
    }
    return true;
}

void SettingsNetwork::injectEquipmentButtonIfMissing() {
    // Placement policy: immediately after the beans item, so an upgraded user
    // gets it in a sensible default place regardless of their custom layout;
    // append to bottomRight if beans was removed. Note the beans anchor is
    // searched across ALL zones in alphabetical key order (QJsonObject::keys()
    // sorts; it is NOT the order the zones appear in the JSON), so a layout
    // whose beans sits in centerTop — which the current default does — puts
    // Equipment in the centre row, not the bottom bar. (Contract and gating:
    // see the declaration.)
    QJsonObject layout = getLayoutObject();
    QJsonObject zones = layout["zones"].toObject();
    for (const QString& zoneName : zones.keys()) {
        const QJsonArray items = zones[zoneName].toArray();
        for (const QJsonValue& v : items) {
            if (v.toObject()["type"].toString() == "equipment")
                return;  // already placed
        }
    }

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
    layout["zones"] = zones;
    if (saveLayoutObjectVerified(layout, QStringLiteral("the Equipment idle button")))
        DIAG_DEBUG(APP, "SettingsNetwork") << "injected Equipment idle button (schema 22 crossed)";
}

void SettingsNetwork::injectRecipesButtonIfMissing() {
    // Placement policy: immediately LEFT of the espresso button; else beside
    // equipment in the bottom row; else appended to bottomRight. Same zone-order
    // caveat as the equipment button above. (Contract and gating: see the
    // declaration.)
    QJsonObject layout = getLayoutObject();
    QJsonObject zones = layout["zones"].toObject();
    for (const QString& zoneName : zones.keys()) {
        const QJsonArray items = zones[zoneName].toArray();
        for (const QJsonValue& v : items) {
            if (v.toObject()["type"].toString() == "recipes")
                return;  // already placed
        }
    }

    const QJsonObject recipesItem{{"type", "recipes"}, {"id", "recipes1"}};
    auto insertRelativeTo = [&zones, &recipesItem](const QString& anchorType,
                                                   int offsetFromAnchor) {
        for (const QString& zoneName : zones.keys()) {
            QJsonArray items = zones[zoneName].toArray();
            for (qsizetype i = 0; i < items.size(); ++i) {
                if (items[i].toObject()["type"].toString() == anchorType) {
                    items.insert(i + offsetFromAnchor, recipesItem);
                    zones[zoneName] = items;
                    return true;
                }
            }
        }
        return false;
    };
    if (!insertRelativeTo(QStringLiteral("espresso"), 0)        // left of espresso
        && !insertRelativeTo(QStringLiteral("equipment"), 1)) { // beside equipment
        QJsonArray br = zones.value("bottomRight").toArray();
        br.append(recipesItem);
        zones["bottomRight"] = br;
    }
    layout["zones"] = zones;
    if (saveLayoutObjectVerified(layout, QStringLiteral("the Recipes idle button")))
        DIAG_DEBUG(APP, "SettingsNetwork") << "injected Recipes idle button (schema 25 crossed)";
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
