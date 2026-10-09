#pragma once

#include <QObject>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

class QQuickItem;

// `SettingsSearch.title: ...` etc. on any item on a settings card. Declares or overrides that
// item's settings-search result. scripts/settings_search_index.py reads these declarations from
// the QML source to generate the index, so they must be literals (see that script). At runtime
// SettingsPage reads `title` to find the item a search result points at.
class SettingsSearch : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("SettingsSearch is an attached property")
    QML_ATTACHED(SettingsSearch)

    Q_PROPERTY(QString title MEMBER m_title NOTIFY titleChanged FINAL)
    Q_PROPERTY(QString description MEMBER m_description NOTIFY descriptionChanged FINAL)
    Q_PROPERTY(QStringList keywords MEMBER m_keywords NOTIFY keywordsChanged FINAL)
    // A condition name from SettingsSearchRegistry.conditions; empty means always available.
    Q_PROPERTY(QString availability MEMBER m_availability NOTIFY availabilityChanged FINAL)
    // An out-of-settings destination SettingsPage handles (SettingsSearchRegistry.routes).
    Q_PROPERTY(QString route MEMBER m_route NOTIFY routeChanged FINAL)

public:
    explicit SettingsSearch(QObject* parent);

    static SettingsSearch* qmlAttachedProperties(QObject* object);

signals:
    void titleChanged();
    void descriptionChanged();
    void keywordsChanged();
    void availabilityChanged();
    void routeChanged();

private:
    QString m_title;
    QString m_description;
    QStringList m_keywords;
    QString m_availability;
    QString m_route;
};

// Finds the item a settings-search result for an adjustment points at, inside its card.
// In C++ because reading `item.Accessible.name` from QML would create an Accessible attached
// object on every item walked; this reads only what already exists.
class SettingsSearchLocator : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit SettingsSearchLocator(QObject* parent = nullptr);

    // The row of `card` holding the visible item whose search title is (or starts with)
    // `title`, or null. A row is the card's direct layout child the item sits in.
    Q_INVOKABLE QQuickItem* findRow(QQuickItem* card, const QString& title) const;
};
