#include "liveshotgraphservice.h"

#include "rep_liveshotgraphservice_replica.h"
#include "rendering/fastlinerenderer.h"
#include "models/portalsample.h"

#include <QDataStream>
#include <QMetaType>
#include <QRemoteObjectNode>
#include <QUuid>
#include <cmath>
#include <utility>

namespace {
QPointer<QRemoteObjectNode> s_publishedNode;
constexpr qsizetype kMaximumGraphPayloadBytes = 1024 * 1024;
constexpr qsizetype kMaximumBufferedDeltaCount = 64;
constexpr qsizetype kMaximumBufferedDeltaBytes = 256 * 1024;
constexpr qsizetype kMaximumSeriesPoints = FastLineRenderer::MAX_POINTS;
constexpr qsizetype kMaximumMarkers = 512;

const QStringList kSeriesNames{
    QStringLiteral("pressure"), QStringLiteral("flow"), QStringLiteral("temperature"),
    QStringLiteral("temperatureMix"), QStringLiteral("weight"), QStringLiteral("weightFlow"),
    QStringLiteral("resistance"), QStringLiteral("conductance"), QStringLiteral("darcyResistance")
};
const QStringList kSnapshotKeys{
    QStringLiteral("series"), QStringLiteral("rawTime"), QStringLiteral("maxTime"),
    QStringLiteral("stopTime"), QStringLiteral("weightAtStop"), QStringLiteral("finalWeight"),
    QStringLiteral("phaseMarkers"), QStringLiteral("pressureGoalPoints"),
    QStringLiteral("flowGoalPoints"), QStringLiteral("temperatureGoalPoints"),
    QStringLiteral("temperatureMixGoalPoints"), QStringLiteral("portalSamples"),
    QStringLiteral("portalSampleCount"), QStringLiteral("portalEcMin"),
    QStringLiteral("portalEcMax"), QStringLiteral("profileTargetWeight"),
    QStringLiteral("machineTargetWeight")
};

bool finiteNumber(const QVariant& value, double* output)
{
    if (!output || value.metaType().id() == QMetaType::Bool
        || value.metaType().id() == QMetaType::QString)
        return false;
    bool ok = false;
    const double number = value.toDouble(&ok);
    if (!ok || !std::isfinite(number))
        return false;
    *output = number;
    return true;
}

bool integer(const QVariant& value, qint64* output)
{
    const int type = value.metaType().id();
    if (!output || (type != QMetaType::Int && type != QMetaType::UInt
                    && type != QMetaType::LongLong && type != QMetaType::ULongLong))
        return false;
    if (type == QMetaType::ULongLong || type == QMetaType::UInt) {
        bool ok = false;
        const qulonglong number = value.toULongLong(&ok);
        if (!ok || number > static_cast<qulonglong>(std::numeric_limits<qint64>::max()))
            return false;
        *output = static_cast<qint64>(number);
    } else {
        bool ok = false;
        const qlonglong number = value.toLongLong(&ok);
        if (!ok || number < 0)
            return false;
        *output = number;
    }
    return true;
}

bool serializedUnderLimit(const QVariantMap& map, qsizetype maximum)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << map;
    return stream.status() == QDataStream::Ok && bytes.size() <= maximum;
}

bool readPoints(const QVariant& value, qsizetype maximum, QVector<QPointF>* output,
                bool allowNaNSeparators = false)
{
    if (!output || value.metaType().id() != QMetaType::QVariantList)
        return false;
    const QVariantList list = value.toList();
    if (list.size() > maximum)
        return false;
    QVector<QPointF> points;
    points.reserve(list.size());
    for (const QVariant& item : list) {
        QPointF point;
        bool separator = false;
        if (item.metaType().id() == QMetaType::QPointF) {
            point = item.toPointF();
            separator = std::isnan(point.x()) && std::isnan(point.y());
        } else if (allowNaNSeparators
                   && item.metaType().id() == QMetaType::QVariantList) {
            const QVariantList pair = item.toList();
            if (pair.size() != 2 || pair.at(0).isValid() || pair.at(1).isValid())
                return false;
            point = QPointF(std::numeric_limits<double>::quiet_NaN(),
                            std::numeric_limits<double>::quiet_NaN());
            separator = true;
        } else {
            return false;
        }
        if ((!std::isfinite(point.x()) || !std::isfinite(point.y()))
            && !(allowNaNSeparators && separator))
            return false;
        points.append(point);
    }
    *output = std::move(points);
    return true;
}

