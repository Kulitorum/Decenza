#include "webrequestguard.h"

#include <QHostAddress>
#include <QList>
#include <QSysInfo>

namespace WebRequestGuard {

QString headerValue(const QByteArray& headerBlock, const char* name)
{
    const QByteArray key = QByteArray(name) + ':';
    const QList<QByteArray> lines = headerBlock.split('\n');
    for (const QByteArray& line : lines) {
        if (line.size() > key.size() && line.left(key.size()).compare(key, Qt::CaseInsensitive) == 0)
            return QString::fromUtf8(line.mid(key.size())).trimmed();
    }
    return {};
}

// "host:port" or "[v6]:port" -> "host" / "v6", lower-cased.
static QString hostName(const QString& host)
{
    QString h = host.trimmed().toLower();
    if (h.startsWith(QLatin1Char('['))) {
        const qsizetype close = h.indexOf(QLatin1Char(']'));
        return close < 0 ? h.mid(1) : h.mid(1, close - 1);
    }
    const qsizetype colon = h.indexOf(QLatin1Char(':'));
    return colon < 0 ? h : h.left(colon);
}

bool hostIsOurs(const QString& host)
{
    const QString name = hostName(host);
    if (name.isEmpty() || name == QLatin1String("localhost"))
        return true;
    if (!QHostAddress(name).isNull())
        return true;
    if (!name.contains(QLatin1Char('.')))
        return true;
    // Exactly the machine's name only: "<name>.attacker.example" is the attacker's,
    // and on Android the name is "localhost" (deviceinfo.h), which would pass anything.
    if (name == QSysInfo::machineHostName().toLower())
        return true;
    // Suffixes that never resolve on the public internet, plus the two a home
    // network reaches this machine by: a FRITZ!Box router's names and Tailscale
    // MagicDNS. A public domain the user pointed at the tablet is refused, and
    // the 403 says to open it by IP.
    static const char* const suffixes[] = {
        ".local", ".lan", ".home", ".internal", ".home.arpa", ".localdomain",
        ".intranet", ".private", ".corp", ".fritz.box", ".ts.net"
    };
    for (const char* s : suffixes)
        if (name.endsWith(QLatin1String(s)))
            return true;
    return false;
}

// "https://host:port" -> "host:port". Browsers omit a default port on both
// Origin and Host, so the two compare as written.
static QString originHost(const QString& origin)
{
    const qsizetype at = origin.indexOf(QLatin1String("://"));
    return at < 0 ? origin : origin.mid(at + 3);
}

QString crossSiteReason(const QString& method, const QString& path, const QByteArray& headerBlock)
{
    const QString host = headerValue(headerBlock, "Host");
    if (!hostIsOurs(host))
        return QStringLiteral("Host %1 is not this machine; open the page by its IP address").arg(host);
    const QString origin = headerValue(headerBlock, "Origin");
    if (!origin.isEmpty()) {
        // "null" is an opaque origin (a sandboxed frame, a file:// page): nobody we serve.
        if (origin.compare(QLatin1String("null"), Qt::CaseInsensitive) == 0
            || originHost(origin).compare(host, Qt::CaseInsensitive) != 0)
            return QStringLiteral("Origin %1 is not this server (%2)").arg(origin, host);
        return {};
    }
    const QString site = headerValue(headerBlock, "Sec-Fetch-Site").toLower();
    if (site.isEmpty() || site == QLatin1String("same-origin") || site == QLatin1String("none"))
        return {};
    const bool apiPath = path.startsWith(QLatin1String("/api/")) || path == QLatin1String("/mcp")
                         || path.startsWith(QLatin1String("/mcp/"));
    const bool topLevelNavigation = (method == QLatin1String("GET") || method == QLatin1String("HEAD"))
                                    && headerValue(headerBlock, "Sec-Fetch-Mode").compare(QLatin1String("navigate"), Qt::CaseInsensitive) == 0
                                    && headerValue(headerBlock, "Sec-Fetch-Dest").compare(QLatin1String("document"), Qt::CaseInsensitive) == 0;
    if (topLevelNavigation && !apiPath)
        return {};
    return QStringLiteral("%1 %2 %3 from another site").arg(site, method, path);
}

}
