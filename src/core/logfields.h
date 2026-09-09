#pragma once

#include <QString>
#include <QUrl>

namespace DecenzaLog {

// Identifier values only, never a sanitizer for remote prose or payloads.
inline QString field(QString value)
{
    for (auto& c : value) {
        if (!(c.isLetterOrNumber() || QStringLiteral("._-/:@").contains(c)))
            c = QLatin1Char('_');
    }
    return value.left(128);
}

inline QString safeUrl(const QString& text)
{
    QUrl url(text);
    if (!url.isValid() || (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("http"))
        || url.host().isEmpty())
        return QStringLiteral("invalid-or-non-http-url");
    url.setUserInfo(QString());
    url.setQuery(QString());
    url.setFragment(QString());
    // Keep control characters encoded, so URL identity cannot introduce fields.
    return url.toString(QUrl::FullyEncoded).left(384);
}

} // namespace DecenzaLog
