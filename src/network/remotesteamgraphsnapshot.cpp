#include "remotesteamgraphsnapshot.h"
#include "../core/steamgraphvalidation.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <cmath>
#include <utility>

namespace {
bool exactInteger(const QJsonValue& value, qint64* out)
{
    if (!value.isDouble())
        return false;
    const double n = value.toDouble();
    if (!std::isfinite(n) || std::floor(n) != n || n < 0
        || n > SteamGraphValidation::MaximumExactJsonInteger)
        return false;
    *out = static_cast<qint64>(n);
    return true;
}

bool finite(const QJsonValue& value, double* out)
{
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
        return false;
    *out = value.toDouble();
    return true;
}

QJsonArray encode(const QVector<QPointF>& curve)
{
    QJsonArray result;
    for (const auto& p : curve)
        result.append(QJsonArray{p.x(), p.y()});
    return result;
}

bool decode(const QJsonValue& value, QVector<QPointF>* out, qsizetype limit,
            const QString& label, QString* error)
{
    if (!value.isArray()) {
        *error = label + QStringLiteral(" is not an array");
        return false;
    }
    const QJsonArray array = value.toArray();
    if (array.size() > limit) {
        *error = label + QStringLiteral(" exceeds its point limit");
        return false;
    }
    QVector<QPointF> parsed;
    parsed.reserve(array.size());
    double priorX = -1.0;
    for (const QJsonValue& item : array) {
        if (!item.isArray() || item.toArray().size() != 2) {
            *error = label + QStringLiteral(" contains a malformed point");
            return false;
        }
        const QJsonArray pair = item.toArray();
        double x = 0.0;
        double y = 0.0;
        if (!finite(pair.at(0), &x) || !finite(pair.at(1), &y)
            || x < 0.0 || x < priorX) {
            *error = label + QStringLiteral(" contains an invalid or unordered point");
            return false;
        }
        parsed.append(QPointF(x, y));
        priorX = x;
    }
    *out = std::move(parsed);
    return true;
}

} // namespace

RemoteSteamGraphSnapshot::Result RemoteSteamGraphSnapshot::serialize(const Snapshot& s)
{
    QString error;
    const auto failure = [&s](int status, const QString& code, const QString& detail) {
        QJsonObject errorObject{{QStringLiteral("code"), code},
                          {QStringLiteral("detail"), detail}};
        QJsonObject obj{{QStringLiteral("type"), QStringLiteral("steamGraphSnapshotError")},
                        {QStringLiteral("protocolVersion"), 1},
                        {QStringLiteral("packageId"), s.packageId},
                        {QStringLiteral("hostId"), s.hostId},
                        {QStringLiteral("revision"), static_cast<double>(qMax<qint64>(0, s.revision))},
                        {QStringLiteral("error"), errorObject}};
        const QByteArray body = QJsonDocument(obj).toJson(QJsonDocument::Compact);
        return Result{status, body, false};
    };
    if (s.packageId.isEmpty() || s.hostId.isEmpty() || s.revision < 0
        || s.revision > SteamGraphValidation::MaximumExactJsonInteger
        || !std::isfinite(s.rawTime)
        || s.rawTime < 0.0)
        return failure(422, QStringLiteral("invalid_metadata"),
                       QStringLiteral("Steam graph metadata is invalid"));
    if (!SteamGraphValidation::curve(s.pressure, MaximumSamples,
                                     QStringLiteral("pressure"), &error)
        || !SteamGraphValidation::curve(s.flow, MaximumSamples,
                                        QStringLiteral("flow"), &error)
        || !SteamGraphValidation::curve(s.temperature, MaximumSamples,
                                        QStringLiteral("temperature"), &error)
        || !SteamGraphValidation::curve(s.flowGoalPoints, MaximumGoalPoints,
                                        QStringLiteral("flow goals"), &error)) {
        return failure(error.contains(QStringLiteral("limit")) ? 413 : 422,
                       error.contains(QStringLiteral("limit")) ? QStringLiteral("point_limit")
                                                              : QStringLiteral("invalid_curve"),
                       error);
    }
    if (!SteamGraphValidation::samples(s.rawTime, s.pressure, s.flow,
                                      s.temperature, &error))
        return failure(422, QStringLiteral("invalid_samples"), error);

    QJsonObject graph{{QStringLiteral("rawTime"), s.rawTime},
                      {QStringLiteral("sampleCount"), s.pressure.size()},
                      {QStringLiteral("pressure"), encode(s.pressure)},
                      {QStringLiteral("flow"), encode(s.flow)},
                      {QStringLiteral("temperature"), encode(s.temperature)},
                      {QStringLiteral("flowGoalPoints"), encode(s.flowGoalPoints)}};
    QJsonObject response{{QStringLiteral("type"), QStringLiteral("steamGraphSnapshot")},
                         {QStringLiteral("protocolVersion"), 1},
                         {QStringLiteral("packageId"), s.packageId},
                         {QStringLiteral("hostId"), s.hostId},
                         {QStringLiteral("revision"), static_cast<double>(s.revision)},
                         {QStringLiteral("steamGraph"), graph}};
    const QByteArray body = QJsonDocument(response).toJson(QJsonDocument::Compact);
    if (body.size() > MaximumResponseBytes) {
        return failure(413, QStringLiteral("body_limit"),
                       QStringLiteral("Steam graph snapshot exceeds the 2 MiB limit"));
    }
    return {200, body, true};
}

