#include "flowcalibrationservice.h"

#include "../models/flowcalibrationmath.h"

#include <QJSEngine>
#include <QQmlEngine>
#include <QMetaObject>
#include <QUuid>
#include <QVariantMap>

#include <cmath>
#include <functional>

namespace { QPointer<QRemoteObjectNode> s_flowCalibrationNode; }

FlowCalibrationService* FlowCalibrationService::create(QQmlEngine* engine)
{
    auto* service = new FlowCalibrationService(s_flowCalibrationNode, engine);
    QJSEngine::setObjectOwnership(service, QJSEngine::CppOwnership);
    return service;
}

FlowCalibrationService::FlowCalibrationService(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent)
{
    setNode(node);
}

FlowCalibrationService::~FlowCalibrationService()
{
    if (m_state) disconnect(m_state, nullptr, this, nullptr);
}

void FlowCalibrationService::setPublishedNode(QRemoteObjectNode* node) { s_flowCalibrationNode = node; }
QRemoteObjectNode* FlowCalibrationService::publishedNode() { return s_flowCalibrationNode; }

void FlowCalibrationService::setNode(QRemoteObjectNode* node)
{
    if (!RemoteServiceReplica::replace(this, node, m_node, m_state, m_nodeDestroyed,
                                      [this] { failPending(QStringLiteral("disconnected")); })) return;
    connect(m_state, &QRemoteObjectReplica::stateChanged, this, [this] {
        const QPointer<FlowCalibrationService> guard(this);
        if (!connected()) failPending(QStringLiteral("disconnected"));
        if (guard) emit stateChanged();
    });
    connect(m_state, &QRemoteObjectReplica::notified, this, &FlowCalibrationService::stateChanged);
    connect(m_state, &FlowCalibrationStateReplica::readyChanged, this, &FlowCalibrationService::stateChanged);
    connect(m_state, &FlowCalibrationStateReplica::loadingChanged, this, &FlowCalibrationService::stateChanged);
    connect(m_state, &FlowCalibrationStateReplica::savedMultiplierChanged, this, &FlowCalibrationService::stateChanged);
    connect(m_state, &FlowCalibrationStateReplica::calibrationRevisionChanged, this, &FlowCalibrationService::stateChanged);
    connect(m_state, &FlowCalibrationStateReplica::shotListRevisionChanged, this, &FlowCalibrationService::stateChanged);
    connect(m_state, &FlowCalibrationStateReplica::errorMessageChanged, this, &FlowCalibrationService::stateChanged);
    connect(m_state, &FlowCalibrationStateReplica::operationCompleted, this,
            [this, replica = m_state](const QString& id, const QString& operation, bool success,
                   const QVariantMap& result, const QString& reason) {
        if (m_state != replica) return;
        if (!m_pending.finish(id, operation)) return;
        const QPointer<FlowCalibrationService> guard(this);
        if (success && (operation == QLatin1String("loadRecentShots")
                        || operation == QLatin1String("loadShot"))) {
            m_flowSnapshot = result;
            emit flowSnapshotChanged();
            if (!guard) return;
        }
        emit busyChanged();
        if (guard) emit operationCompleted(id, operation, success, result, reason);
    }, Qt::QueuedConnection);
    emit stateChanged();
}

bool FlowCalibrationService::connected() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, s_flowCalibrationNode);
}
bool FlowCalibrationService::ready() const { return connected() && m_state->ready(); }
bool FlowCalibrationService::loading() const { return connected() && m_state->loading(); }
double FlowCalibrationService::savedMultiplier() const { return connected() ? m_state->savedMultiplier() : 1.0; }
qint64 FlowCalibrationService::calibrationRevision() const { return connected() ? m_state->calibrationRevision() : -1; }
quint64 FlowCalibrationService::shotListRevision() const { return connected() ? m_state->shotListRevision() : 0; }
QString FlowCalibrationService::errorMessage() const
{
    return connected() ? m_state->errorMessage() : QStringLiteral("Flow calibration is unavailable");
}

QString FlowCalibrationService::newRequestId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
}

