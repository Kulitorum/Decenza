#pragma once

#include <QMetaType>
#include <QSet>
#include <QUuid>
#include <QVariantMap>
#include <cmath>

namespace ConversationSessionRequest {

inline constexpr qsizetype MaxShotPayloadBytes = 64 * 1024;

inline bool isCanonicalUuid(const QString& value)
{
    if (value.size() != 36)
        return false;
    const QUuid uuid(value);
    return !uuid.isNull() && uuid.toString(QUuid::WithoutBraces) == value;
}

inline bool validStoredShotOverrides(const QVariantMap& values)
{
    static const QSet<QString> allowed = {
        QStringLiteral("tasteBalance"), QStringLiteral("tasteBody"),
        QStringLiteral("enjoyment0to100")
    };
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if (!allowed.contains(it.key()))
            return false;
        if (it.key() == QLatin1String("tasteBalance") || it.key() == QLatin1String("tasteBody")) {
            if (it.value().metaType().id() != QMetaType::QString
                || it.value().toString().toUtf8().size() > 256)
                return false;
        } else {
            const int type = it.value().metaType().id();
            if (type != QMetaType::Int && type != QMetaType::UInt
                && type != QMetaType::LongLong && type != QMetaType::ULongLong
                && type != QMetaType::Float && type != QMetaType::Double)
                return false;
            bool ok = false;
            const double score = it.value().toDouble(&ok);
            if (!ok || !std::isfinite(score) || std::floor(score) != score
                || score < 0 || score > 100)
                return false;
        }
    }
    return true;
}

} // namespace ConversationSessionRequest
