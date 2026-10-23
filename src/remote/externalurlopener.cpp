#include "externalurlopener.h"

#include <QDesktopServices>
#include <QUuid>

#ifdef Q_OS_WASM
#include <emscripten.h>

EM_JS(int, reserveExternalWindow, (const char* reservationId), {
    const id = UTF8ToString(reservationId);
    let handle;
    try {
        handle = window.open("about:blank", "_blank");
    } catch (e) {
        return 0;
    }
    if (!handle) return 0;
    try { handle.opener = null; } catch (e) {}
    if (!Module.decenzaExternalWindows) Module.decenzaExternalWindows = Object.create(null);
    Module.decenzaExternalWindows[id] = handle;
    return 1;
});

EM_JS(int, navigateExternalWindow, (const char* reservationId, const char* targetUrl), {
    const id = UTF8ToString(reservationId);
    const url = UTF8ToString(targetUrl);
    const windows = Module.decenzaExternalWindows;
    const handle = windows ? windows[id] : null;
    if (windows) delete windows[id];
    if (!handle || handle.closed) return 0;
    try {
        const parsed = new URL(url);
        if ((parsed.protocol !== "https:" && parsed.protocol !== "http:") || parsed.username || parsed.password) {
            handle.close();
            return 0;
        }
        handle.location.replace(parsed.href);
        return 1;
    } catch (e) {
        try { handle.close(); } catch (ignored) {}
        return 0;
    }
});

EM_JS(void, closeExternalWindow, (const char* reservationId), {
    const id = UTF8ToString(reservationId);
    const windows = Module.decenzaExternalWindows;
    const handle = windows ? windows[id] : null;
    if (windows) delete windows[id];
    if (handle && !handle.closed) {
        try { handle.close(); } catch (e) {}
    }
});
#endif

ExternalUrlOpener::ExternalUrlOpener(QObject* parent) : QObject(parent) {}

ExternalUrlOpener::~ExternalUrlOpener()
{
    const auto reservations = m_reservations;
    for (const QString& id : reservations) cancel(id);
}

bool ExternalUrlOpener::isSafeExternalUrl(const QUrl& url)
{
    return url.isValid() && (url.scheme() == QLatin1String("https")
                            || url.scheme() == QLatin1String("http"))
        && !url.host().isEmpty() && url.userInfo().isEmpty()
        && url.toString(QUrl::FullyEncoded).size() <= 4096;
}

QString ExternalUrlOpener::reserve()
{
    if (m_reservations.size() >= 8) return {};
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
#ifdef Q_OS_WASM
    const QByteArray encoded = id.toUtf8();
    if (!reserveExternalWindow(encoded.constData())) return {};
#endif
    m_reservations.insert(id);
    return id;
}

bool ExternalUrlOpener::openReserved(const QString& reservationId, const QString& urlText)
{
    if (!m_reservations.contains(reservationId)) return false;
    const QUrl url(urlText, QUrl::StrictMode);
    if (!isSafeExternalUrl(url)) {
        cancel(reservationId);
        return false;
    }
    m_reservations.remove(reservationId);
#ifdef Q_OS_WASM
    const QByteArray id = reservationId.toUtf8();
    const QByteArray target = url.toString(QUrl::FullyEncoded).toUtf8();
    return navigateExternalWindow(id.constData(), target.constData()) != 0;
#else
    return QDesktopServices::openUrl(url);
#endif
}

void ExternalUrlOpener::cancel(const QString& reservationId)
{
    if (!m_reservations.remove(reservationId)) return;
#ifdef Q_OS_WASM
    const QByteArray id = reservationId.toUtf8();
    closeExternalWindow(id.constData());
#endif
}
