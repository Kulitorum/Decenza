#include "localclipboard.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMetaObject>
#include <QUuid>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <cstdlib>
#endif

namespace {
constexpr qsizetype MaxClipboardBytes = 64 * 1024;
constexpr qsizetype MaxPendingOperations = 8;
quint32 s_nextToken = 1;
#ifdef __EMSCRIPTEN__
QHash<quint32, QPointer<LocalClipboard>> s_wasmRequests;
#endif
}

LocalClipboard::~LocalClipboard()
{
#ifdef __EMSCRIPTEN__
    for (auto it = s_wasmRequests.begin(); it != s_wasmRequests.end();) {
        if (it.value() == this)
            it = s_wasmRequests.erase(it);
        else
            ++it;
    }
#endif
}

QString LocalClipboard::newRequestId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

quint32 LocalClipboard::begin(const QString& requestId, const QString& operation)
{
    if (requestId.trimmed().isEmpty() || m_pending.size() >= MaxPendingOperations)
        return 0;
    for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it) {
        if (it->requestId == requestId)
            return 0;
    }

    quint32 token = 0;
    do {
        token = s_nextToken++;
        if (s_nextToken == 0)
            s_nextToken = 1;
    } while (token == 0 || m_pending.contains(token)
#ifdef __EMSCRIPTEN__
             || s_wasmRequests.contains(token)
#endif
    );
    m_pending.insert(token, {requestId, operation});
#ifdef __EMSCRIPTEN__
    s_wasmRequests.insert(token, this);
#endif
    return token;
}

bool LocalClipboard::writeText(const QString& requestId, const QString& text)
{
    const quint32 token = begin(requestId, QStringLiteral("writeText"));
    if (!token)
        return false;

    if (text.size() > MaxClipboardBytes) {
        QMetaObject::invokeMethod(this, [this, token] {
            finish(token, false, {}, QStringLiteral("tooLarge"));
        }, Qt::QueuedConnection);
        return true;
    }
    const QByteArray utf8 = text.toUtf8();
    if (utf8.size() > MaxClipboardBytes) {
        QMetaObject::invokeMethod(this, [this, token] {
            finish(token, false, {}, QStringLiteral("tooLarge"));
        }, Qt::QueuedConnection);
        return true;
    }

#ifdef __EMSCRIPTEN__
    EM_ASM({
        const token = $0;
        const send = (ok, reason) => {
            const textPtr = stringToNewUTF8("");
            const reasonPtr = stringToNewUTF8(reason || "");
            Module._decenzaLocalClipboardComplete(token, ok ? 1 : 0, textPtr, reasonPtr);
        };
        const clipboard = navigator.clipboard;
        if (!clipboard || typeof clipboard.writeText !== "function") {
            send(false, "unsupported");
            return;
        }
        let pending;
        try {
            pending = clipboard.writeText(UTF8ToString($1));
        } catch (error) {
            send(false, error && error.name ? error.name : "writeFailed");
            return;
        }
        pending.then(() => send(true, "")).catch(error => {
            send(false, error && error.name ? error.name : "writeFailed");
        });
    }, token, utf8.constData());
    return true;
#else
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        QMetaObject::invokeMethod(this, [this, token] {
            finish(token, false, {}, QStringLiteral("unavailable"));
        }, Qt::QueuedConnection);
        return true;
    }
    clipboard->setText(text, QClipboard::Clipboard);
    QMetaObject::invokeMethod(this, [this, token] {
        finish(token, true, {}, {});
    }, Qt::QueuedConnection);
    return true;
#endif
}

bool LocalClipboard::readText(const QString& requestId)
{
    const quint32 token = begin(requestId, QStringLiteral("readText"));
    if (!token)
        return false;

#ifdef __EMSCRIPTEN__
    EM_ASM({
        const token = $0;
        const send = (ok, text, reason) => {
            if (ok && lengthBytesUTF8(text || "") > 65536) {
                ok = false;
                text = "";
                reason = "tooLarge";
            }
            const textPtr = stringToNewUTF8(text || "");
            const reasonPtr = stringToNewUTF8(reason || "");
            Module._decenzaLocalClipboardComplete(token, ok ? 1 : 0, textPtr, reasonPtr);
        };
        const clipboard = navigator.clipboard;
        if (!clipboard || typeof clipboard.readText !== "function") {
            send(false, "", "unsupported");
            return;
        }
        let pending;
        try {
            // This invocation occurs synchronously inside the QML button's
            // click handler, preserving browser transient user activation.
            pending = clipboard.readText();
        } catch (error) {
            send(false, "", error && error.name ? error.name : "readFailed");
            return;
        }
        pending.then(text => send(true, text, "")).catch(error => {
            send(false, "", error && error.name ? error.name : "readFailed");
        });
    }, token);
    return true;
#else
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        QMetaObject::invokeMethod(this, [this, token] {
            finish(token, false, {}, QStringLiteral("unavailable"));
        }, Qt::QueuedConnection);
        return true;
    }
    const QString text = clipboard->text(QClipboard::Clipboard);
    bool withinLimit = text.size() <= MaxClipboardBytes;
    if (withinLimit)
        withinLimit = text.toUtf8().size() <= MaxClipboardBytes;
    QMetaObject::invokeMethod(this, [this, token, text, withinLimit] {
        finish(token, withinLimit, withinLimit ? text : QString(),
               withinLimit ? QString() : QStringLiteral("tooLarge"));
    }, Qt::QueuedConnection);
    return true;
#endif
}

void LocalClipboard::finish(quint32 token, bool success, const QString& text,
                            const QString& reason)
{
    const auto it = m_pending.find(token);
    if (it == m_pending.end())
        return;
    const PendingOperation pending = it.value();
    m_pending.erase(it);
#ifdef __EMSCRIPTEN__
    s_wasmRequests.remove(token);
#endif
    emit operationCompleted(pending.requestId, pending.operation,
                            success, text, reason);
}

void LocalClipboard::completeWasmRequest(quint32 token, bool success,
                                        const QString& text, const QString& reason)
{
#ifdef __EMSCRIPTEN__
    const QPointer<LocalClipboard> clipboard = s_wasmRequests.value(token);
    if (!clipboard)
        return;
    QMetaObject::invokeMethod(clipboard, [clipboard, token, success, text, reason] {
        if (clipboard)
            clipboard->finish(token, success, text, reason);
    }, Qt::QueuedConnection);
#else
    Q_UNUSED(token);
    Q_UNUSED(success);
    Q_UNUSED(text);
    Q_UNUSED(reason);
#endif
}

#ifdef __EMSCRIPTEN__
extern "C" EMSCRIPTEN_KEEPALIVE void decenzaLocalClipboardComplete(
    unsigned int token, int success, char* textUtf8, char* reasonUtf8)
{
    const QString text = textUtf8 ? QString::fromUtf8(textUtf8) : QString();
    const QString reason = reasonUtf8 ? QString::fromUtf8(reasonUtf8) : QString();
    std::free(textUtf8);
    std::free(reasonUtf8);
    LocalClipboard::completeWasmRequest(token, success != 0, text, reason);
}
#endif
