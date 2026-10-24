#pragma once

#include <QMetaType>
#include <QString>
#include <QVariantMap>
#include <QUuid>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include "filebundlepolicy.h"
#include "network/shotserveruploadroute.h"

namespace UpdateOperationPolicy {
inline bool supported(const QString& operation)
{
    return operation == QLatin1String("checkAll")
        || operation == QLatin1String("checkApp")
        || operation == QLatin1String("downloadAndInstall")
        || operation == QLatin1String("installLocalApk")
        || operation == QLatin1String("dismissAppUpdate")
        || operation == QLatin1String("checkFirmware")
        || operation == QLatin1String("startFirmware")
        || operation == QLatin1String("retryFirmware")
        || operation == QLatin1String("dismissFirmwareAvailability")
        || operation == QLatin1String("setTranslationUploadEnabled");
}

inline bool validArguments(const QString& operation, const QVariantMap& arguments)
{
    if (operation == QLatin1String("installLocalApk")) {
        const QVariant value = arguments.value(QStringLiteral("transferId"));
        const QString id = value.toString();
        return arguments.size() == 1 && value.metaType().id() == QMetaType::QString
            && id.size() == 36 && !QUuid(id).isNull()
            && QUuid(id).toString(QUuid::WithoutBraces) == id;
    }
    if (operation == QLatin1String("setTranslationUploadEnabled"))
        return arguments.size() == 1
            && arguments.contains(QStringLiteral("enabled"))
            && arguments.value(QStringLiteral("enabled")).metaType().id() == QMetaType::Bool;
    return arguments.isEmpty();
}

// Called on a worker after consuming a completed host-owned upload ticket.
inline QString validatedLocalApkPath(const QString& directoryPath, QString* reason)
{
    reason->clear();
    const QDir directory(directoryPath);
    const QStringList files = directory.entryList(QDir::Files | QDir::NoDotAndDotDot);
    if (files.size() != 1) { *reason = QStringLiteral("invalidTransfer"); return {}; }
    const QFileInfo info(directory.filePath(files.first()));
    if (!info.isFile() || info.isSymLink() || info.suffix().compare(QStringLiteral("apk"), Qt::CaseInsensitive)
        || info.size() <= 0 || info.size() > FileBundlePolicy::MaximumApkBytes) {
        *reason = QStringLiteral("invalidApk"); return {};
    }
    QFile file(info.filePath());
    if (!file.open(QIODevice::ReadOnly) || !startsLikeApk(file.read(4))) {
        *reason = QStringLiteral("invalidApk"); return {};
    }
    return info.filePath();
}
} // namespace UpdateOperationPolicy
