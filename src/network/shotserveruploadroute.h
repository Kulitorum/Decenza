#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

// Which POSTs ShotServer streams to a temp file, decided by exact path. A substring
// match on the request line once took POST /api/settings/decent/upload-missing for
// an APK upload and handed its 2-byte body to PackageInstaller.
enum class StreamedUpload { None, Media, BackupRestore, Apk };

inline StreamedUpload streamedUploadKind(const QString& requestLine)
{
    const QStringList parts = requestLine.trimmed().split(QLatin1Char(' '));
    if (parts.size() < 2 || parts.at(0) != QLatin1String("POST")) return StreamedUpload::None;
    const QString path = parts.at(1).section(QLatin1Char('?'), 0, 0);
    if (path == QLatin1String("/upload")) return StreamedUpload::Apk;
    if (path == QLatin1String("/upload/media")) return StreamedUpload::Media;
    if (path == QLatin1String("/api/backup/restore")) return StreamedUpload::BackupRestore;
    return StreamedUpload::None;
}

// An APK is a ZIP archive, so it begins with the ZIP local-file-header signature.
inline bool startsLikeApk(const QByteArray& head)
{
    return head.startsWith(QByteArrayLiteral("PK\x03\x04"));
}