bool readMarkerList(const QVariant& value)
{
    if (value.metaType().id() != QMetaType::QVariantList)
        return false;
    const QVariantList list = value.toList();
    if (list.size() > kMaximumMarkers)
        return false;
    for (const QVariant& item : list) {
        if (item.metaType().id() != QMetaType::QVariantMap)
            return false;
        const QVariantMap marker = item.toMap();
        double time = 0.0;
        if (!finiteNumber(marker.value(QStringLiteral("time")), &time)
            || marker.value(QStringLiteral("label")).metaType().id() != QMetaType::QString)
            return false;
    }
    return true;
}

bool readGoalSegments(const QVariant& value)
{
    if (value.metaType().id() != QMetaType::QVariantList)
        return false;
    const QVariantList segments = value.toList();
    if (segments.size() > kMaximumMarkers)
        return false;
    qsizetype total = 0;
    for (const QVariant& segmentValue : segments) {
        QVector<QPointF> points;
        if (!readPoints(segmentValue, kMaximumSeriesPoints, &points, true))
            return false;
        total += points.size();
        if (total > kMaximumSeriesPoints)
            return false;
    }
    return true;
}

bool normalizeGoalPoints(QVariantMap* snapshot)
{
    if (!snapshot)
        return false;
    for (const QString& key : {QStringLiteral("pressureGoalPoints"), QStringLiteral("flowGoalPoints")}) {
        const QVariantList segments = snapshot->value(key).toList();
        QVariantList decodedSegments;
        decodedSegments.reserve(segments.size());
        for (const QVariant& segmentValue : segments) {
            QVector<QPointF> points;
            if (!readPoints(segmentValue, kMaximumSeriesPoints, &points, true))
                return false;
            QVariantList decoded;
            decoded.reserve(points.size());
            for (const QPointF& point : points)
                decoded.append(QVariant::fromValue(point));
            decodedSegments.append(QVariant::fromValue(decoded));
        }
        snapshot->insert(key, decodedSegments);
    }
    for (const QString& key : {QStringLiteral("temperatureGoalPoints"),
                               QStringLiteral("temperatureMixGoalPoints")}) {
        QVector<QPointF> points;
        if (!readPoints(snapshot->value(key), kMaximumSeriesPoints, &points, true))
            return false;
        QVariantList decoded;
        decoded.reserve(points.size());
        for (const QPointF& point : points)
            decoded.append(QVariant::fromValue(point));
        snapshot->insert(key, decoded);
    }
    return true;
}

bool knownKeys(const QVariantMap& map, const QStringList& keys)
{
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        if (!keys.contains(it.key()))
            return false;
    }
    return true;
}
} // namespace

LiveShotGraphService::LiveShotGraphService(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent)
{
    setNode(node);
}

LiveShotGraphService::~LiveShotGraphService()
{
    if (m_state)
        disconnect(m_state, nullptr, this, nullptr);
}

QString LiveShotGraphService::packageId() const { return m_packageId; }
QString LiveShotGraphService::hostId() const { return m_hostId; }
double LiveShotGraphService::rawTime() const { return m_graph.value(QStringLiteral("rawTime")).toDouble(); }
double LiveShotGraphService::maxTime() const { return m_graph.value(QStringLiteral("maxTime")).toDouble(); }
double LiveShotGraphService::stopTime() const { return m_graph.value(QStringLiteral("stopTime")).toDouble(); }
double LiveShotGraphService::weightAtStop() const { return m_graph.value(QStringLiteral("weightAtStop")).toDouble(); }
double LiveShotGraphService::finalWeight() const { return m_graph.value(QStringLiteral("finalWeight")).toDouble(); }
QVariantList LiveShotGraphService::phaseMarkers() const { return m_graph.value(QStringLiteral("phaseMarkers")).toList(); }
QVariantList LiveShotGraphService::pressureGoalPoints() const { return m_graph.value(QStringLiteral("pressureGoalPoints")).toList(); }
QVariantList LiveShotGraphService::flowGoalPoints() const { return m_graph.value(QStringLiteral("flowGoalPoints")).toList(); }
QVariantList LiveShotGraphService::temperatureGoalPoints() const { return m_graph.value(QStringLiteral("temperatureGoalPoints")).toList(); }
QVariantList LiveShotGraphService::temperatureMixGoalPoints() const { return m_graph.value(QStringLiteral("temperatureMixGoalPoints")).toList(); }
double LiveShotGraphService::profileTargetWeight() const { return m_graph.value(QStringLiteral("profileTargetWeight")).toDouble(); }
double LiveShotGraphService::machineTargetWeight() const { return m_graph.value(QStringLiteral("machineTargetWeight")).toDouble(); }
QVariantList LiveShotGraphService::portalSamples() const { return m_portalSamples; }
int LiveShotGraphService::portalSampleCount() const { return m_graph.value(QStringLiteral("portalSampleCount")).toInt(); }
double LiveShotGraphService::portalEcMin() const { return m_graph.value(QStringLiteral("portalEcMin")).toDouble(); }
double LiveShotGraphService::portalEcMax() const { return m_graph.value(QStringLiteral("portalEcMax")).toDouble(); }

