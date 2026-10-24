#include "machinecontrolservice.h"

#include "serviceoperation.h"

#include <QMetaObject>
#include <QMetaType>
#include <QRemoteObjectNode>
#include <QUuid>
#include <utility>

namespace {
QPointer<QRemoteObjectNode> s_publishedNode;
constexpr qint64 MaxSafeEventSequence = 9007199254740991LL;
}

MachineControlService::MachineControlService(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent)
{
    setNode(node);
}

MachineControlService::~MachineControlService()
{
    if (m_state)
        disconnect(m_state, nullptr, this, nullptr);
}

void MachineControlService::setPublishedNode(QRemoteObjectNode* node)
{
    s_publishedNode = node;
}

QRemoteObjectNode* MachineControlService::publishedNode()
{
    return s_publishedNode;
}

void MachineControlService::setNode(QRemoteObjectNode* node)
{
    if (!RemoteServiceReplica::replace(this, node, m_node, m_state, m_nodeDestroyedConnection,
            [this] {
                failPending(QStringLiteral("Connection lost; the result is unknown. The action was not retried."));
            }))
        return;
    m_wasReady = false;
    if (m_state) {
        connect(m_state, &QRemoteObjectReplica::stateChanged, this, [this] {
            const QPointer<MachineControlService> guard(this);
            updatePresentationReadiness();
            if (!ready())
                failPending(QStringLiteral("Connection lost; the result is unknown. The action was not retried."));
            if (guard) emit stateChanged();
        });
        connect(m_state, &QRemoteObjectReplica::notified,
                this, &MachineControlService::stateChanged);
        connect(m_state, &MachineControlStateReplica::selectedRecipeIdChanged,
                this, &MachineControlService::stateChanged);
        connect(m_state, &MachineControlStateReplica::espressoOperationIdChanged,
                this, &MachineControlService::stateChanged);
        connect(m_state, &MachineControlStateReplica::eventSourceIdChanged,
                this, &MachineControlService::stateChanged);
        connect(m_state, &MachineControlStateReplica::eventSequenceChanged,
                this, &MachineControlService::stateChanged);
        connect(m_state, &MachineControlStateReplica::phaseChanged,
                this, &MachineControlService::stateChanged);
        connect(m_state, &MachineControlStateReplica::currentFrameIndexChanged,
                this, &MachineControlService::stateChanged);
        connect(m_state, &MachineControlStateReplica::currentFrameNameChanged,
                this, &MachineControlService::stateChanged);
        connect(m_state, &MachineControlStateReplica::operationCompleted,
                this, &MachineControlService::onOperationCompleted, Qt::QueuedConnection);
        connect(m_state, &MachineControlStateReplica::presentationEvent,
                this, &MachineControlService::receivePresentationEvent);
    }
    updatePresentationReadiness();
    emit stateChanged();
}

qint64 MachineControlService::selectedRecipeId() const
{
    return ready() ? m_state->selectedRecipeId() : -1;
}

qint64 MachineControlService::espressoOperationId() const
{
    return ready() ? m_state->espressoOperationId() : 0;
}

bool MachineControlService::ready() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, s_publishedNode);
}

int MachineControlService::phase() const
{
    return ready() ? m_state->phase() : -1;
}

int MachineControlService::currentFrameIndex() const
{
    return ready() ? m_state->currentFrameIndex() : -1;
}

QString MachineControlService::currentFrameName() const
{
    return ready() ? m_state->currentFrameName() : QString();
}

void MachineControlService::updatePresentationReadiness()
{
    const bool nowReady = ready();
    if (!nowReady) {
        m_wasReady = false;
        return;
    }
    if (m_wasReady)
        return;
    m_wasReady = true;

    const QString sourceId = m_state->eventSourceId();
    const qint64 sourceSequence = m_state->eventSequence();
    if (sourceId.isEmpty() || sourceSequence < 0)
        return;

    // A newly-created source has no event history for this replica. For the
    // same source after reconnect, retain the last delivered cursor so the
    // first later event can report anything missed while disconnected.
    if (!m_hasEventBaseline || sourceId != m_eventSourceId) {
        m_eventSourceId = sourceId;
        m_lastEventSequence = sourceSequence;
        m_hasEventBaseline = true;
    }
}

void MachineControlService::receivePresentationEvent(const QString& sourceId,
                                                       qint64 sequence,
                                                       const QString& event,
                                                       const QVariantMap& payload)
{
    if (!ready() || sourceId.isEmpty() || sequence <= 0
        || sequence > MaxSafeEventSequence)
        return;

    if (!m_hasEventBaseline || sourceId != m_eventSourceId) {
        m_eventSourceId = sourceId;
        m_lastEventSequence = sequence;
        m_hasEventBaseline = true;
        emit stateChanged();
        return;
    }
    if (sequence <= m_lastEventSequence)
        return;
    if (sequence != m_lastEventSequence + 1) {
        const qint64 missed = sequence - m_lastEventSequence - 1;
        m_lastEventSequence = sequence;
        emit presentationEventsMissed(missed);
        return;
    }
    m_lastEventSequence = sequence;
    emit presentationEvent(sourceId, sequence, event, payload);
}

