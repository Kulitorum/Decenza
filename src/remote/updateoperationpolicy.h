#pragma once

#include <QMetaType>
#include <QString>
#include <QVariantMap>

namespace UpdateOperationPolicy {
inline bool supported(const QString& operation)
{
    return operation == QLatin1String("checkAll")
        || operation == QLatin1String("checkApp")
        || operation == QLatin1String("downloadAndInstall")
        || operation == QLatin1String("dismissAppUpdate")
        || operation == QLatin1String("checkFirmware")
        || operation == QLatin1String("startFirmware")
        || operation == QLatin1String("retryFirmware")
        || operation == QLatin1String("dismissFirmwareAvailability")
        || operation == QLatin1String("setTranslationUploadEnabled");
}

inline bool validArguments(const QString& operation, const QVariantMap& arguments)
{
    if (operation == QLatin1String("setTranslationUploadEnabled"))
        return arguments.size() == 1
            && arguments.contains(QStringLiteral("enabled"))
            && arguments.value(QStringLiteral("enabled")).metaType().id() == QMetaType::Bool;
    return arguments.isEmpty();
}
} // namespace UpdateOperationPolicy
