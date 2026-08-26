#include "githubreleaseclient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace {

QNetworkRequest requestForPath(const QString& path)
{
    return GitHubReleaseClient::requestForUrl(
        QUrl(QStringLiteral("https://api.github.com/repos/%1").arg(path)));
}

} // namespace

QNetworkRequest GitHubReleaseClient::requestForUrl(const QUrl& url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Decenza"));
    request.setRawHeader("Accept", "application/vnd.github.v3+json");
    request.setAttribute(QNetworkRequest::ConnectionCacheExpiryTimeoutSecondsAttribute, 0);
    return request;
}

QNetworkRequest GitHubReleaseClient::releasesRequest(const QString& repository)
{
    return requestForPath(repository + QStringLiteral("/releases?per_page=10"));
}

QNetworkRequest GitHubReleaseClient::releaseRequest(const QString& repository, const QString& tag)
{
    return requestForPath(repository + QStringLiteral("/releases/tags/") + tag);
}

std::optional<GitHubRelease> GitHubReleaseClient::parseRelease(const QByteArray& data, QString* error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (!document.isObject()) {
        if (error)
            *error = parseError.error == QJsonParseError::NoError
                ? QStringLiteral("GitHub release response is not an object") : parseError.errorString();
        return std::nullopt;
    }
    const QJsonObject object = document.object();
    const QString tag = object.value(QStringLiteral("tag_name")).toString();
    if (tag.isEmpty()) {
        if (error)
            *error = QStringLiteral("GitHub release response has no tag name");
        return std::nullopt;
    }
    return GitHubRelease{tag, object.value(QStringLiteral("body")).toString()};
}
