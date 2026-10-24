#include "remoteshotgraphsnapshot.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>
#include <cmath>

namespace {
constexpr qsizetype kMaximumPointsPerCurve = RemoteShotGraphSnapshot::MaximumEntriesPerCurve;
constexpr qsizetype kMaximumPhaseMarkers = RemoteShotGraphSnapshot::MaximumPhaseMarkers;
constexpr qsizetype kMaximumResponseBytes = RemoteShotGraphSnapshot::MaximumResponseBytes;

using Input = RemoteShotGraphSnapshot::Input;
using Result = RemoteShotGraphSnapshot::Result;
constexpr quint64 kMaximumExactJsonInteger = 9007199254740991ULL;

Result errorResult(const Input& input, int status, const QString& code,
                   const QString& detail)
{
    QJsonObject error;
    error.insert(QStringLiteral("code"), code);
    error.insert(QStringLiteral("detail"), detail);

    QJsonObject response;
    response.insert(QStringLiteral("type"), QStringLiteral("graphSnapshotError"));
    response.insert(QStringLiteral("protocolVersion"), 1);
    response.insert(QStringLiteral("hostId"), input.hostId);
    response.insert(QStringLiteral("revision"),
                     static_cast<double>(std::min(input.revision, kMaximumExactJsonInteger)));
    response.insert(QStringLiteral("error"), error);

    const QByteArray body = QJsonDocument(response).toJson(QJsonDocument::Compact);
    return {status, body, false, body.size()};
}

bool validScalar(double value)
{
    return std::isfinite(value);
}

bool withinPointLimit(const QVector<QPointF>& curve)
{
    // The browser backend bounds array length, including null goal separators.
    return curve.size() <= kMaximumPointsPerCurve;
}

QJsonValue encodePoint(const QPointF& point)
{
    if (std::isnan(point.x()) && std::isnan(point.y()))
        return QJsonValue(QJsonValue::Null);
    QJsonArray pair;
    pair.append(point.x());
    pair.append(point.y());
    return pair;
}

QJsonArray encodeCurve(const QVector<QPointF>& curve)
{
    QJsonArray result;
    for (const QPointF& point : curve)
        result.append(encodePoint(point));
    return result;
}

bool validCurve(const QVector<QPointF>& curve, bool allowBreaks)
{
    if (!withinPointLimit(curve))
        return false;
    for (const QPointF& point : curve) {
        const bool xNan = std::isnan(point.x());
        const bool yNan = std::isnan(point.y());
        if (allowBreaks && xNan && yNan)
            continue;
        if (!validScalar(point.x()) || !validScalar(point.y()))
            return false;
    }
    return true;
}

QJsonObject curvesObject(const Input& input)
{
    return {
        {QStringLiteral("pressure"), encodeCurve(input.pressureGoal)},
        {QStringLiteral("flow"), encodeCurve(input.flowGoal)},
        {QStringLiteral("temperature"), encodeCurve(input.temperatureGoal)},
        {QStringLiteral("temperatureMix"), encodeCurve(input.temperatureMixGoal)}
    };
}

} // namespace

