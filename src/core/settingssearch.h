#pragma once

#include <QObject>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

class QQuickItem;

// `SettingsSearch.title: ...` etc. on an item on a settings card. scripts/settings_search_index.py
// reads these declarations from the QML source, so they must be literals. At runtime only `title`
// is read (by SettingsSearchLocator); the rest are declared here so qmllint checks them.
class SettingsSearch : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("SettingsSearch is an attached property")
    QML_ATTACHED(SettingsSearch)

    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged FINAL)
    Q_PROPERTY(QString description MEMBER m_description NOTIFY descriptionChanged FINAL)
    Q_PROPERTY(QStringList keywords MEMBER m_keywords NOTIFY keywordsChanged FINAL)
    // An out-of-settings destination SettingsPage handles (SettingsSearchRegistry.routes).
    Q_PROPERTY(QString route MEMBER m_route NOTIFY routeChanged FINAL)
    // Content drawn over the tab and reached through the control that opens it, like a Popup.
    Q_PROPERTY(bool overlay MEMBER m_overlay NOTIFY overlayChanged FINAL)

public:
    explicit SettingsSearch(QObject* parent);

    static SettingsSearch* qmlAttachedProperties(QObject* object);

    QString title() const { return m_title; }
    void setTitle(const QString& title);

signals:
    void titleChanged();
    void descriptionChanged();
    void keywordsChanged();
    void routeChanged();
    void overlayChanged();

private:
    QString m_title;
    QString m_description;
    QStringList m_keywords;
    QString m_route;
    bool m_overlay = false;
};

// Finds the item a settings-search result for an adjustment points at, inside its card.
// In C++ because a QML read of `item.Accessible.name` creates the attached object
// (qqmltypewrapper.cpp:385); queryAccessibleInterface() creates none for a plain item without
// one (qquickaccessiblefactory.cpp:34-40).
class SettingsSearchLocator : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit SettingsSearchLocator(QObject* parent = nullptr);

    // The row of `card` holding the visible item whose search title is `title` (or begins with
    // it, up to a word boundary), or null.
    Q_INVOKABLE QQuickItem* findRow(QQuickItem* card, const QString& title) const;
    // Whether such an item exists at all, visible or not: tells a hidden row from a missing one.
    Q_INVOKABLE bool hasItem(QQuickItem* card, const QString& title) const;
};
