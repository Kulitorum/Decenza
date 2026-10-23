#include "liveshotgraphservicesource.h"

#include "machine/machinestate.h"
#include "models/portalsample.h"
#include "models/shotdatamodel.h"
#include "rendering/fastlinerenderer.h"
#include "controllers/profilemanager.h"

#include <QDataStream>
#include <QMetaObject>
#include <QRegularExpression>
#include <QUuid>
#include <cmath>
#include <limits>
#include <utility>

namespace {
constexpr qsizetype kMaximumGraphPayloadBytes = 1024 * 1024;

QVariant pointValue(const QPointF& point)
{
    if (std::isnan(point.x()) && std::isnan(point.y()))
        return QVariantList{QVariant{}, QVariant{}};
    return QVariant::fromValue(point);
}

QVariantList pointList(const QVector<QPointF>& points)
{
    QVariantList values;
    values.reserve(points.size());
    for (const QPointF& point : points)
        values.append(pointValue(point));
    return values;
}

QVariantMap goals(const ShotDataModel* model)
{
    QVariantList pressure;
    for (const auto& segment : model->pressureGoalSegmentsData())
        pressure.append(QVariant::fromValue(pointList(segment)));
    QVariantList flow;
    for (const auto& segment : model->flowGoalSegmentsData())
        flow.append(QVariant::fromValue(pointList(segment)));
    return {
        {QStringLiteral("pressureGoalPoints"), pressure},
        {QStringLiteral("flowGoalPoints"), flow},
        {QStringLiteral("temperatureGoalPoints"), pointList(model->temperatureGoalData())},
        {QStringLiteral("temperatureMixGoalPoints"), pointList(model->temperatureMixGoalData())}
    };
}

bool goalsWithinLimit(const ShotDataModel* model)
{
    qsizetype pressureCount = 0;
    for (const auto& segment : model->pressureGoalSegmentsData())
        pressureCount += segment.size();
    qsizetype flowCount = 0;
    for (const auto& segment : model->flowGoalSegmentsData())
        flowCount += segment.size();
    return pressureCount <= FastLineRenderer::MAX_POINTS
        && flowCount <= FastLineRenderer::MAX_POINTS
        && model->temperatureGoalData().size() <= FastLineRenderer::MAX_POINTS
        && model->temperatureMixGoalData().size() <= FastLineRenderer::MAX_POINTS;
}

QVariantList markers(const ShotDataModel* model)
{
    return model->phaseMarkersVariant();
}

QVariantMap metadata(const ShotDataModel* model, const ProfileManager* profiles,
                     const MachineState* machineState)
{
    return {
        {QStringLiteral("rawTime"), model->rawTime()},
        {QStringLiteral("maxTime"), model->maxTime()},
        {QStringLiteral("stopTime"), model->stopTime()},
        {QStringLiteral("weightAtStop"), model->weightAtStop()},
        {QStringLiteral("finalWeight"), model->finalWeight()},
        {QStringLiteral("profileTargetWeight"), profiles ? profiles->targetWeight() : 0.0},
        {QStringLiteral("machineTargetWeight"), machineState ? machineState->targetWeight() : 0.0},
        {QStringLiteral("portalEcMin"), model->portalEcMin()},
        {QStringLiteral("portalEcMax"), model->portalEcMax()}
    };
}
} // namespace