void LiveShotGraphService::setPublishedNode(QRemoteObjectNode* node) { s_publishedNode = node; }
QRemoteObjectNode* LiveShotGraphService::publishedNode() { return s_publishedNode; }

bool LiveShotGraphService::connected() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, s_publishedNode);
}

void LiveShotGraphService::setNode(QRemoteObjectNode* node)
{
    if (m_node == node && m_state && (node || !m_nodeDestroyed))
        return;
    disconnect(m_nodeDestroyed);
    m_nodeDestroyed = {};
    if (m_state) {
        disconnect(m_state, nullptr, this, nullptr);
        m_state->deleteLater();
    }
    m_node = node;
    m_state = node ? node->acquire<LiveShotGraphStateReplica>() : new LiveShotGraphStateReplica;
    m_state->setParent(this);
    if (node)
        m_nodeDestroyed = connect(node, &QObject::destroyed, this, [this] { setNode(nullptr); });
    connect(m_state, &QRemoteObjectReplica::stateChanged, this, &LiveShotGraphService::changed);
    connect(m_state, &QRemoteObjectReplica::notified, this, &LiveShotGraphService::changed);
    connect(m_state, &LiveShotGraphStateReplica::snapshotResult,
            this, &LiveShotGraphService::receiveSnapshot, Qt::QueuedConnection);
    connect(m_state, &LiveShotGraphStateReplica::graphDelta,
            this, &LiveShotGraphService::receiveDelta, Qt::QueuedConnection);
    clearGraph(QStringLiteral("Live graph is disconnected."));
    m_snapshotRequested = false;
    changed();
}

void LiveShotGraphService::changed()
{
    if (!connected()) {
        if (m_ready || !m_graph.isEmpty())
            clearGraph(QStringLiteral("Live graph is disconnected."));
        m_snapshotRequested = false;
        return;
    }
    const QString package = m_state->packageId();
    const QString host = m_state->hostId();
    if (package.isEmpty() || host.isEmpty() || !m_state->ready()) {
        clearGraph(QStringLiteral("Live graph is not ready on the paired host."));
        return;
    }
    const bool identityChanged = package != m_packageId || host != m_hostId;
    m_packageId = package;
    m_hostId = host;
    if (identityChanged) {
        clearGraph();
        m_snapshotRequested = false;
    }
    if (!m_ready && !m_snapshotRequested)
        requestSnapshot();
    emit graphChanged();
}

void LiveShotGraphService::clearGraph(const QString& error)
{
    m_ready = false;
    m_graph.clear();
    m_series.clear();
    m_portalSamples.clear();
    m_bufferedDeltas.clear();
    m_bufferedBytes = 0;
    m_bufferOverflowed = false;
    m_epoch = -1;
    m_sequence = -1;
    m_errorMessage = error;
    applyRenderers();
    emit graphChanged();
}

QString LiveShotGraphService::newRequestId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
}

void LiveShotGraphService::requestSnapshot()
{
    if (!connected() || !m_state)
        return;
    requestSnapshotFor(newRequestId());
}

void LiveShotGraphService::requestSnapshotFor(const QString& requestId)
{
    if (!connected() || !m_state)
        return;
    if (m_ready) {
        m_ready = false;
        m_bufferedDeltas.clear();
        m_bufferedBytes = 0;
        m_bufferOverflowed = false;
        emit graphChanged();
    }
    m_pendingSnapshotId = requestId;
    m_snapshotRequested = true;
    m_state->requestSnapshot(requestId);
}

