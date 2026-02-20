#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QJsonObject>
#include <QJsonArray>
#include <QImage>
#include <QSet>

class Settings;

/**
 * @brief Local library for storing and managing layout items, zones, layouts, and themes.
 *
 * Library entries are stored as individual JSON files in <AppDataLocation>/library/.
 * An index file (library/index.json) caches metadata for fast startup.
 *
 * Four entry types:
 *   - "item":   A single layout widget (custom, temperature, etc.)
 *   - "zone":   A complete zone configuration (all items + Y offset)
 *   - "layout": An entire layout (all zones + offsets, optionally with theme)
 *   - "theme":  A color/font theme (colors + fonts + name)
 *
 * Usage from QML:
 *   WidgetLibrary.addItemFromLayout(itemId)
 *   WidgetLibrary.applyItem(entryId, "centerStatus")
 *   WidgetLibrary.addCurrentTheme("My Theme")
 *   WidgetLibrary.applyThemeEntry(entryId)
 */
class WidgetLibrary : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
    Q_PROPERTY(int count READ count NOTIFY entriesChanged)
    Q_PROPERTY(QString selectedEntryId READ selectedEntryId WRITE setSelectedEntryId NOTIFY selectedEntryIdChanged)

public:
    explicit WidgetLibrary(Settings* settings, QObject* parent = nullptr);

    QVariantList entries() const;
    int count() const;
    QString selectedEntryId() const;
    void setSelectedEntryId(const QString& id);

    // Save from current layout to library
    Q_INVOKABLE QString addItemFromLayout(const QString& itemId);
    Q_INVOKABLE QString addZoneFromLayout(const QString& zoneName);
    Q_INVOKABLE QString addCurrentLayout(bool includeTheme = false);

    // Save/apply standalone theme entries
    Q_INVOKABLE QString addCurrentTheme(const QString& name = QString());
    Q_INVOKABLE bool applyThemeEntry(const QString& entryId);

    // Manage library entries
    Q_INVOKABLE bool removeEntry(const QString& entryId);
    Q_INVOKABLE QVariantMap getEntry(const QString& entryId) const;
    Q_INVOKABLE QVariantMap getEntryData(const QString& entryId) const;

    // Apply library entry to active layout
    Q_INVOKABLE bool applyItem(const QString& entryId, const QString& targetZone);
    Q_INVOKABLE bool applyZone(const QString& entryId, const QString& targetZone);
    Q_INVOKABLE bool applyLayout(const QString& entryId, bool applyTheme = false);

    // Import/export for sharing
    Q_INVOKABLE QString importEntry(const QByteArray& json);
    Q_INVOKABLE QByteArray exportEntry(const QString& entryId) const;

    // Update theme entry name (for rename before sharing)
    Q_INVOKABLE bool updateThemeName(const QString& entryId, const QString& newName);

    // Rename entry ID (used after upload to adopt server-assigned ID)
    bool renameEntry(const QString& oldId, const QString& newId);

    // Theme thumbnail generation (color palette grid)
    Q_INVOKABLE void generateThemeThumbnail(const QString& entryId);

    // Thumbnail management
    Q_INVOKABLE void saveThumbnail(const QString& entryId, const QImage& image);
    Q_INVOKABLE void saveThumbnailCompact(const QString& entryId, const QImage& image);
    Q_INVOKABLE QString thumbnailPath(const QString& entryId) const;
    Q_INVOKABLE QString thumbnailCompactPath(const QString& entryId) const;
    Q_INVOKABLE bool hasThumbnail(const QString& entryId) const;
    Q_INVOKABLE bool hasThumbnailCompact(const QString& entryId) const;

    /// Request QML to capture a thumbnail for the given entry (async, fire-and-forget)
    Q_INVOKABLE void triggerThumbnailCapture(const QString& entryId);

    // Tag extraction from entry data
    Q_INVOKABLE QStringList extractTags(const QVariantMap& entryData) const;

    // Filtering
    Q_INVOKABLE QVariantList entriesByType(const QString& type) const;

signals:
    void entriesChanged();
    void selectedEntryIdChanged();
    void entryAdded(const QString& entryId);
    void entryRemoved(const QString& entryId);
    void requestThumbnailCapture(const QString& entryId);
    void thumbnailSaved(const QString& entryId);

private:
    QString libraryPath() const;
    QString thumbnailsPath() const;
    void ensureDirectories();
    void loadIndex();
    void saveIndex();
    void rebuildIndex();
    QString saveEntryFile(const QJsonObject& entry);
    QJsonObject readEntryFile(const QString& entryId) const;
    bool deleteEntryFile(const QString& entryId);
    QJsonObject buildEnvelope(const QString& type, const QJsonObject& data) const;
    QStringList extractTagsFromItem(const QJsonObject& item) const;
    QStringList extractTagsFromTheme(const QJsonObject& themeData) const;

    void populateThumbnailCache();

    Settings* m_settings;
    QVariantList m_index;
    QString m_selectedEntryId;
    QSet<QString> m_thumbExists;         // entry IDs with full thumbnail
    QSet<QString> m_thumbCompactExists;  // entry IDs with compact thumbnail
};
