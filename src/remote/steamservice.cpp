#include "steamservice.h"

#include "rep_steamservice_replica.h"
#include "core/steamcalculations.h"
#include "core/steampitcherselection.h"
#include "core/steamgraphvalidation.h"
#include "core/uiworkstats.h"
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
    double revision = 0;
    if (value.metaType().id() == QMetaType::Bool || value.metaType().id() == QMetaType::QString
        || !readNumber(value, &revision) || std::trunc(revision) != revision
        || revision < 0 || revision > SteamGraphValidation::MaximumExactJsonInteger)
        return false;
    *output = static_cast<qint64>(revision);
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
    if (!RemoteServiceReplica::replace(this, node, m_node, m_state, m_nodeDestroyedConnection,
            [this] {
                ++m_generation;
                failPending(QStringLiteral("Steam session disconnected; the operation was not retried."));
            }))
        return;
    const QPointer<SteamService> bindingGuard(this);
    const QPointer<SteamStateReplica> bindingState = m_state;
    const quint64 bindingGeneration = m_generation;
    resetGraphSync();
    m_graph->clear();
    if (!bindingGuard || bindingState != m_state || bindingGeneration != m_generation) return;
    if (m_state) {
        connect(m_state, &QRemoteObjectReplica::stateChanged,
                this, &SteamService::onReplicaStateChanged);
        connect(m_state, &QRemoteObjectReplica::notified,
                this, [this] {
                    const QPointer<SteamService> guard(this);
                    const QPointer<SteamStateReplica> requestedState = m_state;
                    const quint64 generation = m_generation;
                    emit stateChanged();
                    if (guard && requestedState == m_state && generation == m_generation && ready())
                        applyGraphSnapshot();
                });
        connect(m_state, &SteamStateReplica::editorRevisionChanged,
                this, &SteamService::stateChanged);
        connect(m_state, &SteamStateReplica::graphUpdateChanged,
                this, &SteamService::applyGraphSnapshot);
        connect(m_state, &SteamStateReplica::warningSnapshotChanged,
                this, &SteamService::reconcileWarningSnapshot);
        connect(m_state, &SteamStateReplica::hostIdChanged,
                this, &SteamService::reconcileWarningSnapshot);
        connect(m_state, &SteamStateReplica::hostIdChanged, this, [this] {
            if (m_graphHostId == m_state->hostId()) return;
            resetGraphSync();
            if (!ready()) return;
            m_graphHostId = m_state->hostId();
            if (!m_state->graphUpdate().isEmpty()) requestGraphSnapshot();
        });
    }
    emit stateChanged();
    if (!bindingGuard || bindingState != m_state || bindingGeneration != m_generation) return;
    emit readyChanged();
    if (bindingGuard && bindingState == m_state && bindingGeneration == m_generation)
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

int SteamService::effectiveSteamDurationSec(int index, double milkG) const
{
    if (!ready())
        return 0;
    return SteamCalculations::effectiveSteamDuration(getSteamPitcherPreset(index),
        m_state->milkAutoCaptureEnabled(), m_state->steamSecondsPerGram(), milkG)
        .effectiveSeconds;
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

    const QPointer<SteamStateReplica> requested = m_state;
    const QPointer<SteamService> guard(this);
    const quint64 generation = m_generation;
    m_pendingRequestId = requestId;
    m_pendingOperation = operation;
    m_requestGeneration = m_generation;
    setBusy(true);
    if (!guard || requested != m_state || generation != m_generation || m_pendingRequestId != requestId)
        return requestId;
    setErrorMessage({});
    if (!guard || requested != m_state || generation != m_generation || m_pendingRequestId != requestId)
        return requestId;
    if (!ready()) {
        failPending(QStringLiteral("Steam session was lost; the operation was not retried."));
        return requestId;
    }
    auto* watcher = new QRemoteObjectPendingCallWatcher(
        requested->execute(operation, fields, revision), this);
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
        const QPointer<SteamService> guard(this);
        setBusy(false);
        if (!guard) return;
        if (generation == m_generation && m_pendingRequestId.isEmpty())
            setErrorMessage(success ? QString() : message);
        if (!guard) return;
        emit completed(requestId, operation, success, message, result, newRevision);
    }, Qt::QueuedConnection);
}

