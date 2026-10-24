#include "visualizerprofileimportservice.h"

#include <QMetaObject>
#include <QRemoteObjectNode>
#include <QUuid>
#include <utility>

namespace {
QPointer<QRemoteObjectNode> s_publishedNode;
}

VisualizerProfileImportService::VisualizerProfileImportService(QRemoteObjectNode* node,
                                                               QObject* parent)
    : RemoteService(parent)
{
    setNode(node);
}

VisualizerProfileImportService::~VisualizerProfileImportService()
{
    if (m_state)
        disconnect(m_state, nullptr, this, nullptr);
}

void VisualizerProfileImportService::setPublishedNode(QRemoteObjectNode* node)
{
    s_publishedNode = node;
}

QRemoteObjectNode* VisualizerProfileImportService::publishedNode()
{
    return s_publishedNode;
}

bool VisualizerProfileImportService::connected() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, s_publishedNode);
}

bool VisualizerProfileImportService::ready() const
{
    return connected() && m_state->ready();
}

bool VisualizerProfileImportService::importing() const
{
    return connected() && m_state->importing();
}

bool VisualizerProfileImportService::fetching() const
{
    return connected() && m_state->fetching();
}

QString VisualizerProfileImportService::lastError() const
{
    return connected() ? m_state->lastError() : QString();
}

QVariantList VisualizerProfileImportService::sharedShots() const
{
    return connected() ? m_state->sharedShots() : QVariantList{};
}

bool VisualizerProfileImportService::sharedShotsTruncated() const
{
    return connected() && m_state->sharedShotsTruncated();
}

void VisualizerProfileImportService::setNode(QRemoteObjectNode* node)
{
    if (!RemoteServiceReplica::replace(this, node, m_node, m_state, m_nodeDestroyed,
            [this] { failPending(); }))
        return;

    connect(m_state, &QRemoteObjectReplica::stateChanged,
            this, &VisualizerProfileImportService::changed);
    connect(m_state, &QRemoteObjectReplica::notified,
            this, &VisualizerProfileImportService::changed);
    connect(m_state, &VisualizerProfileImportStateReplica::readyChanged,
            this, &VisualizerProfileImportService::changed);
    connect(m_state, &VisualizerProfileImportStateReplica::importingChanged,
            this, &VisualizerProfileImportService::changed);
    connect(m_state, &VisualizerProfileImportStateReplica::fetchingChanged,
            this, &VisualizerProfileImportService::changed);
    connect(m_state, &VisualizerProfileImportStateReplica::lastErrorChanged,
            this, &VisualizerProfileImportService::changed);
    connect(m_state, &VisualizerProfileImportStateReplica::sharedShotsTruncatedChanged,
            this, &VisualizerProfileImportService::changed);
    connect(m_state, &VisualizerProfileImportStateReplica::sharedShotsChanged, this,
            [this] {
        emit sharedShotsChanged();
        changed();
    }, Qt::QueuedConnection);
    connect(m_state, &VisualizerProfileImportStateReplica::operationCompleted, this,
            [this](const QString& id, const QString& operation, bool success,
                   const QVariantMap& result, const QString& reason) {
        const auto it = m_pending.find(id);
        if (it == m_pending.end() || it.value() != operation)
            return;
        m_pending.erase(it);
        m_cancelledRequests.remove(id);
        if (m_duplicateResolutionPendingId == id)
            m_duplicateResolutionPendingId.clear();
        if (m_duplicateRequestId == id)
            m_duplicateRequestId.clear();
        emit operationCompleted(id, operation, success, result, reason);
        if (!success) {
            emit importFailed(reason);
        } else if (operation.startsWith(QLatin1String("import"))
                   && !result.value(QStringLiteral("title")).toString().isEmpty()) {
            emit importSuccess(result.value(QStringLiteral("title")).toString());
        }
        changed();
    }, Qt::QueuedConnection);
    connect(m_state, &VisualizerProfileImportStateReplica::duplicateFound, this,
            [this](const QString& id, const QString& title) {
        if (!m_pending.contains(id))
            return;
        m_duplicateRequestId = id;
        if (m_cancelledRequests.contains(id)) {
            execute(QStringLiteral("cancelPending"), {}, id);
            return;
        }
        emit duplicateFound(id, title);
    }, Qt::QueuedConnection);
    connect(m_state, &VisualizerProfileImportStateReplica::requestRejected, this,
            [this](const QString& id, const QString& operation, const QString& reason) {
        if (!m_pending.contains(id))
            return;
        if (m_duplicateResolutionPendingId == id)
            m_duplicateResolutionPendingId.clear();
        emit operationRejected(id, operation, reason);
    }, Qt::QueuedConnection);
    changed();
}

