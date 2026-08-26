#pragma once

#include <QNetworkRequest>
#include <QString>
#include <QUrl>

#include <optional>

struct GitHubRelease {
    QString tag;
    QString body;
};

namespace GitHubReleaseClient {

QNetworkRequest requestForUrl(const QUrl& url);
QNetworkRequest releasesRequest(const QString& repository);
QNetworkRequest releaseRequest(const QString& repository, const QString& tag);
std::optional<GitHubRelease> parseRelease(const QByteArray& data, QString* error = nullptr);

} // namespace GitHubReleaseClient