bool LiveShotGraphService::parseSnapshot(const QVariantMap& snapshot,
                                         QHash<QString, QVector<QPointF>>* series) const
{
    if (!series || !serializedUnderLimit(snapshot, kMaximumGraphPayloadBytes)
        || snapshot.size() != kSnapshotKeys.size())
        return false;
    for (const QString& key : kSnapshotKeys) {
        if (!snapshot.contains(key))
            return false;
    }
    const QVariantMap wireSeries = snapshot.value(QStringLiteral("series")).toMap();
    if (wireSeries.size() != kSeriesNames.size())
        return false;
    QHash<QString, QVector<QPointF>> parsed;
    for (const QString& name : kSeriesNames) {
        QVector<QPointF> points;
        if (!readPoints(wireSeries.value(name), kMaximumSeriesPoints, &points))
            return false;
        parsed.insert(name, std::move(points));
    }
    for (const QString& key : {QStringLiteral("rawTime"), QStringLiteral("maxTime"),
                               QStringLiteral("stopTime"), QStringLiteral("weightAtStop"),
                               QStringLiteral("finalWeight"), QStringLiteral("portalEcMin"),
                               QStringLiteral("portalEcMax"), QStringLiteral("profileTargetWeight"),
                               QStringLiteral("machineTargetWeight")}) {
        double number = 0.0;
        if (!finiteNumber(snapshot.value(key), &number))
            return false;
    }
    for (const QString& key : {QStringLiteral("pressureGoalPoints"), QStringLiteral("flowGoalPoints"),
                               QStringLiteral("temperatureGoalPoints"), QStringLiteral("temperatureMixGoalPoints")}) {
        if (key == QLatin1String("pressureGoalPoints") || key == QLatin1String("flowGoalPoints")) {
            if (!readGoalSegments(snapshot.value(key)))
                return false;
            continue;
        }
        QVector<QPointF> points;
        if (!readPoints(snapshot.value(key), kMaximumSeriesPoints, &points, true))
            return false;
    }
    if (!readMarkerList(snapshot.value(QStringLiteral("phaseMarkers"))))
        return false;
    const QVariant portalCount = snapshot.value(QStringLiteral("portalSampleCount"));
    qint64 countValue = -1;
    if (!integer(portalCount, &countValue) || countValue > kMaximumSeriesPoints)
        return false;
    const int count = static_cast<int>(countValue);
    if (snapshot.value(QStringLiteral("portalSamples")).metaType().id() != QMetaType::QVariantList)
        return false;
    const QVariantList portals = snapshot.value(QStringLiteral("portalSamples")).toList();
    if (portals.size() != count || portals.size() > kMaximumSeriesPoints)
        return false;
    double previousPortalTime = -1.0;
    for (const QVariant& value : portals) {
        if (value.metaType().id() != QMetaType::QVariantMap)
            return false;
        const QVariantMap sample = value.toMap();
        double time = 0.0, ec = 0.0, temperature = 0.0;
        if (!finiteNumber(sample.value(QStringLiteral("time")), &time)
            || !finiteNumber(sample.value(QStringLiteral("ecRaw")), &ec)
            || !finiteNumber(sample.value(QStringLiteral("temperatureC")), &temperature)
            || !PortalSamples::valid(time, ec, temperature, previousPortalTime)
            || sample.value(QStringLiteral("breakBefore")).metaType().id() != QMetaType::Bool)
            return false;
        previousPortalTime = time;
    }
    *series = std::move(parsed);
    return true;
}

void LiveShotGraphService::receiveSnapshot(const QString& requestId, qint64 epoch,
                                           qint64 sequence, const QVariantMap& snapshot,
                                           const QString& reason)
{
    if (requestId != m_pendingSnapshotId || !connected())
        return;
    m_pendingSnapshotId.clear();
    m_snapshotRequested = false;
    QHash<QString, QVector<QPointF>> parsed;
    if (!reason.isEmpty() || epoch < 0 || sequence < 0 || !parseSnapshot(snapshot, &parsed)) {
        clearGraph(reason.isEmpty() ? QStringLiteral("The host sent an invalid live graph snapshot.") : reason);
        return;
    }
    if (m_state->packageId() != m_packageId || m_state->hostId() != m_hostId) {
        clearGraph(QStringLiteral("The live graph host changed during synchronization."));
        changed();
        return;
    }
    QVariantMap localSnapshot = snapshot;
    if (!normalizeGoalPoints(&localSnapshot)) {
        clearGraph(QStringLiteral("The host sent invalid goal curves in a live graph snapshot."));
        return;
    }
    m_graph = localSnapshot;
    m_portalSamples = localSnapshot.value(QStringLiteral("portalSamples")).toList();
    m_graph.remove(QStringLiteral("portalSamples"));
    m_series = std::move(parsed);
    m_epoch = epoch;
    m_sequence = sequence;
    m_ready = true;
    m_errorMessage.clear();
    applyRenderers();
    replacePortalSamples(portalSamples());

    const QList<QVariantMap> buffered = m_bufferedDeltas;
    m_bufferedDeltas.clear();
    m_bufferedBytes = 0;
    for (const QVariantMap& delta : buffered) {
        qint64 deltaEpoch = -1;
        qint64 deltaSequence = -1;
        if (!integer(delta.value(QStringLiteral("epoch")), &deltaEpoch)
            || !integer(delta.value(QStringLiteral("sequence")), &deltaSequence)) {
            clearGraph(QStringLiteral("The host sent an invalid live graph counter."));
            requestSnapshot();
            return;
        }
        if (deltaEpoch < m_epoch || (deltaEpoch == m_epoch && deltaSequence <= m_sequence))
            continue;
        receiveDelta(delta);
        if (!m_ready)
            return;
    }
    if (m_bufferOverflowed) {
        m_bufferOverflowed = false;
        m_ready = false;
        requestSnapshot();
        emit graphChanged();
        return;
    }
    emit graphChanged();
}

