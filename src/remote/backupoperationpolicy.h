#pragma once

#include <QMetaType>
#include <QString>
#include <QVariantMap>
#include <QCryptographicHash>
#include <QDate>
#include <QRegularExpression>

#include "servicevalue.h"
#include "filebundlepolicy.h"

namespace BackupOperationPolicy {
inline bool safeBackupFilename(const QString& filename)
{
    static const QRegularExpression pattern(QStringLiteral("^shots_backup_([0-9]{8})\\.(zip|db)$"));
    const auto match = pattern.match(filename);
    return match.hasMatch() && QDate::fromString(match.captured(1), QStringLiteral("yyyyMMdd")).isValid();
}

inline bool downloadableBackupFilename(const QString& filename)
{
    return safeBackupFilename(filename)
        && filename.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive);
}

inline QString backupIdForFilename(const QString& filename)
{
    if (!safeBackupFilename(filename)) return {};
    return QString::fromLatin1(QCryptographicHash::hash(filename.toUtf8(), QCryptographicHash::Sha256).toHex());
}

inline bool isFileOperation(const QString& operation)
{
    return operation == QLatin1String("prepareBackupDownload")
        || operation == QLatin1String("cancelFileOperation")
        || operation == QLatin1String("readBundleChunk")
        || operation == QLatin1String("releaseBundle");
}

inline bool supported(const QString& operation)
{
    return operation == QLatin1String("createBackup")
        || operation == QLatin1String("restoreBackup")
        || operation == QLatin1String("restoreUploadedBackup")
        || operation == QLatin1String("requestStoragePermission")
        || operation == QLatin1String("setBackupHour")
        || isFileOperation(operation);
}

inline bool validBackupId(const QString& id)
{
    if (id.size() != 64) return false;
    for (const QChar c : id) {
        const ushort u = c.unicode();
        if (!((u >= '0' && u <= '9') || (u >= 'a' && u <= 'f'))) return false;
    }
    return true;
}

inline bool validArguments(const QString& operation, const QVariantMap& args)
{
    if (operation == QLatin1String("createBackup")
        || operation == QLatin1String("requestStoragePermission"))
        return args.isEmpty();
    if (operation == QLatin1String("prepareBackupDownload")) {
        return args.size() == 1
            && args.value(QStringLiteral("backupId")).metaType().id() == QMetaType::QString
            && validBackupId(args.value(QStringLiteral("backupId")).toString());
    }
    if (operation == QLatin1String("cancelFileOperation")) {
        const QString requestId = args.value(QStringLiteral("requestId")).toString();
        return args.size() == 1
            && args.value(QStringLiteral("requestId")).metaType().id() == QMetaType::QString
            && !requestId.isEmpty() && requestId.size() <= 128;
    }
    if (operation == QLatin1String("readBundleChunk")) {
        qint64 offset = -1;
        const QString id = args.value(QStringLiteral("bundleId")).toString();
        return args.size() == 2
            && args.value(QStringLiteral("bundleId")).metaType().id() == QMetaType::QString
            && FileBundlePolicy::validOpaqueId(id)
            && ServiceValue::boundedInteger64(args.value(QStringLiteral("offset")), 0,
                                               FileBundlePolicy::MaximumBackupBytes, &offset);
    }
    if (operation == QLatin1String("releaseBundle")) {
        const QString id = args.value(QStringLiteral("bundleId")).toString();
        return args.size() == 1
            && args.value(QStringLiteral("bundleId")).metaType().id() == QMetaType::QString
            && FileBundlePolicy::validOpaqueId(id);
    }
    if (operation == QLatin1String("restoreBackup")
        || operation == QLatin1String("restoreUploadedBackup")) {
        const QString identityKey = operation == QLatin1String("restoreBackup")
            ? QStringLiteral("backupId") : QStringLiteral("transferId");
        if (args.size() != 6 || args.value(identityKey).metaType().id() != QMetaType::QString)
            return false;
        const QString id = args.value(identityKey).toString();
        if (operation == QLatin1String("restoreBackup") ? !validBackupId(id)
                                                        : !FileBundlePolicy::validOpaqueId(id))
            return false;
        for (const QString& key : {QStringLiteral("merge"), QStringLiteral("shots"),
                                   QStringLiteral("settings"), QStringLiteral("profiles"),
                                   QStringLiteral("media")}) {
            if (!args.contains(key) || args.value(key).metaType().id() != QMetaType::Bool)
                return false;
        }
        return true;
    }
    if (operation == QLatin1String("setBackupHour")) {
        int hour = 0;
        int expectedHour = 0;
        return args.size() == 2
            && ServiceValue::boundedInteger(args.value(QStringLiteral("hour")), -1, 23, &hour)
            && ServiceValue::boundedInteger(args.value(QStringLiteral("expectedHour")), -1, 23,
                                            &expectedHour);
    }
    return false;
}
} // namespace BackupOperationPolicy