bool RemoteSteamGraphSnapshot::parse(const QByteArray& body, const QString& packageId,
                                     const QString& hostId, qint64 minimumRevision,
                                     Snapshot* output, QString* error)
{
    if (!output || !error || body.size() > MaximumResponseBytes || minimumRevision < 0) {
        if (error) *error = QStringLiteral("Steam graph snapshot exceeds limits or has invalid request state");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        *error = QStringLiteral("Steam graph response is not valid JSON");
        return false;
    }
    const QJsonObject root = doc.object();
    qint64 revision = -1;
    if (root.value(QStringLiteral("type")).toString() != QStringLiteral("steamGraphSnapshot")
        || root.value(QStringLiteral("protocolVersion")).toInt(-1) != 1
        || root.value(QStringLiteral("packageId")).toString() != packageId
        || root.value(QStringLiteral("hostId")).toString() != hostId
        || !exactInteger(root.value(QStringLiteral("revision")), &revision)
        || revision < minimumRevision) {
        *error = QStringLiteral("Steam graph identity, version, or revision is incompatible");
        return false;
    }
    const QJsonObject graph = root.value(QStringLiteral("steamGraph")).toObject();
    Snapshot parsed;
    parsed.packageId = packageId;
    parsed.hostId = hostId;
    parsed.revision = revision;
    if (!finite(graph.value(QStringLiteral("rawTime")), &parsed.rawTime)
        || parsed.rawTime < 0.0
        || !decode(graph.value(QStringLiteral("pressure")), &parsed.pressure, MaximumSamples,
                   QStringLiteral("pressure"), error)
        || !decode(graph.value(QStringLiteral("flow")), &parsed.flow, MaximumSamples,
                   QStringLiteral("flow"), error)
        || !decode(graph.value(QStringLiteral("temperature")), &parsed.temperature, MaximumSamples,
                   QStringLiteral("temperature"), error)
        || !decode(graph.value(QStringLiteral("flowGoalPoints")), &parsed.flowGoalPoints,
                   MaximumGoalPoints, QStringLiteral("flow goals"), error))
        return false;
    if (!SteamGraphValidation::curve(parsed.pressure, MaximumSamples,
                                     QStringLiteral("pressure"), error)
        || !SteamGraphValidation::curve(parsed.flow, MaximumSamples,
                                        QStringLiteral("flow"), error)
        || !SteamGraphValidation::curve(parsed.temperature, MaximumSamples,
                                        QStringLiteral("temperature"), error)
        || !SteamGraphValidation::curve(parsed.flowGoalPoints, MaximumGoalPoints,
                                        QStringLiteral("flow goals"), error)
        || !SteamGraphValidation::samples(parsed.rawTime, parsed.pressure, parsed.flow,
                                          parsed.temperature, error)) {
        return false;
    }
    qint64 count = -1;
    if (!exactInteger(graph.value(QStringLiteral("sampleCount")), &count)
        || count != parsed.pressure.size()) {
        *error = QStringLiteral("Steam graph sample count does not match its curves");
        return false;
    }
    *output = std::move(parsed);
    return true;
}
