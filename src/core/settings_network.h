#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QJsonObject>

// Network/web/layout settings: shot server, web security, auto-favorites,
// saved searches, shot history sort, layout configuration, Discuss-Shot URLs.
// Split from Settings to keep settings.h's transitive-include footprint small.
class SettingsNetwork : public QObject {
    Q_OBJECT

    // Saved searches & shot history sort
    Q_PROPERTY(QStringList savedSearches READ savedSearches WRITE setSavedSearches NOTIFY savedSearchesChanged)
    Q_PROPERTY(QString shotHistorySortField READ shotHistorySortField WRITE setShotHistorySortField NOTIFY shotHistorySortFieldChanged)
    Q_PROPERTY(QString shotHistorySortDirection READ shotHistorySortDirection WRITE setShotHistorySortDirection NOTIFY shotHistorySortDirectionChanged)

    // Shot server (HTTP API)
    Q_PROPERTY(bool shotServerEnabled READ shotServerEnabled WRITE setShotServerEnabled NOTIFY shotServerEnabledChanged)
    Q_PROPERTY(QString shotServerHostname READ shotServerHostname WRITE setShotServerHostname NOTIFY shotServerHostnameChanged)
    Q_PROPERTY(int shotServerPort READ shotServerPort WRITE setShotServerPort NOTIFY shotServerPortChanged)
    Q_PROPERTY(bool webSecurityEnabled READ webSecurityEnabled WRITE setWebSecurityEnabled NOTIFY webSecurityEnabledChanged)

    // Auto-favorites
    Q_PROPERTY(QString autoFavoritesGroupBy READ autoFavoritesGroupBy WRITE setAutoFavoritesGroupBy NOTIFY autoFavoritesGroupByChanged)
    Q_PROPERTY(int autoFavoritesMaxItems READ autoFavoritesMaxItems WRITE setAutoFavoritesMaxItems NOTIFY autoFavoritesMaxItemsChanged)
    Q_PROPERTY(bool autoFavoritesOpenBrewSettings READ autoFavoritesOpenBrewSettings WRITE setAutoFavoritesOpenBrewSettings NOTIFY autoFavoritesOpenBrewSettingsChanged)
    Q_PROPERTY(bool autoFavoritesHideUnrated READ autoFavoritesHideUnrated WRITE setAutoFavoritesHideUnrated NOTIFY autoFavoritesHideUnratedChanged)

    // Shot export
    Q_PROPERTY(bool exportShotsToFile READ exportShotsToFile WRITE setExportShotsToFile NOTIFY exportShotsToFileChanged)

    // Layout configuration
    Q_PROPERTY(QString layoutConfiguration READ layoutConfiguration WRITE setLayoutConfiguration NOTIFY layoutConfigurationChanged)

    // Discuss Shot URLs
    Q_PROPERTY(int discussShotApp READ discussShotApp WRITE setDiscussShotApp NOTIFY discussShotAppChanged)
    Q_PROPERTY(QString discussShotCustomUrl READ discussShotCustomUrl WRITE setDiscussShotCustomUrl NOTIFY discussShotCustomUrlChanged)
    Q_PROPERTY(QString claudeRcSessionUrl READ claudeRcSessionUrl WRITE setClaudeRcSessionUrl NOTIFY claudeRcSessionUrlChanged)
    Q_PROPERTY(int discussAppNone READ discussAppNone CONSTANT)
    Q_PROPERTY(int discussAppClaudeDesktop READ discussAppClaudeDesktop CONSTANT)

public:
    explicit SettingsNetwork(QObject* parent = nullptr);

    int discussAppNone() const { return 6; }
    int discussAppClaudeDesktop() const { return 7; }

    // Saved searches
    QStringList savedSearches() const;
    void setSavedSearches(const QStringList& searches);
    Q_INVOKABLE void addSavedSearch(const QString& search);
    Q_INVOKABLE void removeSavedSearch(const QString& search);

    // Shot history sort
    QString shotHistorySortField() const;
    void setShotHistorySortField(const QString& field);
    QString shotHistorySortDirection() const;
    void setShotHistorySortDirection(const QString& direction);

    // Shot server
    bool shotServerEnabled() const;
    void setShotServerEnabled(bool enabled);
    QString shotServerHostname() const;
    void setShotServerHostname(const QString& hostname);
    int shotServerPort() const;
    void setShotServerPort(int port);
    bool webSecurityEnabled() const;
    void setWebSecurityEnabled(bool enabled);

    // Auto-favorites
    QString autoFavoritesGroupBy() const;
    void setAutoFavoritesGroupBy(const QString& groupBy);
    int autoFavoritesMaxItems() const;
    void setAutoFavoritesMaxItems(int maxItems);
    bool autoFavoritesOpenBrewSettings() const;
    void setAutoFavoritesOpenBrewSettings(bool open);
    bool autoFavoritesHideUnrated() const;
    void setAutoFavoritesHideUnrated(bool hide);

    // Shot export
    bool exportShotsToFile() const;
    void setExportShotsToFile(bool enabled);

    // Discuss Shot
    int discussShotApp() const;
    void setDiscussShotApp(int app);
    QString discussShotCustomUrl() const;
    void setDiscussShotCustomUrl(const QString& url);
    QString claudeRcSessionUrl() const;
    void setClaudeRcSessionUrl(const QString& url);
    Q_INVOKABLE QString discussShotUrl() const;
    Q_INVOKABLE void openDiscussUrl(const QString& url);
    Q_INVOKABLE void dismissDiscussOverlay();

