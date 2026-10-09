#include "settingssearch.h"

#include <QAccessible>
#include <QMetaObject>
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

void SettingsSearch::setTitle(const QString& title)
{
    if (m_title == title)
        return;
    m_title = title;
    emit titleChanged();
}

SettingsSearchLocator::SettingsSearchLocator(QObject* parent)
    : QObject(parent)
{
}

namespace {

// The properties a search title is read from, strongest first, around the accessible name.
// scripts/settings_search_index.py reads both arrays from this file, so it and this lookup
// cannot disagree; keep them literal.
constexpr const char* kNameProperties[] = {"accessibleName", "accessibleLabel"};
constexpr const char* kTextProperties[] = {"text", "title", "label", "zoneLabel"};

QString attachedTitle(QObject* object)
{
    auto* attached = qobject_cast<SettingsSearch*>(qmlAttachedPropertiesObject<SettingsSearch>(object, false));
    return attached ? attached->title() : QString();
}

// Every name the item answers to, strongest first. A Heading (the card's own header) answers
// only to a title declared on it.
QStringList namesOf(QQuickItem* item)
{
    QStringList names{attachedTitle(item)};
    // A pointer handler (TapHandler) is the control but not an Item; its title names its item.
    const auto children = item->children();
    for (QObject* child : children) {
        if (!qobject_cast<QQuickItem*>(child))
            names << attachedTitle(child);
    }
    QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(item);
    if (!iface || iface->role() != QAccessible::Heading) {
        for (const char* prop : kNameProperties)
            names << item->property(prop).toString();
        if (iface)
            names << iface->text(QAccessible::Name);
        for (const char* prop : kTextProperties)
            names << item->property(prop).toString();
    }
    names.removeAll(QString());
    return names;
}

bool matches(const QString& name, const QString& title, bool exact)
{
    if (exact)
        return name == title;
    // "Level" may name "Level: 5", never "Level offset".
    return name.startsWith(title) && (name.size() == title.size() || !name.at(title.size()).isLetterOrNumber());
}

QQuickItem* findItem(QQuickItem* root, const QString& title, bool exact, bool visibleOnly)
{
    const auto children = root->childItems();
    for (QQuickItem* child : children) {
        if (visibleOnly && !child->isVisible())
            continue;
        for (const QString& name : namesOf(child)) {
            if (matches(name, title, exact))
                return child;
        }
        if (QQuickItem* found = findItem(child, title, exact, visibleOnly))
            return found;
    }
    return nullptr;
}

QQuickItem* find(QQuickItem* card, const QString& title, bool visibleOnly)
{
    if (!card || title.isEmpty())
        return nullptr;
    QQuickItem* item = findItem(card, title, true, visibleOnly);
    return item ? item : findItem(card, title, false, visibleOnly);
}

} // namespace

QQuickItem* SettingsSearchLocator::findRow(QQuickItem* card, const QString& title) const
{
    QQuickItem* item = find(card, title, true);
    // A Repeater takes no space (qquickrepeater.cpp:25); its first delegate stands for it.
    if (item && item->inherits("QQuickRepeater")) {
        QQuickItem* first = nullptr;
        QMetaObject::invokeMethod(item, "itemAt", Q_RETURN_ARG(QQuickItem*, first), Q_ARG(int, 0));
        item = first;
    }
    // The row is the ancestor sitting directly in the nearest vertical layout: the card's own
    // column, or the column inside a Flickable on a scrolling card.
    while (item && item != card && item->parentItem()) {
        QQuickItem* parent = item->parentItem();
        if (parent == card || parent->inherits("QQuickColumnLayout") || parent->inherits("QQuickColumn"))
            break;
        item = parent;
    }
    return item;
}

bool SettingsSearchLocator::hasItem(QQuickItem* card, const QString& title) const
{
    return find(card, title, false) != nullptr;
}
