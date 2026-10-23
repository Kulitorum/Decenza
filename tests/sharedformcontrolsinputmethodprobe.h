#pragma once

#include <QCoreApplication>
#include <QInputMethodEvent>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

// Sends real Qt input-method events to a focused Quick item so the QML tests
// exercise TextInput's committed and preedit text paths without a platform IME.
class SharedFormControlsInputMethodProbe : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    Q_INVOKABLE void sendPreedit(QQuickItem *target, const QString &preedit) const
    {
        if (!target)
            return;
        QInputMethodEvent event(preedit, {});
        QCoreApplication::sendEvent(target, &event);
    }

    Q_INVOKABLE void commit(QQuickItem *target, const QString &text) const
    {
        if (!target)
            return;
        QInputMethodEvent event;
        event.setCommitString(text);
        QCoreApplication::sendEvent(target, &event);
    }
};
