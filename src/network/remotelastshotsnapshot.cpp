#include "remotelastshotsnapshot.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>

#include <utility>

namespace {
constexpr quint64 kMaximumExactJsonInteger = 9007199254740991ULL;

bool projectionWithinBounds(const QVariantMap& projection)
{
    for (auto it = projection.cbegin(); it != projection.cend(); ++it) {
        if (it.value().metaType().id() != QMetaType::QVariantList)
            continue;

        const qsizetype limit = it.key() == QStringLiteral("phases")
            ? RemoteLastShotSnapshot::MaximumPhaseMarkers
            : RemoteLastShotSnapshot::MaximumEntriesPerSeries;
        if (it.value().toList().size() > limit)
            return false;
    }
    return true;
}

QByteArray errorBody(const QString& packageId, const QString& hostId, quint64 revision,
                     const QString& code, const QString& detail)
{
    QJsonObject error{
        {QStringLiteral("code"), code},
        {QStringLiteral("detail"), detail}
    };
    QJsonObject result{
        {QStringLiteral("type"), QStringLiteral("lastShotSnapshotError")},
        {QStringLiteral("protocolVersion"), 1},
        {QStringLiteral("packageId"), packageId},
        {QStringLiteral("hostId"), hostId},
        {QStringLiteral("revision"), static_cast<double>(revision)},
        {QStringLiteral("error"), error}
    };
    return QJsonDocument(result).toJson(QJsonDocument::Compact);
}
} // namespace

RemoteLastShotSnapshot::Result RemoteLastShotSnapshot::serialize(
    const QString& packageId, const QString& hostId, quint64 revision,
    const std::optional<ShotProjection>& shot)
{
    if (revision > kMaximumExactJsonInteger) {
        return {500, errorBody(packageId, hostId, 0, QStringLiteral("unavailable"),
                               QStringLiteral("History revision is out of range"))};
    }
    if (shot && !shot->isValid()) {
        return {500, errorBody(packageId, hostId, revision, QStringLiteral("unavailable"),
                               QStringLiteral("Latest shot could not be loaded"))};
    }

    const QVariantMap shotMap = shot ? shot->toVariantMap() : QVariantMap{};
    if (shot && !projectionWithinBounds(shotMap)) {
        return {413, errorBody(packageId, hostId, revision, QStringLiteral("tooLarge"),
                               QStringLiteral("Shot exceeds the snapshot limits"))};
    }

    QJsonObject result{
        {QStringLiteral("type"), QStringLiteral("lastShotSnapshot")},
        {QStringLiteral("protocolVersion"), 1},
        {QStringLiteral("packageId"), packageId},
        {QStringLiteral("hostId"), hostId},
        {QStringLiteral("revision"), static_cast<double>(revision)},
        {QStringLiteral("shot"), shot ? QJsonValue(QJsonObject::fromVariantMap(shotMap))
                                      : QJsonValue(QJsonValue::Null)}
    };
    QByteArray body = QJsonDocument(result).toJson(QJsonDocument::Compact);
    if (body.size() > MaximumResponseBytes) {
        return {413, errorBody(packageId, hostId, revision, QStringLiteral("tooLarge"),
                               QStringLiteral("Shot exceeds the snapshot limits"))};
    }
    return {200, std::move(body)};
}

RemoteLastShotSnapshot::Result RemoteLastShotSnapshot::error(
    int httpStatus, const QString& packageId, const QString& hostId, quint64 revision,
    const QString& code, const QString& detail)
{
    return {httpStatus, errorBody(packageId, hostId,
                                  qMin(revision, kMaximumExactJsonInteger), code, detail)};
}
