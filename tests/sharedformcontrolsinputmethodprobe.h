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

// Counts conversion calls without adding a reactive property dependency to
// the binding under test. Actual Markdown safety/formatting has its own suite.
class MarkdownRendererProbe : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(MarkdownRenderer)
    QML_SINGLETON
public:
    Q_INVOKABLE QString toHtml(const QString& text) {
        ++m_calls;
        return QStringLiteral("<p>%1</p>").arg(text.toHtmlEscaped());
    }
    Q_INVOKABLE int calls() const { return m_calls; }
private:
    int m_calls = 0;
};
