#include "historyservice.h"

#include <QJSEngine>
#include <QQmlEngine>
#include <QMetaObject>
#include <QRemoteObjectNode>
#include <QUuid>

namespace {
QPointer<QRemoteObjectNode> s_historyNode;
}

HistoryService* HistoryService::create(QQmlEngine* engine, QJSEngine*)
{
    auto* service = new HistoryService(s_historyNode, engine);
    QJSEngine::setObjectOwnership(service, QJSEngine::CppOwnership);
    return service;
}

HistoryService::HistoryService(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent)
{
    setNode(node);
}

void HistoryService::setPublishedNode(QRemoteObjectNode* node)
{
    s_historyNode = node;
}

void HistoryService::setNode(QRemoteObjectNode* node)
{
    if (!RemoteServiceReplica::replace(this, node, m_node, m_state, m_nodeDestroyed,
            [this] { failPending(QStringLiteral("disconnected")); }))
        return;
    connect(m_state, &QRemoteObjectReplica::stateChanged, this, [this] {
        if (!connected())
            failPending(QStringLiteral("disconnected"));
        emit stateChanged();
        emit historyChanged();
        emit preferencesChanged();
    });
    connect(m_state, &QRemoteObjectReplica::notified, this, [this] {
        emit stateChanged();
        emit historyChanged();
        emit preferencesChanged();
    });
    connect(m_state, &HistoryStateReplica::readyChanged, this, &HistoryService::stateChanged);
    connect(m_state, &HistoryStateReplica::totalShotsChanged, this, &HistoryService::historyChanged);
    connect(m_state, &HistoryStateReplica::historyRevisionChanged, this, &HistoryService::historyChanged);
    connect(m_state, &HistoryStateReplica::preferencesChanged, this, &HistoryService::preferencesChanged);
    connect(m_state, &HistoryStateReplica::preferencesRevisionChanged, this, &HistoryService::preferencesChanged);
    connect(m_state, &HistoryStateReplica::pageReady, this,
            [this](const QString& requestId, int offset, const QVariantList& results,
                   int totalCount, const QString& reason) {
        if (m_pending.value(requestId) != QLatin1String("page"))
            return;
        m_pending.remove(requestId);
        emit pageReady(requestId, offset, results, totalCount, reason);
    }, Qt::QueuedConnection);
    connect(m_state, &HistoryStateReplica::operationCompleted, this,
            [this](const QString& requestId, const QString& operation, bool success,
                   const QVariantMap& result, const QString& reason) {
        if (m_pending.value(requestId) != operation)
            return;
        m_pending.remove(requestId);
        emitLegacyResult(operation, m_pendingShotIds.take(requestId), result, success);
        emit operationCompleted(requestId, operation, success, result, reason);
    }, Qt::QueuedConnection);
    connect(m_state, &HistoryStateReplica::historyDataChanged,
            this, &HistoryService::historyDataChanged, Qt::QueuedConnection);
    connect(m_state, &HistoryStateReplica::shotBadgesUpdated,
            this, &HistoryService::shotBadgesUpdated, Qt::QueuedConnection);
    connect(m_state, &HistoryStateReplica::shotPulledFromVisualizer,
            this, &HistoryService::shotPulledFromVisualizer, Qt::QueuedConnection);
    connect(m_state, &HistoryStateReplica::visualizerInfoUpdated,
            this, &HistoryService::visualizerInfoUpdated, Qt::QueuedConnection);
    connect(m_state, &HistoryStateReplica::decentUploadStateUpdated,
            this, &HistoryService::decentUploadStateUpdated, Qt::QueuedConnection);
    emit stateChanged();
    emit historyChanged();
    emit preferencesChanged();
}

bool HistoryService::connected() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, s_historyNode);
}

bool HistoryService::ready() const
{
    return connected() && m_state->ready();
}

qint64 HistoryService::totalShots() const
{
    return connected() ? m_state->totalShots() : 0;
}

qint64 HistoryService::lastSavedShotId() const
{
    return connected() ? m_state->lastSavedShotId() : 0;
}

quint64 HistoryService::historyRevision() const
{
    return connected() ? m_state->historyRevision() : 0;
}

QVariantMap HistoryService::preferences() const
{
    return connected() ? m_state->preferences() : QVariantMap();
}

quint64 HistoryService::preferencesRevision() const
{
    return connected() ? m_state->preferencesRevision() : 0;
}

QString HistoryService::newRequestId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
}

void HistoryService::refresh()
{
    if (connected())
        m_state->refresh();
}

QString HistoryService::requestAutoFavorites(const QString& groupBy, int maxItems)
{
    return send(QStringLiteral("autoFavorites"), [groupBy, maxItems](HistoryStateReplica* state, const QString& id) {
        state->requestAutoFavorites(id, groupBy, maxItems);
    });
}

QString HistoryService::requestAutoFavoriteGroupDetails(const QVariantMap& group)
{
    return send(QStringLiteral("autoFavoriteDetails"), [group](HistoryStateReplica* state, const QString& id) {
        state->requestAutoFavoriteGroupDetails(id, group);
    });
}

QString HistoryService::requestMostRecentShotId()
{
    return send(QStringLiteral("mostRecentShotId"),
                [](HistoryStateReplica* state, const QString& requestId) {
        state->requestMostRecentShotId(requestId);
    });
}

