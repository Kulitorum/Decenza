#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <array>
#include <optional>

struct HdsFirmwareRelease {
    QString version;
    QString minFromVersion;
    QString model;
    QString releaseNotesUrl;
};

// An advisory view of the public catalog the HDS itself later downloads and
// verifies. This class deliberately models only the compatibility information
// Decenza can identify; the scale remains the final authority for installation.
class HdsFirmwareCatalog {
public:
    static std::optional<HdsFirmwareCatalog> fromJson(const QByteArray& data, QString* error = nullptr);

    std::optional<HdsFirmwareRelease> newestEligibleRelease(const QString& installedVersion,
                                                             const QString& model = QStringLiteral("hds")) const;
    const QList<HdsFirmwareRelease>& releases() const { return m_releases; }

    static int compareVersions(const QString& left, const QString& right);

    // The one grammar for an HDS firmware version, and the only place the
    // component bound lives. Header-inline so the transports can reach it
    // without linking the catalog.
    //
    // Tolerant in exactly the two ways the firmware itself is: a leading `v` is
    // optional (openscale pullOtaParseTargetVersion strips `v`/`V`), and a
    // prerelease suffix is ignored rather than rejected, because the firmware
    // orders releases on the numeric prefix alone
    // (openscale pullOtaCompareVersionPrefixes). That second rule is why an
    // installed "3.1.14-preview.1" compares correctly without anyone trimming
    // it first, and so why the WiFi driver can report the version exactly as
    // the discovery list shows it.
    //
    // 127 is the largest value a payload byte can carry
    // (openscale HDS_OTA_TARGET_MAX_COMPONENT). Rejected rather than clamped:
    // a clamp would turn a version the user was shown into a different,
    // installable release.
    static constexpr int MaxVersionComponent = 127;

    static std::optional<std::array<int, 3>> parseVersion(const QString& version)
    {
        QString text = version.trimmed();
        if (text.startsWith(QLatin1Char('v')) || text.startsWith(QLatin1Char('V')))
            text = text.mid(1);
        text = text.section(QLatin1Char('-'), 0, 0);   // drop any prerelease suffix

        const QStringList parts = text.split(QLatin1Char('.'));
        if (parts.size() != 3)
            return std::nullopt;

        std::array<int, 3> components{};
        for (int i = 0; i < 3; ++i) {
            bool ok = false;
            const int value = parts.at(i).toInt(&ok);
            if (!ok || value < 0 || value > MaxVersionComponent)
                return std::nullopt;
            components[size_t(i)] = value;
        }
        return components;
    }

    // The canonical wire form. The scale's own target parser requires exactly
    // three numeric components and nothing after them, so a release is sent as
    // this rather than as whatever string the manifest happened to carry.
    static QString canonicalVersion(const std::array<int, 3>& components)
    {
        return QStringLiteral("%1.%2.%3")
            .arg(components[0]).arg(components[1]).arg(components[2]);
    }

private:
    QList<HdsFirmwareRelease> m_releases;
};
