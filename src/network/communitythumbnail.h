#pragma once

#include <QRegularExpression>
#include <QUrl>
#include <QUuid>

namespace CommunityThumbnail {

inline bool isCanonicalEntryId(const QString& entryId)
{
    static const QRegularExpression uuidPattern(
        QStringLiteral("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[1-8][0-9a-fA-F]{3}-[89aAbB][0-9a-fA-F]{3}-[0-9a-fA-F]{12}$"));
    return uuidPattern.match(entryId).hasMatch() && !QUuid(entryId).isNull();
}

inline QString sameOriginUrl(const QString& entryId, bool compact = false)
{
    if (!isCanonicalEntryId(entryId)) return {};
    return QStringLiteral("/api/community/thumbnail?id=") + entryId.toLower()
        + (compact ? QStringLiteral("&compact=1") : QString());
}

inline QString resolvedUrl(const QUrl& origin, const QString& entryId, bool compact = false)
{
    const QString path = sameOriginUrl(entryId, compact);
    if (path.isEmpty() || origin.host().isEmpty()
        || (origin.scheme() != QLatin1String("https") && origin.scheme() != QLatin1String("http")))
        return {};
    return origin.resolved(QUrl(path)).toString(QUrl::FullyEncoded);
}

inline bool hasCanonicalThumbnail(const QString& entryId, const QString& thumbnailUrl,
                                  bool compact)
{
    if (!isCanonicalEntryId(entryId) || thumbnailUrl.isEmpty()) return false;
    const QUrl url(thumbnailUrl);
    const QString suffix = compact ? QStringLiteral("_compact.png") : QStringLiteral("_full.png");
    return url.isValid() && url.scheme() == QLatin1String("https")
        && url.host().compare(QStringLiteral("decenza.coffee"), Qt::CaseInsensitive) == 0
        && (url.port() == -1 || url.port() == 443) && url.userName().isEmpty()
        && url.password().isEmpty() && url.query().isEmpty() && url.fragment().isEmpty()
        && url.path(QUrl::FullyDecoded)
            == QStringLiteral("/library/thumbnails/") + entryId.toLower() + suffix;
}

inline QUrl canonicalUpstreamUrl(const QString& entryId, bool compact)
{
    if (!isCanonicalEntryId(entryId)) return {};
    const QString suffix = compact ? QStringLiteral("compact") : QStringLiteral("full");
    return QUrl(QStringLiteral("https://decenza.coffee/library/thumbnails/%1_%2.png")
                    .arg(entryId.toLower(), suffix));
}

} // namespace CommunityThumbnail