void LiveShotGraphService::receiveDelta(const QVariantMap& delta)
{
    if (!connected())
        return;
    if (!m_ready) {
        if (!serializedUnderLimit(delta, kMaximumBufferedDeltaBytes)
            || m_bufferedDeltas.size() >= kMaximumBufferedDeltaCount
            || m_bufferedBytes > kMaximumBufferedDeltaBytes) {
            m_bufferedDeltas.clear();
            m_bufferedBytes = 0;
            m_bufferOverflowed = true;
            if (!m_snapshotRequested)
                requestSnapshot();
            return;
        }
        QByteArray bytes;
        QDataStream stream(&bytes, QIODevice::WriteOnly);
        stream.setVersion(QDataStream::Qt_6_0);
        stream << delta;
        m_bufferedBytes += bytes.size();
        if (m_bufferedBytes > kMaximumBufferedDeltaBytes) {
            m_bufferedDeltas.clear();
            m_bufferedBytes = 0;
            m_bufferOverflowed = true;
            if (!m_snapshotRequested)
                requestSnapshot();
            return;
        }
        m_bufferedDeltas.append(delta);
        return;
    }

    if (!serializedUnderLimit(delta, kMaximumGraphPayloadBytes)) {
        clearGraph(QStringLiteral("The host sent an oversized live graph update."));
        requestSnapshot();
        return;
    }
    qint64 deltaEpoch = -1;
    qint64 deltaSequence = -1;
    if (!integer(delta.value(QStringLiteral("epoch")), &deltaEpoch)
        || !integer(delta.value(QStringLiteral("sequence")), &deltaSequence)) {
        clearGraph(QStringLiteral("The host sent an invalid live graph counter."));
        requestSnapshot();
        return;
    }
    const QString type = delta.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("reset")) {
        if (deltaEpoch <= m_epoch) {
            m_ready = false;
            m_errorMessage = QStringLiteral("A stale live graph reset was ignored; resynchronizing.");
            emit graphChanged();
            requestSnapshot();
            return;
        }
        const QStringList resetKeys{QStringLiteral("type"), QStringLiteral("epoch"),
                                    QStringLiteral("sequence"), QStringLiteral("snapshot"),
                                    QStringLiteral("reason")};
        const QVariantMap snapshot = delta.value(QStringLiteral("snapshot")).toMap();
        QHash<QString, QVector<QPointF>> parsed;
        if (!knownKeys(delta, resetKeys) || delta.size() != resetKeys.size()
            || deltaSequence != 1 || delta.value(QStringLiteral("reason")).metaType().id() != QMetaType::QString
            || !parseSnapshot(snapshot, &parsed)) {
            clearGraph(QStringLiteral("The host sent an invalid live graph reset."));
            requestSnapshot();
            return;
        }
        QVariantMap localSnapshot = snapshot;
        if (!normalizeGoalPoints(&localSnapshot)) {
            clearGraph(QStringLiteral("The host sent invalid goal curves in a live graph reset."));
            requestSnapshot();
            return;
        }
        m_graph = localSnapshot;
        m_portalSamples = localSnapshot.value(QStringLiteral("portalSamples")).toList();
        m_graph.remove(QStringLiteral("portalSamples"));
        m_series = std::move(parsed);
        m_epoch = deltaEpoch;
        m_sequence = deltaSequence;
        m_errorMessage.clear();
        applyRenderers();
        replacePortalSamples(portalSamples());
        emit graphChanged();
        return;
    }
    if (type != QLatin1String("append") || deltaEpoch != m_epoch
        || deltaSequence != m_sequence + 1 || !applyAppend(delta)) {
        m_ready = false;
        m_errorMessage = QStringLiteral("Live graph updates arrived out of order; resynchronizing.");
        emit graphChanged();
        requestSnapshot();
        return;
    }
    m_sequence = deltaSequence;
    emit graphChanged();
}

