#include "hdsfirmwarecatalog.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

// A release the app may OFFER, which is stricter than a version it can compare.
// parseVersion ignores a prerelease suffix so an installed "3.1.14-preview.1"
// orders correctly; a catalog entry carrying one is a different matter — it is
// not a published stable release, and openscale's own manifest generator refuses
// to emit one (tools/generate_release_manifest.py, clean_version). Offering it
// would put a build in front of a user that the publisher did not ship.
//
// Returned canonically, so a stored version is the exact string a transport puts
// on the wire and the exact string the dialog shows.
QString stableCanonicalVersion(const QString& raw)
{
    const QString trimmed = raw.trimmed();
    if (trimmed.contains(QLatin1Char('-')))
        return {};
    const auto parsed = HdsFirmwareCatalog::parseVersion(trimmed);
    return parsed ? HdsFirmwareCatalog::canonicalVersion(*parsed) : QString();
}

HdsFirmwareRelease releaseFromObject(const QJsonObject& object)
{
    HdsFirmwareRelease release;
    release.version = stableCanonicalVersion(object.value(QStringLiteral("version")).toString());
    release.minFromVersion = stableCanonicalVersion(object.value(QStringLiteral("min_from")).toString());
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
            if (!release.version.isEmpty() && !release.model.isEmpty())
                catalog.m_releases.append(release);
        }
    } else {
        const HdsFirmwareRelease release = releaseFromObject(root);
        if (!release.version.isEmpty() && !release.model.isEmpty())
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
    if (!HdsFirmwareCatalog::parseVersion(installedVersion))
        return std::nullopt;

    std::optional<HdsFirmwareRelease> newest;
    for (const HdsFirmwareRelease& release : m_releases) {
        if (release.model.compare(model, Qt::CaseInsensitive) != 0
            || compareVersions(release.version, installedVersion) <= 0) {
            continue;
        }
        if (!release.minFromVersion.isEmpty()
            && (!HdsFirmwareCatalog::parseVersion(release.minFromVersion)
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
    const auto leftParts = HdsFirmwareCatalog::parseVersion(left);
    const auto rightParts = HdsFirmwareCatalog::parseVersion(right);
    if (!leftParts || !rightParts)
        return 0;

    // Fixed-width components, so a plain lexicographic compare is the whole rule.
    if (*leftParts < *rightParts)
        return -1;
    if (*leftParts > *rightParts)
        return 1;
    return 0;
}