NativeLiveShotGraphSource::NativeLiveShotGraphSource(ShotDataModel* model,
                                                     ProfileManager* profiles,
                                                     MachineState* machineState,
                                                     QObject* parent)
    : LiveShotGraphStateSimpleSource(parent)
    , m_model(model)
    , m_profiles(profiles)
    , m_machineState(machineState)
{
    if (m_model) {
        m_pressureGoalCursors.fill(0, m_model->pressureGoalSegmentsData().size());
        m_flowGoalCursors.fill(0, m_model->flowGoalSegmentsData().size());
        m_lastMarkers = markers(m_model);
        m_lastMetadata = metadata(m_model, m_profiles, m_machineState);
        m_lastPortalCount = m_model->portalSampleCount();
        connect(m_model, &ShotDataModel::flushed,
                this, &NativeLiveShotGraphSource::publishModelChanges);
        connect(m_model, &ShotDataModel::cleared,
                this, &NativeLiveShotGraphSource::resetEpoch);
        connect(m_model, &ShotDataModel::weightDataCleared,
                this, &NativeLiveShotGraphSource::resetEpoch);
        connect(m_model, &ShotDataModel::graphCorrectionFinished,
                this, &NativeLiveShotGraphSource::resetEpoch);
        connect(m_model, &ShotDataModel::phaseMarkersChanged,
                this, [this] { publishModelChanges(); });
        connect(m_model, &ShotDataModel::stopTimeChanged,
                this, [this] { publishModelChanges(); });
        connect(m_model, &ShotDataModel::weightAtStopChanged,
                this, [this] { publishModelChanges(); });
        connect(m_model, &ShotDataModel::finalWeightChanged,
                this, [this] { publishModelChanges(); });
        connect(m_model, &ShotDataModel::portalSampleAdded,
                this, [this] { publishModelChanges(); });
    }
    if (m_profiles)
        connect(m_profiles, &ProfileManager::targetWeightChanged,
                this, [this] { publishModelChanges(); });
    if (m_machineState)
        connect(m_machineState, &MachineState::targetWeightChanged,
                this, [this] { publishModelChanges(); });

    if (m_model) {
        m_lastPressure = m_model->pressureData().size();
        m_lastFlow = m_model->flowData().size();
        m_lastTemperature = m_model->temperatureData().size();
        m_lastTemperatureMix = m_model->temperatureMixData().size();
        m_lastWeight = m_model->weightData().size();
        m_lastWeightFlow = m_model->weightFlowRateData().size();
        m_lastResistance = m_model->resistanceData().size();
        m_lastConductance = m_model->conductanceData().size();
        m_lastDarcyResistance = m_model->darcyResistanceData().size();
    }
}

void NativeLiveShotGraphSource::setIdentity(const QString& packageId, const QString& hostId)
{
    if (packageId == this->packageId() && hostId == this->hostId())
        return;
    setPackageId(packageId);
    setHostId(hostId);
    setReady(!packageId.isEmpty() && !hostId.isEmpty() && m_model);
    resetEpoch();
}