void SteamService::onReplicaStateChanged()
{
    const QPointer<SteamService> guard(this);
    const QPointer<SteamStateReplica> requestedState = m_state;
    const bool nowReady = ready();
    if (!nowReady) ++m_generation;
    const quint64 generation = m_generation;
    if (!nowReady) {
        failPending(QStringLiteral("Steam session was lost; the operation was not retried."));
        if (!guard || requestedState != m_state || generation != m_generation) return;
        resetGraphSync();
        m_graph->clear();
    } else {
        applyGraphSnapshot();
        if (!guard || requestedState != m_state || generation != m_generation) return;
        reconcileWarningSnapshot();
        if (!guard || requestedState != m_state || generation != m_generation) return;
        setErrorMessage({});
    }
    if (!guard || requestedState != m_state || generation != m_generation) return;
    emit stateChanged();
    if (guard && requestedState == m_state && generation == m_generation) emit readyChanged();
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

    const QPointer<SteamService> guard(this);
    const QPointer<SteamStateReplica> requestedState = m_state;
    const quint64 generation = m_generation;
    if (!snapshot.events.isEmpty() && m_warningCursor < snapshot.events.first().id - 1) {
        const qint64 missed = snapshot.events.first().id - m_warningCursor - 1;
        emit warningEventsMissed(static_cast<int>(qMin<qint64>(missed, INT_MAX)));
        if (!guard || requestedState != m_state || generation != m_generation
            || hostId != m_state->hostId()) return;
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
        if (!guard || requestedState != m_state || generation != m_generation
            || hostId != m_state->hostId()) return;
        m_warningCursor = event.id;
    }
    emit warningSnapshotChanged();
}

void SteamService::resetGraphSync()
{
    m_graphInitialized = false;
    m_graphHostId.clear();
    m_graphRequestId.clear();
    m_graphUpdates.clear();
    m_bufferedGraphPoints = 0;
}

void SteamService::applyGraphSnapshot()
{
    if (!ready() || !m_state) return;
    if (m_graphHostId != m_state->hostId()) {
        resetGraphSync();
        m_graphHostId = m_state->hostId();
    }
    consumeGraphUpdate(m_state->graphUpdate());
}

bool SteamService::applyGraphUpdate(const QVariantMap& update, bool append)
{
    UiWorkStats::Span work(UiWorkStats::Kind::SteamApply);
    qint64 revision = 0;
    qint64 base = 0;
    double rawTime = 0.0;
    QVector<QPointF> pressure, flow, temperature, goals;
    const bool hasGoals = update.contains(QStringLiteral("flowGoalPoints"));
    if (!readRevision(update.value(QStringLiteral("revision")), &revision)
        || !readNumber(update.value(QStringLiteral("rawTime")), &rawTime)
        || !readPoints(update.value(QStringLiteral("pressure")), SteamGraphValidation::MaximumSamples, &pressure)
        || !readPoints(update.value(QStringLiteral("flow")), SteamGraphValidation::MaximumSamples, &flow)
        || !readPoints(update.value(QStringLiteral("temperature")), SteamGraphValidation::MaximumSamples, &temperature)
        || ((!append || hasGoals) && !readPoints(update.value(QStringLiteral("flowGoalPoints")),
                    SteamGraphValidation::MaximumGoalPoints, &goals))
        || (append && !readRevision(update.value(QStringLiteral("baseRevision")), &base)))
        return false;
    work.items = pressure.size() + flow.size() + temperature.size() + goals.size();
    work.changes = !append;
    // Set before notifying: graph listeners may synchronously replace this binding.
    m_graphInitialized = true;
    return append ? m_graph->appendSnapshot(base, revision, rawTime, pressure, flow, temperature,
                                            hasGoals ? &goals : nullptr)
                  : m_graph->replaceSnapshot(revision, rawTime, pressure, flow, temperature, goals);
}

