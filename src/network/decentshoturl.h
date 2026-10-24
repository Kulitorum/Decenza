#pragma once

#include "network/decentaccount.h"

#include <QUrl>
#include <QUrlQuery>

// Shared pure URL construction for native and remote QML surfaces.
namespace DecentShotUrl {
inline QString shotViewUrl(const QString& serial, const QString& serverShotId)
{
    if (serial.isEmpty() || serverShotId.isEmpty())
        return {};
    QUrl url(QString::fromLatin1(DecentAccount::kBaseUrl)
             + QString::fromLatin1(DecentAccount::kAccountPath));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("view"), QStringLiteral("chart"));
    query.addQueryItem(QStringLiteral("sn"), serial);
    query.addQueryItem(QStringLiteral("id"), serverShotId);
    url.setQuery(query);
    return url.toString();
}
} // namespace DecentShotUrl
