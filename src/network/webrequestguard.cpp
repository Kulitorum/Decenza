#include "webrequestguard.h"

#include <QList>

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

// "https://host:port" -> "host:port". Browsers omit a default port on both
// Origin and Host, so the two compare as written.
static QString originHost(const QString& origin)
{
    const qsizetype at = origin.indexOf(QLatin1String("://"));
    return at < 0 ? origin : origin.mid(at + 3);
}

QString crossSiteReason(const QString& method, const QString& path, const QByteArray& headerBlock)
{
    const QString origin = headerValue(headerBlock, "Origin");
    if (!origin.isEmpty()) {
        const QString host = headerValue(headerBlock, "Host");
        // "null" is an opaque origin (a sandboxed frame, a file:// page): nobody we serve.
        if (origin.compare(QLatin1String("null"), Qt::CaseInsensitive) == 0
            || originHost(origin).compare(host, Qt::CaseInsensitive) != 0)
            return QStringLiteral("Origin %1 is not this server (%2)").arg(origin, host);
        return {};
    }
    if (headerValue(headerBlock, "Sec-Fetch-Site").compare(QLatin1String("cross-site"), Qt::CaseInsensitive) != 0)
        return {};
    const bool apiPath = path.startsWith(QLatin1String("/api/")) || path == QLatin1String("/mcp")
                         || path.startsWith(QLatin1String("/mcp/"));
    const bool navigation = (method == QLatin1String("GET") || method == QLatin1String("HEAD"))
                            && headerValue(headerBlock, "Sec-Fetch-Mode").compare(QLatin1String("navigate"), Qt::CaseInsensitive) == 0;
    if (navigation && !apiPath)
        return {};
    return QStringLiteral("cross-site %1 %2").arg(method, path);
}

}
