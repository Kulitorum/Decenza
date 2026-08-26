#pragma once

#include <QList>
#include <QString>

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

private:
    QList<HdsFirmwareRelease> m_releases;
};