bool LiveShotGraphService::applyAppend(const QVariantMap& delta)
{
    const QStringList known{
        QStringLiteral("type"), QStringLiteral("epoch"), QStringLiteral("sequence"),
        QStringLiteral("append"), QStringLiteral("phaseMarkers"),
        QStringLiteral("goalAppend"),
        QStringLiteral("pressureGoalPoints"), QStringLiteral("flowGoalPoints"),
        QStringLiteral("temperatureGoalPoints"), QStringLiteral("temperatureMixGoalPoints"),
        QStringLiteral("portalAppend"), QStringLiteral("portalSampleCount"),
        QStringLiteral("portalEcMin"), QStringLiteral("portalEcMax"),
        QStringLiteral("rawTime"), QStringLiteral("maxTime"), QStringLiteral("stopTime"),
        QStringLiteral("weightAtStop"), QStringLiteral("finalWeight"),
        QStringLiteral("profileTargetWeight"), QStringLiteral("machineTargetWeight")
    };
    if (!knownKeys(delta, known))
        return false;
    QHash<QString, QVector<QPointF>> appendedPoints;
    const QVariantMap appended = delta.value(QStringLiteral("append")).toMap();
    if (delta.contains(QStringLiteral("append"))) {
        if (!knownKeys(appended, kSeriesNames))
            return false;
        for (auto it = appended.cbegin(); it != appended.cend(); ++it) {
            QVector<QPointF> values;
            const QVector<QPointF>& current = m_series[it.key()];
            if (!readPoints(it.value(), kMaximumSeriesPoints, &values)
                || current.size() + values.size() > kMaximumSeriesPoints)
                return false;
            appendedPoints.insert(it.key(), std::move(values));
        }
    }
    QVariantMap graphUpdates;
    for (const QString& key : {QStringLiteral("pressureGoalPoints"), QStringLiteral("flowGoalPoints"),
                               QStringLiteral("temperatureGoalPoints"), QStringLiteral("temperatureMixGoalPoints")}) {
        if (!delta.contains(key)) continue;
        QVariantMap decoded;
        if (key == QLatin1String("pressureGoalPoints") || key == QLatin1String("flowGoalPoints")) {
            if (!readGoalSegments(delta.value(key))) return false;
        } else {
            QVector<QPointF> points;
            if (!readPoints(delta.value(key), kMaximumSeriesPoints, &points, true)) return false;
        }
        // Use the snapshot decoder for replacement curves as well as snapshots;
        // renderers always receive QPointF values, including gap separators.
        for (const QString& goal : {QStringLiteral("pressureGoalPoints"), QStringLiteral("flowGoalPoints"),
                                    QStringLiteral("temperatureGoalPoints"), QStringLiteral("temperatureMixGoalPoints")})
            decoded.insert(goal, goal == key ? delta.value(key) : QVariant(QVariantList{}));
        if (!normalizeGoalPoints(&decoded)) return false;
        graphUpdates.insert(key, decoded.value(key));
    }
    if (delta.contains(QStringLiteral("phaseMarkers"))) {
        if (!readMarkerList(delta.value(QStringLiteral("phaseMarkers")))) return false;
        graphUpdates.insert(QStringLiteral("phaseMarkers"), delta.value(QStringLiteral("phaseMarkers")));
    }
    if (delta.contains(QStringLiteral("goalAppend"))) {
        const QVariantMap goalAppend = delta.value(QStringLiteral("goalAppend")).toMap();
        const QStringList goalKeys{QStringLiteral("pressure"), QStringLiteral("flow"),
                                   QStringLiteral("temperature"), QStringLiteral("temperatureMix")};
        if (goalAppend.isEmpty() || !knownKeys(goalAppend, goalKeys))
            return false;
        for (const QString& key : goalKeys) {
            if (!goalAppend.contains(key))
                continue;
            const QString property = key == QLatin1String("pressure") ? QStringLiteral("pressureGoalPoints")
                : key == QLatin1String("flow") ? QStringLiteral("flowGoalPoints")
                : key == QLatin1String("temperature") ? QStringLiteral("temperatureGoalPoints")
                : QStringLiteral("temperatureMixGoalPoints");
            QVariantList current = m_graph.value(property).toList();
            const QVariant value = goalAppend.value(key);
            if (key == QLatin1String("pressure") || key == QLatin1String("flow")) {
                if (value.metaType().id() != QMetaType::QVariantList)
                    return false;
                const QVariantList segmentAdditions = value.toList();
                qsizetype priorSegment = -1;
                qsizetype total = 0;
                for (const QVariant& segmentValue : current) {
                    QVector<QPointF> segmentPoints;
                    if (!readPoints(segmentValue, kMaximumSeriesPoints, &segmentPoints, true))
                        return false;
                    total += segmentPoints.size();
                }
                for (const QVariant& segmentValue : segmentAdditions) {
                    if (segmentValue.metaType().id() != QMetaType::QVariantMap)
                        return false;
                    const QVariantMap segment = segmentValue.toMap();
                    if (segment.size() != 2 || !segment.contains(QStringLiteral("segment"))
                        || !segment.contains(QStringLiteral("points")))
                        return false;
                    qint64 index = -1;
                    if (!integer(segment.value(QStringLiteral("segment")), &index)
                        || index < priorSegment || index > kMaximumMarkers)
                        return false;
                    priorSegment = index;
                    QVector<QPointF> additionsForSegment;
                    if (!readPoints(segment.value(QStringLiteral("points")), kMaximumSeriesPoints,
                                    &additionsForSegment, true)
                        || additionsForSegment.isEmpty())
                        return false;
                    total += additionsForSegment.size();
                    if (total > kMaximumSeriesPoints)
                        return false;
                    while (current.size() <= index)
                        current.append(QVariant::fromValue(QVariantList{}));
                    if (current.size() > kMaximumMarkers)
                        return false;
                    QVariantList segmentPoints = current.at(index).toList();
                    for (const QPointF& point : additionsForSegment)
                        segmentPoints.append(QVariant::fromValue(point));
                    current[index] = segmentPoints;
                }
            } else {
                QVector<QPointF> additionsForCurve;
                if (!readPoints(value, kMaximumSeriesPoints, &additionsForCurve, true)
                    || current.size() + additionsForCurve.size() > kMaximumSeriesPoints)
                    return false;
                for (const QPointF& point : additionsForCurve)
                    current.append(QVariant::fromValue(point));
            }
            graphUpdates.insert(property, current);
        }
    }
    QVariantList portalAdditions;
    if (delta.contains(QStringLiteral("portalAppend"))) {
        if (delta.value(QStringLiteral("portalAppend")).metaType().id() != QMetaType::QVariantList
            || !delta.contains(QStringLiteral("portalSampleCount"))
            || !delta.contains(QStringLiteral("portalEcMin"))
            || !delta.contains(QStringLiteral("portalEcMax")))
            return false;
        portalAdditions = delta.value(QStringLiteral("portalAppend")).toList();
        if (m_portalSamples.size() + portalAdditions.size() > kMaximumSeriesPoints)
            return false;
        double previousTime = m_portalSamples.isEmpty() ? -1.0
            : m_portalSamples.last().toMap().value(QStringLiteral("time")).toDouble();
        for (const QVariant& value : portalAdditions) {
            if (value.metaType().id() != QMetaType::QVariantMap)
                return false;
            const QVariantMap sample = value.toMap();
            double time = 0.0, ec = 0.0, temperature = 0.0;
            if (!finiteNumber(sample.value(QStringLiteral("time")), &time)
                || !finiteNumber(sample.value(QStringLiteral("ecRaw")), &ec)
                || !finiteNumber(sample.value(QStringLiteral("temperatureC")), &temperature)
                || !PortalSamples::valid(time, ec, temperature, previousTime)
                || sample.value(QStringLiteral("breakBefore")).metaType().id() != QMetaType::Bool)
                return false;
            previousTime = time;
        }
        qint64 countValue = -1;
        if (!integer(delta.value(QStringLiteral("portalSampleCount")), &countValue)
            || countValue > kMaximumSeriesPoints)
            return false;
        const int count = static_cast<int>(countValue);
        if (count != m_portalSamples.size() + portalAdditions.size()) return false;
        graphUpdates.insert(QStringLiteral("portalSampleCount"), count);
    }
    for (const QString& key : {QStringLiteral("rawTime"), QStringLiteral("maxTime"),
                               QStringLiteral("stopTime"), QStringLiteral("weightAtStop"),
                               QStringLiteral("finalWeight"), QStringLiteral("profileTargetWeight"),
                               QStringLiteral("machineTargetWeight"), QStringLiteral("portalEcMin"),
                               QStringLiteral("portalEcMax")}) {
        if (!delta.contains(key)) continue;
        double number = 0.0;
        if (!finiteNumber(delta.value(key), &number)) return false;
        graphUpdates.insert(key, number);
    }
    if (!delta.contains(QStringLiteral("portalAppend"))
        && (delta.contains(QStringLiteral("portalSampleCount"))
            || delta.contains(QStringLiteral("portalEcMin"))
            || delta.contains(QStringLiteral("portalEcMax"))))
        return false;

    // All wire values are validated before touching the live model. Appends then
    // modify only the changed series rather than detaching every full curve on
    // each machine flush.
    for (auto it = appendedPoints.cbegin(); it != appendedPoints.cend(); ++it) {
        m_series[it.key()] += it.value();
        FastLineRenderer* renderer = nullptr;
        if (it.key() == QLatin1String("pressure")) renderer = m_pressure;
        else if (it.key() == QLatin1String("flow")) renderer = m_flow;
        else if (it.key() == QLatin1String("temperature")) renderer = m_temperature;
        else if (it.key() == QLatin1String("weight")) renderer = m_weight;
        else if (it.key() == QLatin1String("weightFlow")) renderer = m_weightFlow;
        else if (it.key() == QLatin1String("resistance")) renderer = m_resistance;
        else if (it.key() == QLatin1String("conductance")) renderer = m_conductance;
        else if (it.key() == QLatin1String("darcyResistance")) renderer = m_darcyResistance;
        else if (it.key() == QLatin1String("temperatureMix")) renderer = m_temperatureMix;
        if (renderer) {
            for (const QPointF& point : it.value())
                renderer->appendPoint(point.x(), point.y());
        }
    }
    for (auto it = graphUpdates.cbegin(); it != graphUpdates.cend(); ++it)
        m_graph.insert(it.key(), it.value());
    m_portalSamples += portalAdditions;
    for (const QVariant& value : portalAdditions) {
        const QVariantMap sample = value.toMap();
        emit portalSampleAdded(sample.value(QStringLiteral("time")).toDouble(),
                               sample.value(QStringLiteral("ecRaw")).toDouble(),
                               sample.value(QStringLiteral("temperatureC")).toDouble(),
                               sample.value(QStringLiteral("breakBefore")).toBool());
    }
    return true;
}