RemoteShotGraphSnapshot::Result RemoteShotGraphSnapshot::serialize(const Input& input)
{
    if (input.revision > kMaximumExactJsonInteger) {
        return errorResult(input, 422, QStringLiteral("invalid_revision"),
                           QStringLiteral("Revision exceeds the exact JSON integer range"));
    }
    if (!validScalar(input.rawTime) || !validScalar(input.weightAtStop)
        || !validScalar(input.finalWeight)
        || (input.hasProfileTargetWeight && !validScalar(input.profileTargetWeight))
        || (input.hasMachineTargetWeight && !validScalar(input.machineTargetWeight))) {
        return errorResult(input, 422, QStringLiteral("invalid_metadata"),
                           QStringLiteral("Graph metadata contains a non-finite number"));
    }
    if (input.phaseMarkers.size() > kMaximumPhaseMarkers) {
        return errorResult(input, 413, QStringLiteral("phase_marker_limit"),
                           QStringLiteral("Graph snapshot exceeds the 512 phase-marker limit"));
    }

    const QList<QPair<QString, const QVector<QPointF>*>> curves = {
        {QStringLiteral("pressure"), &input.pressure},
        {QStringLiteral("flow"), &input.flow},
        {QStringLiteral("temperature"), &input.temperature},
        {QStringLiteral("temperatureMix"), &input.temperatureMix},
        {QStringLiteral("weight"), &input.weight},
        {QStringLiteral("weightFlowRate"), &input.weightFlowRate},
        {QStringLiteral("resistance"), &input.resistance},
        {QStringLiteral("conductance"), &input.conductance},
        {QStringLiteral("darcyResistance"), &input.darcyResistance},
        {QStringLiteral("pressureGoal"), &input.pressureGoal},
        {QStringLiteral("flowGoal"), &input.flowGoal},
        {QStringLiteral("temperatureGoal"), &input.temperatureGoal},
        {QStringLiteral("temperatureMixGoal"), &input.temperatureMixGoal}
    };
    for (const auto& [name, curve] : curves) {
        if (!withinPointLimit(*curve)) {
            return errorResult(input, 413, QStringLiteral("point_limit"),
                               QStringLiteral("Curve '%1' exceeds the 6000-point limit").arg(name));
        }
        const bool isGoalCurve = name.endsWith(QStringLiteral("Goal"));
        if (!validCurve(*curve, isGoalCurve)) {
            return errorResult(input, 422, QStringLiteral("invalid_point"),
                               QStringLiteral("Curve '%1' contains an invalid point").arg(name));
        }
    }
    for (const PhaseMarker& marker : input.phaseMarkers) {
        if (!validScalar(marker.time)) {
            return errorResult(input, 422, QStringLiteral("invalid_phase_marker"),
                               QStringLiteral("A phase marker contains a non-finite time"));
        }
    }

    QJsonObject graph;
    graph.insert(QStringLiteral("rawTime"), input.rawTime);
    graph.insert(QStringLiteral("weightAtStop"), input.weightAtStop);
    graph.insert(QStringLiteral("finalWeight"), input.finalWeight);
    if (input.hasProfileTargetWeight)
        graph.insert(QStringLiteral("profileTargetWeight"), input.profileTargetWeight);
    if (input.hasMachineTargetWeight)
        graph.insert(QStringLiteral("machineTargetWeight"), input.machineTargetWeight);

    QJsonArray markers;
    for (const PhaseMarker& marker : input.phaseMarkers) {
        QJsonObject item;
        item.insert(QStringLiteral("time"), marker.time);
        item.insert(QStringLiteral("label"), marker.label);
        item.insert(QStringLiteral("frameNumber"), marker.frameNumber);
        item.insert(QStringLiteral("isFlowMode"), marker.isFlowMode);
        item.insert(QStringLiteral("transitionReason"), marker.transitionReason);
        item.insert(QStringLiteral("transitionSuffix"), marker.transitionSuffix);
        markers.append(item);
    }
    graph.insert(QStringLiteral("phaseMarkers"), markers);

    QJsonObject series;
    series.insert(QStringLiteral("pressure"), encodeCurve(input.pressure));
    series.insert(QStringLiteral("flow"), encodeCurve(input.flow));
    series.insert(QStringLiteral("temperature"), encodeCurve(input.temperature));
    series.insert(QStringLiteral("temperatureMix"), encodeCurve(input.temperatureMix));
    series.insert(QStringLiteral("weight"), encodeCurve(input.weight));
    series.insert(QStringLiteral("weightFlowRate"), encodeCurve(input.weightFlowRate));
    series.insert(QStringLiteral("resistance"), encodeCurve(input.resistance));
    series.insert(QStringLiteral("conductance"), encodeCurve(input.conductance));
    series.insert(QStringLiteral("darcyResistance"), encodeCurve(input.darcyResistance));
    graph.insert(QStringLiteral("series"), series);
    graph.insert(QStringLiteral("goals"), curvesObject(input));

    QJsonObject response;
    response.insert(QStringLiteral("type"), QStringLiteral("graphSnapshot"));
    response.insert(QStringLiteral("protocolVersion"), 1);
    response.insert(QStringLiteral("hostId"), input.hostId);
    response.insert(QStringLiteral("revision"), static_cast<double>(input.revision));
    response.insert(QStringLiteral("graph"), graph);

    const QByteArray body = QJsonDocument(response).toJson(QJsonDocument::Compact);
    if (body.size() > kMaximumResponseBytes) {
        Result result = errorResult(input, 413, QStringLiteral("body_limit"),
                                    QStringLiteral("Graph snapshot exceeds the 2 MiB response limit"));
        result.serializedBodyBytes = body.size();
        return result;
    }
    return {200, body, true, body.size()};
}