QString MachineControlService::selectSteamPitcher(int index, const QVariantMap& expectedPreset)
{
    return send(QStringLiteral("select-steam-pitcher"),
                {{QStringLiteral("index"), index}, {QStringLiteral("expectedPreset"), expectedPreset}});
}

QString MachineControlService::selectWaterVessel(int index, const QVariantMap& expectedPreset)
{
    return send(QStringLiteral("select-water-vessel"),
                {{QStringLiteral("index"), index}, {QStringLiteral("expectedPreset"), expectedPreset}});
}

QString MachineControlService::selectFlushPreset(int index, const QVariantMap& expectedPreset)
{
    return send(QStringLiteral("select-flush-preset"),
                {{QStringLiteral("index"), index}, {QStringLiteral("expectedPreset"), expectedPreset}});
}

QString MachineControlService::startRecipe(const QString& context, qint64 recipeId)
{
    return send(QStringLiteral("start-recipe"), {{QStringLiteral("recipeId"), recipeId}}, context);
}

QString MachineControlService::startEspresso(const QString& context, const QString& filename)
{
    return send(QStringLiteral("start-espresso"), {{QStringLiteral("profileFilename"), filename}}, context);
}

QString MachineControlService::startSteam(const QString& context, int index,
                                          const QVariantMap& expectedPreset)
{
    return send(QStringLiteral("start-steam"),
                {{QStringLiteral("pitcherIndex"), index}, {QStringLiteral("expectedPreset"), expectedPreset}}, context);
}

QString MachineControlService::startHotWater(const QString& context, int index,
                                             const QVariantMap& expectedPreset)
{
    return send(QStringLiteral("start-hot-water"),
                {{QStringLiteral("waterVesselIndex"), index}, {QStringLiteral("expectedPreset"), expectedPreset}}, context);
}

QString MachineControlService::startFlush(const QString& context, int index,
                                          const QVariantMap& expectedPreset)
{
    return send(QStringLiteral("start-flush"),
                {{QStringLiteral("flushPresetIndex"), index}, {QStringLiteral("expectedPreset"), expectedPreset}}, context);
}

QString MachineControlService::sleepMachine(const QString& expectedContextId)
{
    return send(QStringLiteral("sleep-machine"), {}, expectedContextId);
}

QString MachineControlService::wakeMachine(const QString& expectedContextId)
{
    return send(QStringLiteral("wake-machine"), {}, expectedContextId);
}

QString MachineControlService::requestIdle(const QString& expectedContextId)
{
    return send(QStringLiteral("request-idle"), {}, expectedContextId);
}

QString MachineControlService::disconnectMachine(const QString& expectedContextId)
{
    return send(QStringLiteral("disconnect-machine"), {}, expectedContextId);
}

QString MachineControlService::scanForDevices()
{
    return send(QStringLiteral("scan-for-devices"));
}

QString MachineControlService::toggleSteamHeater(const QString& expectedContextId)
{
    return send(QStringLiteral("toggle-steam-heater"), {}, expectedContextId);
}

QString MachineControlService::toggleCharging(const QString& expectedContextId)
{
    return send(QStringLiteral("toggle-charging"), {}, expectedContextId);
}

QString MachineControlService::loadPreviousProfile(const QString& expectedContextId)
{
    return send(QStringLiteral("load-previous-profile"), {}, expectedContextId);
}

QString MachineControlService::generateFakeShot(const QString& expectedContextId)
{
    return send(QStringLiteral("generate-fake-shot"), {}, expectedContextId);
}

QString MachineControlService::applyTemperatureOverride(
    const QString& expectedContextId, const QVariantMap& expectedProfile,
    const QVariantMap& expectedOverride, double valueC)
{
    return send(QStringLiteral("apply-temperature-override"),
                {{QStringLiteral("expectedProfile"), expectedProfile},
                 {QStringLiteral("expectedOverride"), expectedOverride},
                 {QStringLiteral("valueC"), valueC}}, expectedContextId);
}

QString MachineControlService::requestBrewSettings()
{
    return send(QStringLiteral("brew-settings-snapshot"));
}

QString MachineControlService::applyBrewSettings(const QString& expectedRevision,
                                                 const QVariantMap& values,
                                                 const QString& expectedContextId)
{
    QVariantMap fields = values;
    fields.insert(QStringLiteral("expectedRevision"), expectedRevision);
    return send(QStringLiteral("brew-settings-apply"), fields, expectedContextId);
}