void SteamService::rejectGraphUpdate()
{
    const QPointer<SteamService> guard(this);
    const auto state = m_state;
    const auto generation = m_generation;
    resetGraphSync();
    m_graph->clear();
    if (guard && state == m_state && generation == m_generation)
        setErrorMessage(QStringLiteral("The Steam graph snapshot was invalid."));
}

void SteamService::consumeGraphUpdate(const QVariantMap& update)
{
    if (update.isEmpty()) return;
    qint64 revision = 0;
    if (!readRevision(update.value(QStringLiteral("revision")), &revision)) {
        rejectGraphUpdate();
        return;
    }
    if (m_graphInitialized && revision <= m_graph->revision()) return;
    const bool append = update.contains(QStringLiteral("baseRevision"));
    qint64 base = 0;
    if (append && !readRevision(update.value(QStringLiteral("baseRevision")), &base)) {
        rejectGraphUpdate();
        return;
    }
    if (!m_graphRequestId.isEmpty()
        || (append && (!m_graphInitialized || base != m_graph->revision()))) {
        const qsizetype points = update.value(QStringLiteral("pressure")).toList().size()
            + update.value(QStringLiteral("flow")).toList().size()
            + update.value(QStringLiteral("temperature")).toList().size();
        // A bounded backlog. If it overflows, a subsequent gap requests a fresh snapshot.
        if (m_graphUpdates.size() >= 64 || m_bufferedGraphPoints + points > 18000) {
            m_graphUpdates.clear();
            m_bufferedGraphPoints = 0;
        }
        m_graphUpdates.append(update);
        m_bufferedGraphPoints += points;
        if (m_graphRequestId.isEmpty()) requestGraphSnapshot();
        return;
    }
    if (!append) {
        m_graphRequestId.clear();
        m_graphUpdates.clear();
        m_bufferedGraphPoints = 0;
    }
    const QPointer<SteamService> guard(this);
    const auto state = m_state;
    const auto generation = m_generation;
    const bool valid = applyGraphUpdate(update, append);
    if (guard && state == m_state && generation == m_generation && !valid) rejectGraphUpdate();
}

void SteamService::requestGraphSnapshot()
{
    if (!ready() || !m_state || !m_graphRequestId.isEmpty()) return;
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_graphRequestId = id;
    const auto state = m_state;
    const auto generation = m_generation;
    const QString hostId = state->hostId();
    auto* watcher = new QRemoteObjectPendingCallWatcher(state->currentGraphSnapshot(), this);
    connect(watcher, &QRemoteObjectPendingCallWatcher::finished, this,
            [this, watcher, id, state, generation, hostId] {
        const QVariantMap snapshot = watcher->returnValue().toMap();
        watcher->deleteLater();
        if (id != m_graphRequestId || generation != m_generation || state != m_state
            || !ready() || hostId != m_state->hostId()) return;
        m_graphRequestId.clear();
        const auto updates = std::exchange(m_graphUpdates, {});
        m_bufferedGraphPoints = 0;
        const QPointer<SteamService> guard(this);
        const bool valid = applyGraphUpdate(snapshot, false);
        if (!guard || generation != m_generation || state != m_state
            || hostId != m_state->hostId()) return;
        if (!valid) { rejectGraphUpdate(); return; }
        for (const auto& update : updates) {
            consumeGraphUpdate(update);
            if (!guard || generation != m_generation || state != m_state
                || hostId != m_state->hostId()) return;
        }
    });
}

void SteamService::failPending(const QString& message)
{
    if (m_pendingRequestId.isEmpty())
        return;
    const QString requestId = std::exchange(m_pendingRequestId, {});
    const QString operation = std::exchange(m_pendingOperation, {});
    const QPointer<SteamService> guard(this);
    const quint64 generation = m_generation;
    setBusy(false);
    if (!guard) return;
    if (generation == m_generation && m_pendingRequestId.isEmpty())
        setErrorMessage(message);
    if (!guard) return;
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
