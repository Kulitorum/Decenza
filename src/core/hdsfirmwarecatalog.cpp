#include "hdsfirmwarecatalog.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace {

std::optional<QList<int>> versionParts(const QString& version)
{
    static const QRegularExpression expression(
        QStringLiteral("^v?(\\d+)\\.(\\d+)\\.(\\d+)$"));
    const QRegularExpressionMatch match = expression.match(version.trimmed());
    if (!match.hasMatch())
        return std::nullopt;

    bool ok = false;
    const quint16 major = match.captured(1).toUShort(&ok);
    if (!ok)
        return std::nullopt;
    const quint16 minor = match.captured(2).toUShort(&ok);
    if (!ok)
        return std::nullopt;
    const quint16 patch = match.captured(3).toUShort(&ok);
    if (!ok)
        return std::nullopt;
    return QList<int>{major, minor, patch};
}

HdsFirmwareRelease releaseFromObject(const QJsonObject& object)
{
    HdsFirmwareRelease release;
    release.version = object.value(QStringLiteral("version")).toString();
    release.minFromVersion = object.value(QStringLiteral("min_from")).toString();
    release.model = object.value(QStringLiteral("model")).toString();
    release.releaseNotesUrl = object.value(QStringLiteral("release_notes_url")).toString();
    return release;
}

} // namespace

std::optional<HdsFirmwareCatalog> HdsFirmwareCatalog::fromJson(const QByteArray& data, QString* error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (!document.isObject()) {
        if (error) {
            *error = parseError.error == QJsonParseError::NoError
                ? QStringLiteral("HDS manifest root is not an object")
                : parseError.errorString();
        }
        return std::nullopt;
    }

    const QJsonObject root = document.object();
    HdsFirmwareCatalog catalog;

    const QJsonValue releasesValue = root.value(QStringLiteral("releases"));
    if (!releasesValue.isUndefined() && !releasesValue.isNull()) {
        if (!releasesValue.isArray()) {
            if (error)
                *error = QStringLiteral("HDS manifest releases is not an array");
            return std::nullopt;
        }
        for (const QJsonValue& value : releasesValue.toArray()) {
            if (!value.isObject())
                continue;
            const HdsFirmwareRelease release = releaseFromObject(value.toObject());
            if (versionParts(release.version) && !release.model.isEmpty())
                catalog.m_releases.append(release);
        }
    } else {
        const HdsFirmwareRelease release = releaseFromObject(root);
        if (versionParts(release.version) && !release.model.isEmpty())
            catalog.m_releases.append(release);
    }

    if (catalog.m_releases.isEmpty()) {
        if (error)
            *error = QStringLiteral("HDS manifest contains no valid releases");
        return std::nullopt;
    }

    return catalog;
}

std::optional<HdsFirmwareRelease> HdsFirmwareCatalog::newestEligibleRelease(
    const QString& installedVersion, const QString& model) const
{
    if (!versionParts(installedVersion))
        return std::nullopt;

    std::optional<HdsFirmwareRelease> newest;
    for (const HdsFirmwareRelease& release : m_releases) {
        if (release.model.compare(model, Qt::CaseInsensitive) != 0
            || compareVersions(release.version, installedVersion) <= 0) {
            continue;
        }
        if (!release.minFromVersion.isEmpty()
            && (!versionParts(release.minFromVersion)
                || compareVersions(installedVersion, release.minFromVersion) < 0)) {
            continue;
        }
        if (!newest || compareVersions(release.version, newest->version) > 0)
            newest = release;
    }
    return newest;
}

int HdsFirmwareCatalog::compareVersions(const QString& left, const QString& right)
{
    const auto leftParts = versionParts(left);
    const auto rightParts = versionParts(right);
    if (!leftParts || !rightParts)
        return 0;

    for (int i = 0; i < leftParts->size(); ++i) {
        if (leftParts->at(i) < rightParts->at(i))
            return -1;
        if (leftParts->at(i) > rightParts->at(i))
            return 1;
    }
    return 0;
}
