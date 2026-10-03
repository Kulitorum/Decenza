#pragma once

#include <QObject>
#include <QtQmlIntegration/qqmlintegration.h>

class QQuickItem;

// Closes the open popups that belong to a page. A popup stays on screen when the page that
// declared it is covered by another page: Qt only reacts to its parent item being destroyed
// (qquickpopup.cpp:2274), never to it being hidden. QML cannot do this itself because most
// dialogs are reparented to Overlay.overlay, so only the QObject tree still leads to them.
class PopupCloser : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit PopupCloser(QObject* parent = nullptr);

    Q_INVOKABLE void closeAllUnder(QQuickItem* page);
};
