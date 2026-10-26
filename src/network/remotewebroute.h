#pragma once

#include "shotserver_webroute.h"
#include "webrequestguard.h"

#include <QByteArray>
#include <QList>
#include <QString>

// Remote Web Access: the web interface published through the Tailscale Funnel.
// One definition of what that route may reach and how a browser proves it was
// admitted, shared by RemoteAccess (which admits and hands connections over) and
// ShotServer (which serves them and re-checks every request).
namespace RemoteWebRoute {

// Set by GET /web/<token>. The __Host- prefix makes the browser enforce
// Secure, Path=/ and no Domain attribute (RFC 6265bis §4.1.3.2).
inline constexpr char kAdmissionCookie[] = "__Host-decenza_remote";
inline constexpr char kEntryPrefix[] = "/web/";
// Admission lifetime; the same as an authenticator session (ShotServer).
inline constexpr int kAdmissionLifetimeDays = 90;

inline QString pathOnly(const QString& rawPath)
{
    qsizetype cut = rawPath.indexOf(QLatin1Char('?'));
    const qsizetype hash = rawPath.indexOf(QLatin1Char('#'));
    if (hash >= 0 && (cut < 0 || hash < cut))
        cut = hash;
    return cut >= 0 ? rawPath.left(cut) : rawPath;
}

// The routes the current web interface calls, taken from its request sites
// (src/webclient, the shared src/remote facades). Deny by default: a path not
// named here is not served remotely, which is what keeps every legacy HTML page,
// asset, bookmark redirect (/index.html, /shots, ...) and legacy-only API off the
// public host. Checked on the RAW path, before ShotServer resolves /legacy/.
inline bool isAllowedPath(const QString& rawPath)
{
    const QString path = pathOnly(rawPath);
    if (path.contains(QLatin1String("..")) || path.contains(QLatin1String("//")))
        return false;
    static const QList<QLatin1String> exact = {
        QLatin1String("/"), QLatin1String("/wasm"),
        // Authenticator sign-in, shown when "Require authenticator code" is on.
        QLatin1String("/auth/login"), QLatin1String("/auth/setup-required"),
        QLatin1String("/api/auth/login"),
        QLatin1String("/api/library/thumbnail"),
        // Diagnostic ZIP downloads.
        QLatin1String("/api/debug/file/zip"),
    };
    for (const QLatin1String& candidate : exact) {
        if (path == candidate)
            return true;
    }
    static const QList<QLatin1String> prefixes = {
        QLatin1String("/wasm/"),           // packaged client assets
        QLatin1String("/api/session/"),    // remote session transport and images
        QLatin1String("/api/screensaver/media/"),
    };
    for (const QLatin1String& prefix : prefixes) {
        if (path.startsWith(prefix))
            return true;
    }
    // The favicon and home-screen icons; still only for an admitted browser.
    return !ShotServerWebRoute::iconResource(path).isEmpty();
}

// The admission cookie's value from a request's header block, or empty.
inline QString admissionId(const QByteArray& headerBlock)
{
    const QString name = QLatin1String(kAdmissionCookie) + QLatin1Char('=');
    const QString cookies = WebRequestGuard::headerValue(headerBlock, "Cookie");
    for (const QString& part : cookies.split(QLatin1Char(';'))) {
        const QString cookie = part.trimmed();
        if (cookie.startsWith(name))
            return cookie.mid(name.size());
    }
    return QString();
}

} // namespace RemoteWebRoute
