#include "steamservice.h"

#include "rep_steamservice_replica.h"
#include "core/steamcalculations.h"
#include "core/steampitcherselection.h"
#include "core/steamgraphvalidation.h"
#include "models/steamdatamodel.h"
#include "network/steamhealthsnapshot.h"

#include <QRemoteObjectNode>
#include <QRemoteObjectPendingCallWatcher>
#include <QMetaObject>
#include <QJsonObject>
#include <QUuid>

#include <climits>
#include <cmath>
#include <utility>

namespace {
QPointer<QRemoteObjectNode> s_publishedNode;

bool readNumber(const QVariant& value, double* output)
{
    bool ok = false;
    const double number = value.toDouble(&ok);
    if (!ok || !std::isfinite(number))
        return false;
    *output = number;
    return true;
}

bool readPoints(const QVariant& value, int maximum, QVector<QPointF>* output)
{
    if (!output || value.metaType().id() != QMetaType::QVariantList)
        return false;
    const QVariantList list = value.toList();
    if (list.size() > maximum)
        return false;
    QVector<QPointF> points;
    points.reserve(list.size());
    for (const QVariant& entry : list) {
        if (entry.metaType().id() != QMetaType::QVariantList)
            return false;
        const QVariantList pair = entry.toList();
        if (pair.size() != 2)
            return false;
        double x = 0.0;
        double y = 0.0;
        if (!readNumber(pair.at(0), &x) || !readNumber(pair.at(1), &y))
            return false;
        points.append(QPointF(x, y));
    }
    *output = std::move(points);
    return true;
}

bool readRevision(const QVariant& value, qint64* output)
{
    bool ok = false;
    const qlonglong revision = value.toLongLong(&ok);
    if (!ok || revision < 0 || revision > SteamGraphValidation::MaximumExactJsonInteger)
        return false;
    *output = revision;
    return true;
}

bool isMachineOperation(const QString& operation)
{
    return operation.startsWith(QStringLiteral("steam-"))
        || operation == QStringLiteral("tare");
}
} // namespace

SteamService::SteamService(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent)
    , m_graph(new SteamDataModel(this))
{
    setNode(node);
}

SteamService::~SteamService()
{
    if (m_state)
        disconnect(m_state, nullptr, this, nullptr);
}

void SteamService::setPublishedNode(QRemoteObjectNode* node)
{
    s_publishedNode = node;
}

QRemoteObjectNode* SteamService::publishedNode()
{
    return s_publishedNode;
}

void SteamService::setNode(QRemoteObjectNode* node)
{
    if (m_node == node && m_state && (node || !m_nodeDestroyedConnection))
        return;
    disconnect(m_nodeDestroyedConnection);
    m_nodeDestroyedConnection = {};
    ++m_generation;
    failPending(QStringLiteral("Steam session disconnected; the operation was not retried."));
    if (m_state) {
        auto* oldReplica = m_state.data();
        disconnect(m_state, nullptr, this, nullptr);
        oldReplica->deleteLater();
    }
    m_state = nullptr;
    m_node = node;
    m_graph->clear();

    if (node) {
        m_nodeDestroyedConnection = connect(node, &QObject::destroyed, this,
                                           [this] { setNode(nullptr); });
        m_state = node->acquire<SteamStateReplica>();
        if (m_state)
            m_state->setParent(this);
    } else {
        m_state = new SteamStateReplica;
        m_state->setParent(this);
    }
    if (m_state) {
        connect(m_state, &QRemoteObjectReplica::stateChanged,
                this, &SteamService::onReplicaStateChanged);
        connect(m_state, &QRemoteObjectReplica::notified,
                this, [this] {
                    emit stateChanged();
                    if (ready())
                        applyGraphSnapshot();
                });
        connect(m_state, &SteamStateReplica::editorRevisionChanged,
                this, &SteamService::stateChanged);
        connect(m_state, &SteamStateReplica::graphSnapshotChanged,
                this, &SteamService::applyGraphSnapshot);
        connect(m_state, &SteamStateReplica::warningSnapshotChanged,
                this, &SteamService::reconcileWarningSnapshot);
        connect(m_state, &SteamStateReplica::hostIdChanged,
                this, &SteamService::reconcileWarningSnapshot);
    }
    emit stateChanged();
    emit readyChanged();
    onReplicaStateChanged();
}