void VisualizerProfileImportService::changed()
{
    emit stateChanged();
    emit readyChanged();
}

void VisualizerProfileImportService::failPending()
{
    const auto pending = std::exchange(m_pending, {});
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        const QString id = it.key();
        const QString operation = it.value();
        QMetaObject::invokeMethod(this, [this, id, operation] {
            emit operationCompleted(id, operation, false, {}, QStringLiteral("disconnected"));
        }, Qt::QueuedConnection);
    }
    m_duplicateRequestId.clear();
    m_duplicateResolutionPendingId.clear();
    m_cancelledRequests.clear();
}

QString VisualizerProfileImportService::newRequestId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString VisualizerProfileImportService::execute(const QString& operation,
                                                const QVariantMap& arguments,
                                                const QString& requestedId)
{
    if (!connected())
        return {};

    const bool resolvingDuplicate = operation == QLatin1String("saveOverwrite")
        || operation == QLatin1String("saveAsNew")
        || operation == QLatin1String("saveWithNewName")
        || operation == QLatin1String("cancelPending");
    QString id;
    if (resolvingDuplicate) {
        id = requestedId.isEmpty() ? m_duplicateRequestId : requestedId;
        if (id.isEmpty() || id != m_duplicateRequestId
            || !m_pending.contains(id) || m_duplicateResolutionPendingId == id)
            return {};
        if (operation == QLatin1String("cancelPending")) {
            m_cancelledRequests.insert(id);
        }
        m_duplicateResolutionPendingId = id;
    } else {
        id = newRequestId();
        m_pending.insert(id, operation);
    }
    m_state->execute(id, operation, arguments);
    return id;
}

void VisualizerProfileImportService::refresh()
{
    if (connected())
        m_state->refresh();
}

void VisualizerProfileImportService::fetchSharedShots()
{
    execute(QStringLiteral("fetchSharedShots"));
}

QString VisualizerProfileImportService::importFromShareCode(const QString& code)
{
    return execute(QStringLiteral("importFromShareCode"), {{QStringLiteral("shareCode"), code}});
}

QString VisualizerProfileImportService::importFromShotId(const QString& shotId)
{
    return execute(QStringLiteral("importFromShotId"), {{QStringLiteral("shotId"), shotId}});
}

QString VisualizerProfileImportService::importFromShotIdWithName(const QString& shotId,
                                                                  const QString& name)
{
    return execute(QStringLiteral("importFromShotIdWithName"),
                   {{QStringLiteral("shotId"), shotId}, {QStringLiteral("name"), name}});
}

QString VisualizerProfileImportService::importSelectedShots(const QStringList& shotIds,
                                                              bool overwrite)
{
    return execute(QStringLiteral("importSelectedShots"),
                   {{QStringLiteral("shotIds"), shotIds}, {QStringLiteral("overwrite"), overwrite}});
}

QString VisualizerProfileImportService::saveOverwrite(const QString& requestId)
{
    return execute(QStringLiteral("saveOverwrite"), {}, requestId);
}

QString VisualizerProfileImportService::saveAsNew(const QString& requestId)
{
    return execute(QStringLiteral("saveAsNew"), {}, requestId);
}

QString VisualizerProfileImportService::saveWithNewName(const QString& name,
                                                         const QString& requestId)
{
    return execute(QStringLiteral("saveWithNewName"), {{QStringLiteral("name"), name}}, requestId);
}

QString VisualizerProfileImportService::cancelPending(const QString& requestId)
{
    if (!requestId.isEmpty() && m_duplicateRequestId != requestId) {
        if (m_pending.contains(requestId)) {
            m_cancelledRequests.insert(requestId);
            return requestId;
        }
        return {};
    }
    return execute(QStringLiteral("cancelPending"), {}, requestId);
}