void FlowCalibrationService::refresh()
{
    if (connected()) m_state->refresh();
}

QString FlowCalibrationService::send(
    const QString& operation,
    const std::function<void(FlowCalibrationStateReplica*, const QString&)>& call,
    const QString& suppliedId)
{
    const QString id = suppliedId.isEmpty() ? newRequestId() : suppliedId;
    const auto fail = [this, id, operation](const QString& reason) {
        QMetaObject::invokeMethod(this, [this, id, operation, reason] {
            emit operationCompleted(id, operation, false, {}, reason);
        }, Qt::QueuedConnection);
    };
    if (id.isEmpty() || m_pending.contains(id)) { fail(QStringLiteral("invalidRequestId")); return id; }
    if (!ready()) { fail(QStringLiteral("unavailable")); return id; }
    if (m_pending.size() >= 32) { fail(QStringLiteral("tooManyRequests")); return id; }
    const QPointer<FlowCalibrationStateReplica> replica = m_state;
    const quint64 ticket = m_pending.insert(id, operation);
    const QPointer<FlowCalibrationService> guard(this);
    emit busyChanged();
    if (!guard) return id;
    m_pending.dispatch(this, id, ticket,
                       [this, replica] { return replica && m_state == replica && ready(); },
                       [replica, call, id] { call(replica, id); },
                       [this, id, operation] {
        const QPointer<FlowCalibrationService> alive(this);
        emit busyChanged();
        if (alive)
            emit operationCompleted(id, operation, false, {}, QStringLiteral("disconnected"));
    });
    return id;
}

QString FlowCalibrationService::loadRecentShots(const QString& requestId)
{
    return send(QStringLiteral("loadRecentShots"), [](auto* state, const QString& id) {
        state->loadRecentShots(id);
    }, requestId);
}

QString FlowCalibrationService::loadShot(int index, quint64 expectedShotListRevision,
                                         const QString& requestId)
{
    return send(QStringLiteral("loadShot"), [index, expectedShotListRevision](auto* state, const QString& id) {
        state->loadShot(id, index, expectedShotListRevision);
    }, requestId);
}

QString FlowCalibrationService::save(double multiplier, qint64 expectedCalibrationRevision,
                                     const QString& requestId)
{
    return send(QStringLiteral("save"), [multiplier, expectedCalibrationRevision](auto* state, const QString& id) {
        state->save(id, multiplier, expectedCalibrationRevision);
    }, requestId);
}

QVariantList FlowCalibrationService::replayFlowData(const QVariantList& original,
                                                     double recordedMultiplier,
                                                     double proposedMultiplier) const
{
    if (!std::isfinite(recordedMultiplier) || !std::isfinite(proposedMultiplier)) return {};
    QVector<QPointF> points;
    points.reserve(original.size());
    for (const QVariant& value : original) {
        const QVariantMap point = value.toMap();
        bool xOk = false, yOk = false;
        const double x = point.value(QStringLiteral("x")).toDouble(&xOk);
        const double y = point.value(QStringLiteral("y")).toDouble(&yOk);
        if (!xOk || !yOk || !std::isfinite(x) || !std::isfinite(y)) return {};
        points.append(QPointF(x, y));
    }
    const QVector<QPointF> replayed = FlowCalibrationMath::replay(points, recordedMultiplier,
                                                                  proposedMultiplier);
    QVariantList result;
    result.reserve(replayed.size());
    for (const QPointF& point : replayed)
        result.append(QVariantMap{{QStringLiteral("x"), point.x()},
                                  {QStringLiteral("y"), point.y()}});
    return result;
}

void FlowCalibrationService::failPending(const QString& reason)
{
    const QPointer<FlowCalibrationService> guard(this);
    m_pending.failAll([this] { emit busyChanged(); },
                      [this, guard, reason](const QString& id, const QString& operation) {
        if (!guard) return;
        QMetaObject::invokeMethod(this, [this, id, operation, reason] {
            emit operationCompleted(id, operation, false, {}, reason);
        }, Qt::QueuedConnection);
    });
}