QString HistoryService::send(
    const QString& operation,
    const std::function<void(HistoryStateReplica*, const QString&)>& request,
    qint64 shotId)
{
    const QString requestId = newRequestId();
    const auto fail = [this, requestId, operation, shotId](const QString& reason) {
        QMetaObject::invokeMethod(this, [this, requestId, operation, shotId, reason] {
            if (operation == QLatin1String("page"))
                emit pageReady(requestId, 0, {}, 0, reason);
            else {
                emitLegacyResult(operation, shotId, {}, false);
                emit operationCompleted(requestId, operation, false, {}, reason);
            }
        }, Qt::QueuedConnection);
    };
    if (!connected() || !m_state->ready()) {
        fail(QStringLiteral("unavailable"));
        return requestId;
    }
    if (m_pending.size() >= 64) {
        fail(QStringLiteral("busy"));
        return requestId;
    }
    m_pending.insert(requestId, operation);
    if (shotId > 0)
        m_pendingShotIds.insert(requestId, shotId);
    request(m_state, requestId);
    return requestId;
}

QString HistoryService::requestPage(const QVariantMap& filter, int offset, int limit)
{
    return send(QStringLiteral("page"), [filter, offset, limit](HistoryStateReplica* state,
                                                                  const QString& requestId) {
        state->requestPage(requestId, filter, offset, limit);
    });
}

QString HistoryService::requestShot(qint64 shotId)
{
    return send(QStringLiteral("shot"), [shotId](HistoryStateReplica* state, const QString& id) {
        state->requestShot(id, shotId);
    }, shotId);
}

QString HistoryService::requestShotOutcome(qint64 shotId)
{
    return requestOutcome(shotId);
}

QString HistoryService::requestOutcome(qint64 shotId)
{
    return send(QStringLiteral("outcome"), [shotId](HistoryStateReplica* state, const QString& id) {
        state->requestOutcome(id, shotId);
    }, shotId);
}

QString HistoryService::requestUploadState(qint64 shotId)
{
    return send(QStringLiteral("uploadState"), [shotId](HistoryStateReplica* state, const QString& id) {
        state->requestUploadState(id, shotId);
    }, shotId);
}

QString HistoryService::requestDecentUploadState(qint64 shotId)
{
    return requestUploadState(shotId);
}

QString HistoryService::requestBaristas()
{
    return send(QStringLiteral("baristas"), [](HistoryStateReplica* state, const QString& id) {
        state->requestBaristas(id);
    });
}

QString HistoryService::updateMetadata(qint64 shotId, const QVariantMap& metadata)
{
    return send(QStringLiteral("metadata"), [shotId, metadata](HistoryStateReplica* state,
                                                                       const QString& id) {
        state->updateMetadata(id, shotId, metadata);
    }, shotId);
}

QString HistoryService::requestUpdateShotMetadata(qint64 shotId, const QVariantMap& metadata)
{
    return updateMetadata(shotId, metadata);
}

QString HistoryService::deleteShot(qint64 shotId)
{
    return send(QStringLiteral("deleteShot"), [shotId](HistoryStateReplica* state, const QString& id) {
        state->deleteShot(id, shotId);
    }, shotId);
}

QString HistoryService::requestDeleteShot(qint64 shotId)
{
    return deleteShot(shotId);
}

QString HistoryService::deleteShots(const QVariantList& shotIds)
{
    return send(QStringLiteral("deleteShots"), [shotIds](HistoryStateReplica* state,
                                                           const QString& id) {
        state->deleteShots(id, shotIds);
    });
}

QString HistoryService::loadShotWithMetadata(qint64 shotId)
{
    return send(QStringLiteral("loadShotWithMetadata"), [shotId](HistoryStateReplica* state,
                                                                   const QString& id) {
        state->loadShotWithMetadata(id, shotId);
    }, shotId);
}

QString HistoryService::setPreference(const QString& name, const QVariant& value,
                                      quint64 expectedRevision)
{
    return send(QStringLiteral("preference"), [name, value, expectedRevision](HistoryStateReplica* state,
                                                                               const QString& id) {
        state->setPreference(id, name, value, expectedRevision);
    });
}

void HistoryService::failPending(const QString& reason)
{
    const auto pending = m_pending;
    m_pending.clear();
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        if (it.value() == QLatin1String("page"))
            emit pageReady(it.key(), 0, {}, 0, reason);
        else {
            emitLegacyResult(it.value(), m_pendingShotIds.value(it.key()), {}, false);
            emit operationCompleted(it.key(), it.value(), false, {}, reason);
        }
    }
    m_pendingShotIds.clear();
}

void HistoryService::emitLegacyResult(const QString& operation, qint64 pendingShotId,
                                      const QVariantMap& result, bool success)
{
    const qint64 shotId = result.value(QStringLiteral("shotId"), pendingShotId).toLongLong();
    if (operation == QLatin1String("shot") && success) {
        emit shotReady(shotId, result.value(QStringLiteral("shot")).toMap());
    } else if (operation == QLatin1String("outcome") && success) {
        QVariantMap outcome = result;
        outcome.remove(QStringLiteral("shotId"));
        emit shotOutcomeReady(shotId, outcome);
    } else if (operation == QLatin1String("uploadState") && success) {
        QVariantMap state = result;
        state.remove(QStringLiteral("shotId"));
        emit decentUploadStateReady(shotId, state);
    } else if (operation == QLatin1String("metadata")) {
        emit shotMetadataUpdated(shotId, success);
    } else if (operation == QLatin1String("deleteShot") && success) {
        emit shotDeleted(shotId);
    }
}
