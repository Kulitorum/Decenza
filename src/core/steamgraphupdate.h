#pragma once

#include "models/steamdatamodel.h"
#include "core/uiworkstats.h"

namespace SteamGraphUpdate {
inline QVariantList pointList(const QVector<QPointF>& points, qsizetype from = 0) {
    QVariantList values;
    values.reserve(points.size() - from);
    for (qsizetype i = from; i < points.size(); ++i)
        values.append(QVariant::fromValue(QVariantList{points[i].x(), points[i].y()}));
    return values;
}
inline QVariantList goalList(const QVariantList& points) {
    QVariantList values;
    values.reserve(points.size());
    for (const auto& value : points) {
        const auto point = value.toPointF();
        values.append(QVariant::fromValue(QVariantList{point.x(), point.y()}));
    }
    return values;
}
inline QVariantMap capture(const SteamDataModel& graph, qsizetype from = 0,
                           qint64 baseRevision = -1, bool includeGoals = true) {
    UiWorkStats::Span work(UiWorkStats::Kind::SteamPublish);
    QVariantMap values{{QStringLiteral("revision"), graph.revision()},
        {QStringLiteral("rawTime"), graph.rawTime()},
        {QStringLiteral("pressure"), pointList(graph.pressureData(), from)},
        {QStringLiteral("flow"), pointList(graph.flowData(), from)},
        {QStringLiteral("temperature"), pointList(graph.temperatureData(), from)}};
    if (baseRevision >= 0) values.insert(QStringLiteral("baseRevision"), baseRevision);
    if (includeGoals) values.insert(QStringLiteral("flowGoalPoints"), goalList(graph.flowGoalPoints()));
    work.changes = baseRevision < 0;
    work.items = 3 * (graph.pressureData().size() - from)
        + (includeGoals ? graph.flowGoalPoints().size() : 0);
    return values;
}
class Publisher {
public:
    void reset() { m_revision = -1; m_samples = 0; m_goals.clear(); }
    QVariantMap next(const SteamDataModel& graph) {
        if (graph.revision() == m_revision) return {};
        const auto goals = graph.flowGoalPoints();
        const bool append = m_revision >= 0 && graph.sampleCount() >= m_samples;
        const auto update = capture(graph, append ? m_samples : 0,
            append ? m_revision : -1, !append || goals != m_goals);
        m_revision = graph.revision();
        m_samples = graph.sampleCount();
        m_goals = goals;
        return update;
    }
private:
    qint64 m_revision = -1;
    qsizetype m_samples = 0;
    QVariantList m_goals;
};
}
