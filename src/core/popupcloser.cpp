#include "popupcloser.h"

#include <QQuickItem>
#include <QSet>

PopupCloser::PopupCloser(QObject* parent)
    : QObject(parent)
{
}

void PopupCloser::closeAllUnder(QQuickItem* page)
{
    if (!page)
        return;
    // Both trees: a declared popup is a QObject child of its declaring object
    // (qqmlobjectcreator.cpp:1499) or of its Loader (qquickloader.cpp:639), but a Repeater
    // delegate is reachable only as a child item.
    QList<QObject*> pending{page};
    QSet<QObject*> seen;
    while (!pending.isEmpty()) {
        QObject* obj = pending.takeLast();
        if (seen.contains(obj))
            continue;
        seen.insert(obj);
        if (obj->inherits("QQuickPopup") && obj->property("opened").toBool())
            QMetaObject::invokeMethod(obj, "close");
        pending.append(obj->children());
        if (auto* item = qobject_cast<QQuickItem*>(obj)) {
            const auto childItems = item->childItems();
            for (QQuickItem* child : childItems)
                pending.append(child);
        }
    }
}
