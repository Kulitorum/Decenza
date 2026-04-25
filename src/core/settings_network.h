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
    Q_INVOKABLE void setItemProperty(const QString& itemId, const QString& key, const QVariant& value);
    Q_INVOKABLE QVariantMap getItemProperties(const QString& itemId) const;

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