QString MachineControlService::updateProfileTemperature(const QString& expectedRevision,
                                                        double valueC,
                                                        const QString& expectedContextId)
{
    return send(QStringLiteral("brew-settings-update-profile-temperature"),
                {{QStringLiteral("expectedRevision"), expectedRevision},
                 {QStringLiteral("valueC"), valueC}}, expectedContextId);
}

QString MachineControlService::setHotWaterFlow(const QString& context, int flow)
{
    return send(QStringLiteral("set-hot-water-flow"), {{QStringLiteral("flow"), flow}}, context);
}

QString MachineControlService::stopHotWater(const QString& context)
{
    return send(QStringLiteral("stop-hot-water"), {}, context);
}

QString MachineControlService::stopFlush(const QString& context)
{
    return send(QStringLiteral("stop-flush"), {}, context);
}

QString MachineControlService::stopEspresso(const QString& context)
{
    return send(QStringLiteral("stop-espresso"), {}, context);
}

QString MachineControlService::bumpTargetWeight(const QString& context, double deltaG)
{
    return send(QStringLiteral("bump-target-weight"), {{QStringLiteral("deltaG"), deltaG}}, context);
}

QString MachineControlService::bumpFlushSeconds(const QString& context, double deltaSec)
{
    return send(QStringLiteral("bump-flush-seconds"), {{QStringLiteral("deltaSec"), deltaSec}}, context);
}

QString MachineControlService::skipFrame(const QString& context)
{
    return send(QStringLiteral("skip-frame"), {}, context);
}

QString MachineControlService::applyHotWaterSettings(const QString& expectedContextId)
{
    return send(QStringLiteral("apply-hot-water-settings"), {}, expectedContextId);
}

QString MachineControlService::applyFlushSettings(const QString& expectedContextId)
{
    return send(QStringLiteral("apply-flush-settings"), {}, expectedContextId);
}

QString MachineControlService::send(const QString& operation, const QVariantMap& fields,
                                    const QString& expectedContextId)
{
    const QString requestId = ServiceOperation::newId();
    if (!ready()) {
        QMetaObject::invokeMethod(this, [this, requestId, operation] {
            emit completed(requestId, operation, false,
                           QStringLiteral("Machine control session is not ready."), {});
        }, Qt::QueuedConnection);
        return requestId;
    }
    if (m_pending.size() >= 32) {
        QMetaObject::invokeMethod(this, [this, requestId, operation] {
            emit completed(requestId, operation, false,
                           QStringLiteral("Too many machine control requests are pending."), {});
        }, Qt::QueuedConnection);
        return requestId;
    }
    const QPointer<MachineControlStateReplica> requested = m_state;
    const QPointer<MachineControlService> guard(this);
    m_pending.insert(requestId, operation);
    if (m_pending.size() == 1)
        emit busyChanged();
    if (!guard) return requestId;
    if (!m_errorMessage.isEmpty()) {
        m_errorMessage.clear();
        emit stateChanged();
    }
    if (!guard || requested != m_state || m_pending.value(requestId) != operation)
        return requestId;
    if (!ready()) {
        failPending(QStringLiteral("Connection lost; the result is unknown. The action was not retried."));
        return requestId;
    }
    requested->execute(requestId, operation, fields, expectedContextId);
    // The typed QtRO slot is one-way. A disconnect fails the local request;
    // it is never retried or replayed after reconnection.
    return requestId;
}

void MachineControlService::onOperationCompleted(const QString& requestId,
                                                 const QString& operation,
                                                 bool success, const QString& message,
                                                 const QVariantMap& result)
{
    auto it = m_pending.find(requestId);
    if (it == m_pending.end() || *it != operation)
        return;
    m_pending.erase(it);
    if (!success)
        m_errorMessage = message;
    const bool wasBusy = !m_pending.isEmpty();
    const QPointer<MachineControlService> guard(this);
    emit stateChanged();
    if (!guard) return;
    if (!wasBusy)
        emit busyChanged();
    if (!guard) return;
    QMetaObject::invokeMethod(this, [this, requestId, operation, success, message, result] {
        emit completed(requestId, operation, success, message, result);
    }, Qt::QueuedConnection);
}

void MachineControlService::failPending(const QString& message)
{
    if (m_pending.isEmpty())
        return;
    const auto pending = std::exchange(m_pending, {});
    m_errorMessage = message;
    const QPointer<MachineControlService> guard(this);
    emit busyChanged();
    if (!guard) return;
    emit stateChanged();
    if (!guard) return;
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        const QString requestId = it.key();
        const QString operation = it.value();
        QMetaObject::invokeMethod(this, [this, requestId, operation, message] {
            emit completed(requestId, operation, false, message, {});
        }, Qt::QueuedConnection);
    }
}