void NativeLiveShotGraphSource::requestSnapshot(QString requestId)
{
    static const QRegularExpression uuidPattern(
        QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[1-8][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$"));
    if (!uuidPattern.match(requestId).hasMatch())
        return;

    QString error;
    const QVariantMap snapshot = captureSnapshot(&error);
    if (snapshot.isEmpty() || !payloadWithinLimit(snapshot)) {
        if (error.isEmpty())
            error = QStringLiteral("Live graph snapshot exceeds its size limit.");
        emit snapshotResult(requestId, epoch(), sequence(), {}, error);
        return;
    }
    setReady(true);
    emit snapshotResult(requestId, epoch(), sequence(), snapshot, {});
}

QVariantMap NativeLiveShotGraphSource::captureSnapshot(QString* error) const
{
    if (!m_model || !ready()) {
        if (error) *error = QStringLiteral("Live graph is not ready.");
        return {};
    }
    const auto withinSeriesLimit = [](qsizetype count) {
        return count >= 0 && count <= FastLineRenderer::MAX_POINTS;
    };
    if (!withinSeriesLimit(m_model->pressureData().size())
        || !withinSeriesLimit(m_model->flowData().size())
        || !withinSeriesLimit(m_model->temperatureData().size())
        || !withinSeriesLimit(m_model->temperatureMixData().size())
        || !withinSeriesLimit(m_model->weightData().size())
        || !withinSeriesLimit(m_model->weightFlowRateData().size())
        || !withinSeriesLimit(m_model->resistanceData().size())
        || !withinSeriesLimit(m_model->conductanceData().size())
        || !withinSeriesLimit(m_model->darcyResistanceData().size())
        || !withinSeriesLimit(m_model->portalSampleCount())
        || m_model->phaseMarkersList().size() > 512
        || !goalsWithinLimit(m_model)) {
        if (error) *error = QStringLiteral("Live graph exceeds its point or marker limit.");
        return {};
    }
    double previousPortalTime = -1.0;
    for (const PortalSample& sample : m_model->portalSamples()) {
        if (!PortalSamples::valid(sample.time, sample.ecRaw, sample.temperatureC,
                                  previousPortalTime)) {
            if (error) *error = QStringLiteral("Live graph contains invalid portal samples.");
            return {};
        }
        previousPortalTime = sample.time;
    }

    QVariantMap series{
        {QStringLiteral("pressure"), pointList(m_model->pressureData())},
        {QStringLiteral("flow"), pointList(m_model->flowData())},
        {QStringLiteral("temperature"), pointList(m_model->temperatureData())},
        {QStringLiteral("temperatureMix"), pointList(m_model->temperatureMixData())},
        {QStringLiteral("weight"), pointList(m_model->weightData())},
        {QStringLiteral("weightFlow"), pointList(m_model->weightFlowRateData())},
        {QStringLiteral("resistance"), pointList(m_model->resistanceData())},
        {QStringLiteral("conductance"), pointList(m_model->conductanceData())},
        {QStringLiteral("darcyResistance"), pointList(m_model->darcyResistanceData())}
    };
    QVariantMap snapshot{
        {QStringLiteral("series"), series},
        {QStringLiteral("rawTime"), m_model->rawTime()},
        {QStringLiteral("maxTime"), m_model->maxTime()},
        {QStringLiteral("stopTime"), m_model->stopTime()},
        {QStringLiteral("weightAtStop"), m_model->weightAtStop()},
        {QStringLiteral("finalWeight"), m_model->finalWeight()},
        {QStringLiteral("phaseMarkers"), markers(m_model)},
        {QStringLiteral("portalSamples"), m_model->portalSamplesVariant()},
        {QStringLiteral("portalSampleCount"), m_model->portalSampleCount()},
        {QStringLiteral("portalEcMin"), m_model->portalEcMin()},
        {QStringLiteral("portalEcMax"), m_model->portalEcMax()},
        {QStringLiteral("profileTargetWeight"), m_profiles ? m_profiles->targetWeight() : 0.0},
        {QStringLiteral("machineTargetWeight"), m_machineState ? m_machineState->targetWeight() : 0.0}
    };
    const QVariantMap goalValues = goals(m_model);
    for (auto it = goalValues.cbegin(); it != goalValues.cend(); ++it) {
        if (!withinSeriesLimit(it.value().toList().size())) {
            if (error) *error = QStringLiteral("Live graph goal curve exceeds its point limit.");
            return {};
        }
        snapshot.insert(it.key(), it.value());
    }
    if (!payloadWithinLimit(snapshot)) {
        if (error) *error = QStringLiteral("Live graph snapshot exceeds its size limit.");
        return {};
    }
    return snapshot;
}

bool NativeLiveShotGraphSource::payloadWithinLimit(const QVariantMap& payload) const
{
    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << payload;
    return stream.status() == QDataStream::Ok && encoded.size() <= kMaximumGraphPayloadBytes;
}

void NativeLiveShotGraphSource::resetEpoch()
{
    if (m_resetQueued)
        return;
    m_resetQueued = true;
    QMetaObject::invokeMethod(this, [this] {
        m_resetQueued = false;
        if (epoch() >= std::numeric_limits<qint64>::max() - 1) {
            return;
        }
        setEpoch(epoch() + 1);
        setSequence(0);
        if (m_model) {
            m_lastPressure = m_model->pressureData().size();
            m_lastFlow = m_model->flowData().size();
            m_lastTemperature = m_model->temperatureData().size();
            m_lastTemperatureMix = m_model->temperatureMixData().size();
            m_lastWeight = m_model->weightData().size();
            m_lastWeightFlow = m_model->weightFlowRateData().size();
            m_lastResistance = m_model->resistanceData().size();
            m_lastConductance = m_model->conductanceData().size();
            m_lastDarcyResistance = m_model->darcyResistanceData().size();
            m_lastPortalCount = m_model->portalSampleCount();
            m_pressureGoalCursors.fill(0, m_model->pressureGoalSegmentsData().size());
            const auto& pressureSegments = m_model->pressureGoalSegmentsData();
            for (qsizetype i = 0; i < pressureSegments.size(); ++i)
                m_pressureGoalCursors[i] = pressureSegments.at(i).size();
            m_flowGoalCursors.fill(0, m_model->flowGoalSegmentsData().size());
            const auto& flowSegments = m_model->flowGoalSegmentsData();
            for (qsizetype i = 0; i < flowSegments.size(); ++i)
                m_flowGoalCursors[i] = flowSegments.at(i).size();
            m_temperatureGoalCursor = m_model->temperatureGoalData().size();
            m_temperatureMixGoalCursor = m_model->temperatureMixGoalData().size();
            m_lastMarkers = markers(m_model);
            m_lastMetadata = metadata(m_model, m_profiles, m_machineState);
        }
        QString error;
        const QVariantMap snapshot = captureSnapshot(&error);
        QVariantMap delta{
            {QStringLiteral("type"), QStringLiteral("reset")},
            {QStringLiteral("epoch"), epoch()},
            {QStringLiteral("sequence"), 1},
            {QStringLiteral("snapshot"), snapshot},
            {QStringLiteral("reason"), error}
        };
        if (!snapshot.isEmpty() && payloadWithinLimit(delta)) {
            setReady(true);
            setSequence(1);
            emit graphDelta(delta);
        } else {
            setSequence(1);
            delta.insert(QStringLiteral("snapshot"), QVariantMap{});
            delta.insert(QStringLiteral("reason"), error.isEmpty()
                ? QStringLiteral("Live graph reset exceeds its size limit.") : error);
            emit graphDelta(delta);
        }
    }, Qt::QueuedConnection);
}

void NativeLiveShotGraphSource::publishDelta(QVariantMap fields)
{
    if (!ready())
        return;
    if (sequence() >= std::numeric_limits<qint64>::max() - 1) {
        resetEpoch();
        return;
    }
    const qint64 next = sequence() + 1;
    fields.insert(QStringLiteral("type"), QStringLiteral("append"));
    fields.insert(QStringLiteral("epoch"), epoch());
    fields.insert(QStringLiteral("sequence"), next);
    if (!payloadWithinLimit(fields)) {
        resetEpoch();
        return;
    }
    setSequence(next);
    emit graphDelta(fields);
}

void NativeLiveShotGraphSource::publishModelChanges()
{
    if (!m_model || !ready())
        return;

    const auto append = [](const QVector<QPointF>& points, qsizetype* cursor,
                           QVariantMap* output, const QString& name) -> bool {
        if (points.size() > FastLineRenderer::MAX_POINTS
            || *cursor < 0 || *cursor > points.size())
            return false;
        QVariantList additions;
        additions.reserve(points.size() - *cursor);
        for (qsizetype i = *cursor; i < points.size(); ++i)
            additions.append(QVariant::fromValue(points.at(i)));
        *cursor = points.size();
        if (!additions.isEmpty())
            output->insert(name, additions);
        return true;
    };

    QVariantMap fields;
    QVariantMap added;
    if (!append(m_model->pressureData(), &m_lastPressure, &added, QStringLiteral("pressure"))
        || !append(m_model->flowData(), &m_lastFlow, &added, QStringLiteral("flow"))
        || !append(m_model->temperatureData(), &m_lastTemperature, &added, QStringLiteral("temperature"))
        || !append(m_model->temperatureMixData(), &m_lastTemperatureMix, &added, QStringLiteral("temperatureMix"))
        || !append(m_model->weightData(), &m_lastWeight, &added, QStringLiteral("weight"))
        || !append(m_model->weightFlowRateData(), &m_lastWeightFlow, &added, QStringLiteral("weightFlow"))
        || !append(m_model->resistanceData(), &m_lastResistance, &added, QStringLiteral("resistance"))
        || !append(m_model->conductanceData(), &m_lastConductance, &added, QStringLiteral("conductance"))
        || !append(m_model->darcyResistanceData(), &m_lastDarcyResistance, &added, QStringLiteral("darcyResistance"))) {
        resetEpoch();
        return;
    }
    if (!added.isEmpty())
        fields.insert(QStringLiteral("append"), added);

    QVariantMap goalAppend;
    const auto appendSegments = [](const QVector<QVector<QPointF>>& segments,
                                   QVector<qsizetype>* cursors,
                                   QVariantList* output) -> bool {
        QVariantList additions;
        if (!cursors || segments.size() < cursors->size())
            return false;
        cursors->resize(segments.size());
        qsizetype total = 0;
        for (qsizetype i = 0; i < segments.size(); ++i) {
            const QVector<QPointF>& points = segments.at(i);
            total += points.size();
            const qsizetype cursor = cursors->at(i);
            if (cursor < 0 || cursor > points.size())
                return false;
            if (cursor < points.size()) {
                QVariantList suffix;
                suffix.reserve(points.size() - cursor);
                for (qsizetype j = cursor; j < points.size(); ++j)
                    suffix.append(pointValue(points.at(j)));
                additions.append(QVariantMap{
                    {QStringLiteral("segment"), i},
                    {QStringLiteral("points"), suffix}
                });
            }
            (*cursors)[i] = points.size();
        }
        if (total > FastLineRenderer::MAX_POINTS)
            return false;
        *output = std::move(additions);
        return true;
    };
    QVariantList pressureSuffix;
    QVariantList flowSuffix;
    if (!appendSegments(m_model->pressureGoalSegmentsData(), &m_pressureGoalCursors,
                        &pressureSuffix)
        || !appendSegments(m_model->flowGoalSegmentsData(), &m_flowGoalCursors,
                           &flowSuffix)) {
        resetEpoch();
        return;
    }
    if (!pressureSuffix.isEmpty()) goalAppend.insert(QStringLiteral("pressure"), pressureSuffix);
    if (!flowSuffix.isEmpty()) goalAppend.insert(QStringLiteral("flow"), flowSuffix);
    const auto appendLinearGoal = [](const QVector<QPointF>& points, qsizetype* cursor,
                                     QVariantMap* output, const QString& key) -> bool {
        if (!cursor || points.size() > FastLineRenderer::MAX_POINTS
            || *cursor < 0 || *cursor > points.size()) return false;
        if (*cursor < points.size()) {
            QVariantList suffix;
            suffix.reserve(points.size() - *cursor);
            for (qsizetype i = *cursor; i < points.size(); ++i)
                suffix.append(pointValue(points.at(i)));
            output->insert(key, suffix);
        }
        *cursor = points.size();
        return true;
    };
    if (!appendLinearGoal(m_model->temperatureGoalData(), &m_temperatureGoalCursor,
                          &goalAppend, QStringLiteral("temperature"))
        || !appendLinearGoal(m_model->temperatureMixGoalData(), &m_temperatureMixGoalCursor,
                             &goalAppend, QStringLiteral("temperatureMix"))) {
        resetEpoch();
        return;
    }
    if (!goalAppend.isEmpty())
        fields.insert(QStringLiteral("goalAppend"), goalAppend);

    const QVariantList currentMarkers = markers(m_model);
    if (currentMarkers.size() > 512) {
        resetEpoch();
        return;
    }
    if (currentMarkers != m_lastMarkers) {
        fields.insert(QStringLiteral("phaseMarkers"), currentMarkers);
        m_lastMarkers = currentMarkers;
    }

    const QVector<PortalSample>& currentPortal = m_model->portalSamples();
    if (currentPortal.size() < m_lastPortalCount
        || currentPortal.size() > FastLineRenderer::MAX_POINTS) {
        resetEpoch();
        return;
    }
    if (currentPortal.size() > m_lastPortalCount) {
        QVariantList additions;
        additions.reserve(currentPortal.size() - m_lastPortalCount);
        for (qsizetype i = m_lastPortalCount; i < currentPortal.size(); ++i) {
            const PortalSample& sample = currentPortal.at(i);
            additions.append(QVariantMap{
                {QStringLiteral("time"), sample.time},
                {QStringLiteral("ecRaw"), sample.ecRaw},
                {QStringLiteral("temperatureC"), sample.temperatureC},
                {QStringLiteral("breakBefore"), sample.breakBefore}
            });
        }
        fields.insert(QStringLiteral("portalAppend"), additions);
        fields.insert(QStringLiteral("portalSampleCount"), currentPortal.size());
        fields.insert(QStringLiteral("portalEcMin"), m_model->portalEcMin());
        fields.insert(QStringLiteral("portalEcMax"), m_model->portalEcMax());
        m_lastPortalCount = currentPortal.size();
    }

    const QVariantMap currentMetadata = metadata(m_model, m_profiles, m_machineState);
    for (auto it = currentMetadata.cbegin(); it != currentMetadata.cend(); ++it) {
        if (m_lastMetadata.value(it.key()) != it.value())
            fields.insert(it.key(), it.value());
    }
    m_lastMetadata = currentMetadata;
    if (!fields.isEmpty())
        publishDelta(std::move(fields));
}
