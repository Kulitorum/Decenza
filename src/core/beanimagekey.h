#pragma once
#include <QByteArray>
#include <QUrl>
#include <QString>
namespace BeanImageKey {
inline QString forBag(qint64 bagId, const QString& canonicalId)
{
    if (!canonicalId.isEmpty()) return canonicalId;
    return bagId > 0 ? QStringLiteral("bag-%1").arg(bagId) : QString();
}

inline QByteArray encodePathSegment(const QString& key)
{
    return QUrl::toPercentEncoding(key);
}

// Decode only one encoded path component, then apply the same traversal and
// separator restrictions used by BeanBaseClient's image-cache filename.
inline bool decodePathSegment(const QByteArray& encoded, QString* key)
{
    if (!key || encoded.isEmpty() || encoded.size() > 1536)
        return false;
    const auto isHex = [](uchar byte) {
        return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f')
            || (byte >= 'A' && byte <= 'F');
    };
    for (qsizetype i = 0; i < encoded.size(); ++i) {
        const uchar byte = static_cast<uchar>(encoded.at(i));
        if (byte == '%') {
            if (i + 2 >= encoded.size() || !isHex(static_cast<uchar>(encoded.at(i + 1)))
                || !isHex(static_cast<uchar>(encoded.at(i + 2))))
                return false;
            i += 2;
        } else if (!((byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z')
                     || (byte >= '0' && byte <= '9') || byte == '-' || byte == '.'
                     || byte == '_' || byte == '~')) {
            return false;
        }
    }

    const QString decoded = QUrl::fromPercentEncoding(encoded);
    if (decoded.isEmpty() || decoded.size() > 512 || decoded.contains(QLatin1Char('/'))
        || decoded.contains(QLatin1Char('\\')) || decoded.contains(QLatin1String("..")))
        return false;
    for (const QChar character : decoded) {
        if (character.isNull() || character.category() == QChar::Other_Control)
            return false;
    }
    *key = decoded;
    return true;
}

// Decode the exact native session image route. BeanThumbnail uses a numeric
// version query to invalidate Qt's image cache after a cached image changes;
// no other query parameters are part of this endpoint.
inline bool decodeSessionImagePath(const QString& path, QString* key)
{
    static const QString prefix = QStringLiteral("/api/session/bean-image/");
    if (!key || !path.startsWith(prefix))
        return false;

    const QString target = path.mid(prefix.size());
    const qsizetype queryAt = target.indexOf(QLatin1Char('?'));
    const QString encoded = queryAt < 0 ? target : target.left(queryAt);
    if (queryAt >= 0) {
        const QString query = target.mid(queryAt + 1);
        if (query.size() < 3 || query.size() > 12 || !query.startsWith(QLatin1String("v=")))
            return false;
        for (qsizetype i = 2; i < query.size(); ++i) {
            if (query.at(i) < QLatin1Char('0') || query.at(i) > QLatin1Char('9'))
                return false;
        }
    }

    // The path-segment decoder accepts only unreserved ASCII and well-formed
    // percent escapes, including its traversal/separator checks.
    const QByteArray encodedBytes = encoded.toLatin1();
    if (QString::fromLatin1(encodedBytes) != encoded)
        return false;
    return decodePathSegment(encodedBytes, key);
}
}
