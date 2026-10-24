#pragma once

#include <QRegularExpression>
#include <QFileInfo>
#include <QString>
#include <QStringList>

#include "servicevalue.h"

namespace FileBundlePolicy {

inline constexpr int MaximumBundleBytes = 128 * 1024 * 1024;
inline constexpr int MaximumChunkBytes = 48 * 1024;
inline constexpr int MaximumLanguageJsonBytes = 8 * 1024 * 1024;
inline constexpr int MaximumLogBytes = 4 * 1024 * 1024;
inline constexpr int MaximumPersonalMediaBytes = 500 * 1024 * 1024;
inline constexpr int MaximumApkBytes = 500 * 1024 * 1024;
inline constexpr qint64 MaximumBackupBytes = ServiceValue::MaximumExactInteger;
inline constexpr int IdleTimeoutMs = 5 * 60 * 1000;

inline int maximumUploadFiles(const QString& kind)
{
    if (kind == QLatin1String("shotZip") || kind == QLatin1String("database")
        || kind == QLatin1String("languageJson") || kind == QLatin1String("apkFile")
        || kind == QLatin1String("backupArchive") || kind == QLatin1String("personalMediaFile"))
        return 1;
    if (kind == QLatin1String("shotFiles") || kind == QLatin1String("tclFiles")
        || kind == QLatin1String("profileFiles"))
        return 256;
    return 0;
}

inline qint64 maximumUploadFileBytes(const QString& kind)
{
    if (kind == QLatin1String("languageJson")) return MaximumLanguageJsonBytes;
    // Preserve the existing server's APK-upload allowance.
    if (kind == QLatin1String("apkFile")) return MaximumApkBytes;
    if (kind == QLatin1String("personalMediaFile")) return MaximumPersonalMediaBytes;
    if (kind == QLatin1String("backupArchive")) return MaximumBackupBytes;
    return maximumUploadFiles(kind) > 0 ? 64 * 1024 * 1024 : 0;
}

inline qint64 maximumUploadBytes(const QString& kind)
{
    return maximumUploadFiles(kind) == 1 ? maximumUploadFileBytes(kind)
                                        : maximumUploadFiles(kind) > 0 ? MaximumBundleBytes : 0;
}

inline bool validUploadFilename(const QString& kind, const QString& name)
{
    static const QRegularExpression unsafe(QStringLiteral("[\\x00-\\x1F\\x7F<>:\"|?*]"));
    if (name.isEmpty() || name.size() > 255 || QFileInfo(name).fileName() != name
        || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))
        || name.contains(unsafe))
        return false;
    const QString suffix = QFileInfo(name).suffix().toLower();
    if (kind == QLatin1String("shotZip")) return suffix == QLatin1String("zip");
    if (kind == QLatin1String("shotFiles")) return suffix == QLatin1String("shot");
    if (kind == QLatin1String("database"))
        return suffix == QLatin1String("db") || suffix == QLatin1String("sqlite")
            || suffix == QLatin1String("sqlite3");
    if (kind == QLatin1String("tclFiles")) return suffix == QLatin1String("tcl");
    if (kind == QLatin1String("profileFiles"))
        return suffix == QLatin1String("json") || suffix == QLatin1String("tcl");
    if (kind == QLatin1String("languageJson")) return suffix == QLatin1String("json");
    if (kind == QLatin1String("apkFile")) return suffix == QLatin1String("apk");
    if (kind == QLatin1String("personalMediaFile"))
        return QStringList{QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
            QStringLiteral("gif"), QStringLiteral("webp"), QStringLiteral("mp4"),
            QStringLiteral("webm"), QStringLiteral("mov")}.contains(suffix);
    if (kind == QLatin1String("backupArchive"))
        return suffix == QLatin1String("zip") || suffix == QLatin1String("dcbackup")
            || suffix == QLatin1String("db");
    return false;
}

inline bool validOpaqueId(const QString& id)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-f0-9-]{36}$"));
    return pattern.match(id).hasMatch();
}

inline bool validOffset(const QVariant& value, qint64 currentOffset, qint64 totalBytes, qint64* offset)
{
    qint64 parsed = -1;
    if (!ServiceValue::boundedInteger64(value, 0, totalBytes, &parsed)
        || parsed != currentOffset)
        return false;
    if (offset) *offset = parsed;
    return true;
}

inline bool validChunk(qint64 offset, qsizetype bytes, qint64 totalBytes)
{
    return offset >= 0 && bytes > 0 && bytes <= MaximumChunkBytes
        && offset <= totalBytes && bytes <= totalBytes - offset;
}

} // namespace FileBundlePolicy
