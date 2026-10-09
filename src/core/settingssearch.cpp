#include "settingssearch.h"

#include <QAccessible>
#include <QQuickItem>
#include <QtQml/qqml.h>

SettingsSearch::SettingsSearch(QObject* parent)
    : QObject(parent)
{
}

SettingsSearch* SettingsSearch::qmlAttachedProperties(QObject* object)
{
    return new SettingsSearch(object);
}

SettingsSearchLocator::SettingsSearchLocator(QObject* parent)
    : QObject(parent)
{
}

namespace {

// What the item is called, strongest claim first: a SettingsSearch.title override, then the
// names settings_search_index.py reads (accessibleName, Accessible.name), then visible text.
QStringList namesOf(QQuickItem* item)
{
    QStringList names;
    if (auto* attached = qobject_cast<SettingsSearch*>(
            qmlAttachedPropertiesObject<SettingsSearch>(item, false)))
        names << attached->property("title").toString();
    // A pointer handler (TapHandler) is the control but not an Item; its title names its item.
    const auto children = item->children();
    for (QObject* child : children) {
        if (qobject_cast<QQuickItem*>(child))
            continue;
        if (auto* attached = qobject_cast<SettingsSearch*>(
                qmlAttachedPropertiesObject<SettingsSearch>(child, false)))
            names << attached->property("title").toString();
    }
    // The same names, in the same order, as TITLE_PROPS in scripts/settings_search_index.py.
    names << item->property("accessibleName").toString();
    names << item->property("accessibleLabel").toString();
    if (QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(item)) {
        if (iface->role() == QAccessible::Heading)
            return {};   // the card's own header
        names << iface->text(QAccessible::Name);
    }
    for (const char* prop : {"text", "title", "label", "zoneLabel"})
        names << item->property(prop).toString();
    names.removeAll(QString());
    return names;
}

QQuickItem* findItem(QQuickItem* root, const QString& title, bool exact)
{
    const auto children = root->childItems();
    for (QQuickItem* child : children) {
        if (!child->isVisible())
            continue;
        for (const QString& name : namesOf(child)) {
            if (exact ? name == title : name.startsWith(title))
                return child;
        }
        if (QQuickItem* found = findItem(child, title, exact))
            return found;
    }
    return nullptr;
}

} // namespace

QQuickItem* SettingsSearchLocator::findRow(QQuickItem* card, const QString& title) const
{
    if (!card || title.isEmpty())
        return nullptr;
    QQuickItem* item = findItem(card, title, true);
    if (!item)
        item = findItem(card, title, false);
    // SettingsCard -> its ColumnLayout -> rows.
    while (item && item->parentItem() && item->parentItem()->parentItem() != card)
        item = item->parentItem();
    return item;
}