void LiveShotGraphService::setRendererPoints(FastLineRenderer* renderer,
                                             const QVector<QPointF>& points)
{
    if (renderer) renderer->setPoints(points);
}

void LiveShotGraphService::applyRenderers()
{
    const auto values = [this](const QString& key) { return m_series.value(key); };
    setRendererPoints(m_pressure, values(QStringLiteral("pressure")));
    setRendererPoints(m_flow, values(QStringLiteral("flow")));
    setRendererPoints(m_temperature, values(QStringLiteral("temperature")));
    setRendererPoints(m_weight, values(QStringLiteral("weight")));
    setRendererPoints(m_weightFlow, values(QStringLiteral("weightFlow")));
    setRendererPoints(m_resistance, values(QStringLiteral("resistance")));
    setRendererPoints(m_conductance, values(QStringLiteral("conductance")));
    setRendererPoints(m_darcyResistance, values(QStringLiteral("darcyResistance")));
    setRendererPoints(m_temperatureMix, values(QStringLiteral("temperatureMix")));
}

void LiveShotGraphService::replacePortalSamples(const QVariantList& samples)
{
    emit cleared();
    for (const QVariant& value : samples) {
        const QVariantMap sample = value.toMap();
        emit portalSampleAdded(sample.value(QStringLiteral("time")).toDouble(),
                               sample.value(QStringLiteral("ecRaw")).toDouble(),
                               sample.value(QStringLiteral("temperatureC")).toDouble(),
                               sample.value(QStringLiteral("breakBefore")).toBool());
    }
}

void LiveShotGraphService::registerFastSeries(FastLineRenderer* pressure, FastLineRenderer* flow,
                                              FastLineRenderer* temperature, FastLineRenderer* weight,
                                              FastLineRenderer* weightFlow, FastLineRenderer* resistance,
                                              FastLineRenderer* conductance, FastLineRenderer* darcyResistance,
                                              FastLineRenderer* temperatureMix)
{
    m_pressure = pressure;
    m_flow = flow;
    m_temperature = temperature;
    m_weight = weight;
    m_weightFlow = weightFlow;
    m_resistance = resistance;
    m_conductance = conductance;
    m_darcyResistance = darcyResistance;
    m_temperatureMix = temperatureMix;
    applyRenderers();
}
