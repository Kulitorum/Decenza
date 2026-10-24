#pragma once

#include "filebundlepolicy.h"
#include <QMetaType>
#include <QVariantMap>

namespace SystemLogFilePolicy {
inline bool supported(const QString& operation)
{
    return operation == QLatin1String("prepareLogDownload")
        || operation == QLatin1String("cancelFileOperation")
        || operation == QLatin1String("readBundleChunk")
        || operation == QLatin1String("releaseBundle");
}
inline bool validArguments(const QString& operation, const QVariantMap& args)
{
    if (operation == QLatin1String("prepareLogDownload")) {
        const auto format = args.value(QStringLiteral("format"));
        return args.size() == 1 && format.metaType().id() == QMetaType::QString
            && (format.toString() == QLatin1String("log") || format.toString() == QLatin1String("zip"));
    }
    if (operation == QLatin1String("cancelFileOperation")) {
        const auto id = args.value(QStringLiteral("requestId"));
        return args.size() == 1 && id.metaType().id() == QMetaType::QString
            && !id.toString().isEmpty() && id.toString().size() <= 128;
    }
    const auto id = args.value(QStringLiteral("bundleId"));
    if (id.metaType().id() != QMetaType::QString || !FileBundlePolicy::validOpaqueId(id.toString()))
        return false;
    if (operation == QLatin1String("releaseBundle")) return args.size() == 1;
    int offset = -1;
    return operation == QLatin1String("readBundleChunk") && args.size() == 2
        && ServiceValue::boundedInteger(args.value(QStringLiteral("offset")), 0,
                                       FileBundlePolicy::MaximumLogBytes * 2, &offset);
}
}
