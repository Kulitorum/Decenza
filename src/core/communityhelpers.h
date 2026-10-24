#pragma once

#include <QString>
#include <QUrl>
#include <QUrlQuery>

// Pure community identity/presentation helpers shared by the host and UI facade.
namespace CommunityHelpers {

inline QString searchKey(const QString& roaster, const QString& coffee,
                         const QString& canonicalId)
{
    return (roaster.trimmed() + QLatin1Char('\n') + coffee.trimmed()
            + QLatin1Char('\n') + canonicalId.trimmed()).toLower();
}

inline QString recipeName(const QString& coffee, const QString& profileTitle)
{
    const QString name = coffee.trimmed();
    return name.isEmpty() ? profileTitle.trimmed()
                          : name + QStringLiteral(" · ") + profileTitle.trimmed();
}

inline QUrl pageUrl(const QString& baseUrl, int minEnjoyment,
                    const QString& roaster, const QString& coffee)
{
    QUrl url(baseUrl + QStringLiteral("/community"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("commit"), QStringLiteral("Search"));
    query.addQueryItem(QStringLiteral("bean_brand"), roaster);
    query.addQueryItem(QStringLiteral("bean_type"), coffee);
    query.addQueryItem(QStringLiteral("min_enjoyment"), QString::number(minEnjoyment));
    url.setQuery(query);
    return url;
}

} // namespace CommunityHelpers