SteamStateReplica* SteamService::state() const
{
    return m_state;
}

bool SteamService::ready() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, s_publishedNode);
}

QString SteamService::editorRevision() const
{
    return ready() ? m_state->editorRevision() : QString();
}

QVariantMap SteamService::getSteamPitcherPreset(int index) const
{
    if (!ready())
        return {};
    return SteamPitcherSelection::presetAt(m_state->steamPitcherPresets(), index);
}

bool SteamService::steamPitcherNameTaken(const QString& name, int ignoreIndex) const
{
    if (!ready())
        return false;
    return SteamCalculations::steamPitcherNameTaken(m_state->steamPitcherPresets(),
                                                    name, ignoreIndex);
}

QString SteamService::request(const QString& operation, const QVariantMap& fields,
                              const QString& expectedRevision)
{
    const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (m_busy) {
        QMetaObject::invokeMethod(this, [this, requestId, operation] {
            emit completed(requestId, operation, false,
                           QStringLiteral("Another Steam request is still pending."), {}, {});
        }, Qt::QueuedConnection);
        return requestId;
    }
    if (!ready()) {
        QMetaObject::invokeMethod(this, [this, requestId, operation] {
            emit completed(requestId, operation, false,
                           QStringLiteral("Steam session is not ready."), {}, {});
        }, Qt::QueuedConnection);
        return requestId;
    }

    QString revision = expectedRevision;
    if (operation == QStringLiteral("steam-editor")) {
        revision = m_state->controlContextId() + QLatin1Char(':')
            + (revision.isEmpty() ? m_state->editorRevision() : revision);
    } else if (revision.isEmpty()) {
        if (operation == QStringLiteral("steam-prepare")
                 || operation == QStringLiteral("steam-select-pitcher"))
            revision = m_state->controlContextId() + QLatin1Char(':')
                + m_state->editorRevision();
        else if (isMachineOperation(operation))
            revision = m_state->controlContextId();
    } else if ((operation == QStringLiteral("steam-prepare")
                || operation == QStringLiteral("steam-select-pitcher"))
               && ready()) {
        // Page controls capture the machine token at click time. Bind the
        // accompanying pitcher selection revision at dispatch time as well.
        revision += QLatin1Char(':') + m_state->editorRevision();
    }

    m_pendingRequestId = requestId;
    m_pendingOperation = operation;
    m_requestGeneration = m_generation;
    setBusy(true);
    setErrorMessage({});
    auto* watcher = new QRemoteObjectPendingCallWatcher(
        m_state->execute(operation, fields, revision), this);
    connect(watcher, &QRemoteObjectPendingCallWatcher::finished, this,
            [this, watcher, requestId, operation] {
                finishRequest(watcher, requestId, operation);
            });
    return requestId;
}

void SteamService::finishRequest(QRemoteObjectPendingCallWatcher* watcher,
                                 const QString& requestId,
                                 const QString& operation)
{
    if (!watcher)
        return;
    const QVariant value = watcher->returnValue();
    watcher->deleteLater();
    const QVariantMap response = value.toMap();
    const bool success = response.value(QStringLiteral("success")).toBool();
    const QString message = response.value(QStringLiteral("message")).toString();
    const QVariantMap result = response.value(QStringLiteral("result")).toMap();
    const QString newRevision = response.value(QStringLiteral("newRevision")).toString();
    const quint64 generation = m_requestGeneration;
    QMetaObject::invokeMethod(this, [this, requestId, operation, success, message,
                                     result, newRevision, generation] {
        if (generation != m_generation || requestId != m_pendingRequestId)
            return;
        m_pendingRequestId.clear();
        m_pendingOperation.clear();
        setBusy(false);
        setErrorMessage(success ? QString() : message);
        emit completed(requestId, operation, success, message, result, newRevision);
    }, Qt::QueuedConnection);
}

void SteamService::onReplicaStateChanged()
{
    if (!ready()) {
        ++m_generation;
        failPending(QStringLiteral("Steam session was lost; the operation was not retried."));
        m_graph->clear();
    } else {
        applyGraphSnapshot();
        reconcileWarningSnapshot();
        setErrorMessage({});
    }
    emit stateChanged();
    emit readyChanged();
}

