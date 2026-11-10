#pragma once

#include <QDate>
#include <QRegularExpression>
#include <QString>

namespace BackupFilePolicy {

inline QString catalogFilter()
{
    return QStringLiteral("shots_backup_*.zip");
}

inline bool safeBackupFilename(const QString& filename)
{
    static const QRegularExpression pattern(QStringLiteral("^shots_backup_([0-9]{8})\\.zip$"));
    const auto match = pattern.match(filename);
    return match.hasMatch() && QDate::fromString(match.captured(1), QStringLiteral("yyyyMMdd")).isValid();
}

inline QString filenameFromCatalogRow(const QString& row)
{
    const qsizetype separator = row.lastIndexOf(QLatin1Char('|'));
    if (separator <= 0) return {};
    const QString filename = row.mid(separator + 1);
    return safeBackupFilename(filename) ? filename : QString();
}

} // namespace BackupFilePolicy