    // WiFi scale IP cache (mDNS resilience). Keyed by bare hostname
    // (no "wifi:" prefix). Empty string means "no entry — go straight
    // to hostname resolution".
    Q_INVOKABLE QString wifiScaleIp(const QString& hostname) const;
    Q_INVOKABLE void setWifiScaleIp(const QString& hostname, const QString& ip);

    // Layout configuration (dynamic IdlePage layout)
    QString layoutConfiguration() const;
    void setLayoutConfiguration(const QString& json);
    Q_INVOKABLE QVariantList getZoneItems(const QString& zoneName) const;
    Q_INVOKABLE void moveItem(const QString& itemId, const QString& fromZone, const QString& toZone, int toIndex);
    Q_INVOKABLE void addItem(const QString& type, const QString& zone, int index = -1);
    Q_INVOKABLE void removeItem(const QString& itemId, const QString& zone);
    Q_INVOKABLE void reorderItem(const QString& zoneName, int fromIndex, int toIndex);
    Q_INVOKABLE void resetLayoutToDefault();
    Q_INVOKABLE bool hasItemType(const QString& type) const;
    Q_INVOKABLE int getZoneYOffset(const QString& zoneName) const;
    Q_INVOKABLE void setZoneYOffset(const QString& zoneName, int offset);
    Q_INVOKABLE double getZoneScale(const QString& zoneName) const;
    Q_INVOKABLE void setZoneScale(const QString& zoneName, double scale);
    // Per-zone layout/appearance options (composable-brew-bar), stored in a
    // zone-keyed map alongside offsets/scales. Keys: "distribution", "alignment",
    // "style". Absent => defaultValue (preserves current behaviour).
    Q_INVOKABLE QString getZoneOption(const QString& zoneName, const QString& key, const QString& defaultValue) const;
    Q_INVOKABLE void setZoneOption(const QString& zoneName, const QString& key, const QString& value);
    // Replace a zone's items in one step (used by "populate from preset").
    Q_INVOKABLE void setZoneItems(const QString& zoneName, const QVariantList& items);
    // Reset a single zone to its default items + options (counterpart to clear).
    Q_INVOKABLE void resetZoneToDefault(const QString& zoneName);
    // Both setters return false when the write was refused (unstorable value)
    // or no item with itemId exists (e.g. deleted from another device while an
    // editor was open) — callers should surface that instead of assuming success.
    Q_INVOKABLE bool setItemProperty(const QString& itemId, const QString& key, const QVariant& value);
    // Array-valued properties set from QML must use this typed variant: the
    // engine converts a JS array to QVariantList for a typed parameter, but
    // hands the generic QVariant one a wrapped QJSValue, which would store as
    // null (setItemProperty refuses that write instead of corrupting the value).
    Q_INVOKABLE bool setItemPropertyList(const QString& itemId, const QString& key, const QVariantList& value);
    Q_INVOKABLE QVariantMap getItemProperties(const QString& itemId) const;

    // Source of truth for "does this widget type expose per-instance options?"
    // Derived from the readout capability schema plus the bespoke-editor set
    // (settings_network.cpp). The web editor receives the same schema as JSON,
    // so there is no hand-maintained mirror. See: layout-readout-capability-schema.
    Q_INVOKABLE static bool typeHasOptions(const QString& type);
    // The readout option keys a widget type supports ("displayMode", "color",
    // "dataMode", "showRatio"). Empty for non-configurable types and for types
    // with a dedicated editor. Drives the unified readout options editor.
    Q_INVOKABLE static QStringList optionKeysForType(const QString& type);
    // The full schema as JSON (type → option keys; bespoke-editor types map to
    // an empty array). Injected into the web layout editor page so it consumes
    // the same table instead of a hand-maintained mirror.
    static QJsonObject readoutCapabilitiesJson();
    // Whether a placed item instance is "configured" — its type has options, or
    // it carries any per-instance property beyond the bare type/id. Used to gate
    // remove-confirmation so an accidental tap can't discard a set-up widget.
    Q_INVOKABLE bool itemIsConfigured(const QString& itemId) const;

signals:
    void savedSearchesChanged();
    void shotHistorySortFieldChanged();
    void shotHistorySortDirectionChanged();
    void shotServerEnabledChanged();
    void shotServerHostnameChanged();
    void shotServerPortChanged();
    void webSecurityEnabledChanged();
    void autoFavoritesGroupByChanged();
    void autoFavoritesMaxItemsChanged();
    void autoFavoritesOpenBrewSettingsChanged();
    void autoFavoritesHideUnratedChanged();
    void exportShotsToFileChanged();
    void discussShotAppChanged();
    void discussShotCustomUrlChanged();
    void claudeRcSessionUrlChanged();
    void layoutConfigurationChanged();

private:
    QString defaultLayoutJson() const;
    QJsonObject getLayoutObject() const;
    void saveLayoutObject(const QJsonObject& layout);
    QString generateItemId(const QString& type) const;
    void invalidateLayoutCache();

    mutable QSettings m_settings;
    mutable QJsonObject m_layoutCache;
    mutable QString m_layoutJsonCache;
    mutable bool m_layoutCacheValid = false;
};