void SteamService::reconcileWarningSnapshot()
{
    if (!ready() || !m_state || !m_state->isInitialized())
        return;
    SteamHealthSnapshot::Snapshot snapshot;
    QString parseError;
    if (!SteamHealthSnapshot::Snapshot::parse(
            QJsonObject::fromVariantMap(m_state->warningSnapshot()), &snapshot, &parseError)) {
        setErrorMessage(QStringLiteral("The Steam warning snapshot was invalid."));
        return;
    }

    const QString hostId = m_state->hostId();
    if (!m_warningInitialized || hostId != m_warningHostId) {
        m_warningInitialized = true;
        m_warningHostId = hostId;
        m_warningCursor = snapshot.latestEventId;
        emit warningSnapshotChanged();
        return;
    }

    if (!snapshot.events.isEmpty() && m_warningCursor < snapshot.events.first().id - 1) {
        const qint64 missed = snapshot.events.first().id - m_warningCursor - 1;
        emit warningEventsMissed(static_cast<int>(qMin<qint64>(missed, INT_MAX)));
        m_warningCursor = snapshot.events.first().id - 1;
    }
    for (const SteamHealthSnapshot::Warning& event : snapshot.events) {
        if (event.id <= m_warningCursor)
            continue;
        switch (event.kind) {
        case SteamHealthSnapshot::Kind::PressureTooHigh:
            emit pressureTooHigh();
            break;
        case SteamHealthSnapshot::Kind::TemperatureTooHigh:
            emit temperatureTooHigh();
            break;
        case SteamHealthSnapshot::Kind::Descale:
            emit descaleWarning();
            break;
        case SteamHealthSnapshot::Kind::TemperatureWarning:
            emit temperatureWarning(event.message);
            break;
        case SteamHealthSnapshot::Kind::ScaleBuildup:
            emit scaleBuildupWarning(event.message);
            break;
        }
        m_warningCursor = event.id;
    }
    emit warningSnapshotChanged();
}

void SteamService::applyGraphSnapshot()
{
    if (!ready() || !m_state)
        return;
    const QVariantMap snapshot = m_state->graphSnapshot();
    if (snapshot.isEmpty())
        return;
    qint64 revision = 0;
    double rawTime = 0.0;
    QVector<QPointF> pressure;
    QVector<QPointF> flow;
    QVector<QPointF> temperature;
    QVector<QPointF> goals;
    const bool valid = readRevision(snapshot.value(QStringLiteral("revision")), &revision)
        && readNumber(snapshot.value(QStringLiteral("rawTime")), &rawTime)
        && readPoints(snapshot.value(QStringLiteral("pressure")),
                      SteamGraphValidation::MaximumSamples, &pressure)
        && readPoints(snapshot.value(QStringLiteral("flow")),
                      SteamGraphValidation::MaximumSamples, &flow)
        && readPoints(snapshot.value(QStringLiteral("temperature")),
                      SteamGraphValidation::MaximumSamples, &temperature)
        && readPoints(snapshot.value(QStringLiteral("flowGoalPoints")),
                      SteamGraphValidation::MaximumGoalPoints, &goals)
        && m_graph->replaceSnapshot(revision, rawTime, pressure, flow, temperature, goals);
    if (!valid) {
        m_graph->clear();
        setErrorMessage(QStringLiteral("The Steam graph snapshot was invalid."));
    }
}

void SteamService::failPending(const QString& message)
{
    if (m_pendingRequestId.isEmpty())
        return;
    const QString requestId = std::exchange(m_pendingRequestId, {});
    const QString operation = std::exchange(m_pendingOperation, {});
    setBusy(false);
    setErrorMessage(message);
    QMetaObject::invokeMethod(this, [this, requestId, operation, message] {
        emit completed(requestId, operation, false, message, {}, {});
    }, Qt::QueuedConnection);
}

void SteamService::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void SteamService::setErrorMessage(const QString& message)
{
    if (m_errorMessage == message)
        return;
    m_errorMessage = message;
    emit errorMessageChanged();
}
