#pragma once

#include <QObject>
#include <QHash>
#include <QPointer>
#include <QString>
#include <QtGlobal>

#include <QtQml/qqmlregistration.h>

// Local system clipboard adapter. It never crosses the host/remote boundary.
// QClipboard is synchronous on native platforms; WebAssembly uses the browser's
// Promise-based Clipboard API so QML can report permission failures accurately.
class LocalClipboard : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    LocalClipboard() = default;
    ~LocalClipboard() override;

    Q_INVOKABLE QString newRequestId() const;
    Q_INVOKABLE bool writeText(const QString& requestId, const QString& text);
    Q_INVOKABLE bool readText(const QString& requestId);

    // Called by the Emscripten Promise callback. Public for the C ABI bridge;
    // application/QML callers should use the correlated methods above.
    static void completeWasmRequest(quint32 token, bool success,
                                    const QString& text, const QString& reason);

signals:
    void operationCompleted(const QString& requestId, const QString& operation,
                            bool success, const QString& text, const QString& reason);

private:
    struct PendingOperation {
        QString requestId;
        QString operation;
    };

    quint32 begin(const QString& requestId, const QString& operation);
    void finish(quint32 token, bool success, const QString& text, const QString& reason);

    QHash<quint32, PendingOperation> m_pending;
};
