#include "globalsupportservice.h"

#include <QMetaObject>
#include <QRemoteObjectNode>
#include <QRemoteObjectPendingCallWatcher>
#include <QUuid>
#include <utility>

namespace {
QPointer<QRemoteObjectNode> s_publishedNode;
constexpr qsizetype kMaxClientPending = 32;
}

GlobalSupportService::GlobalSupportService(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent)
{
    setNode(node);
}

bool GlobalSupportService::ready() const
{
    return connected() && m_state->ready();
}

qint64 GlobalSupportService::mcpConfirmationDeadlineMs() const
{
    return connected() ? m_state->mcpConfirmationDeadlineMs() : 0;
}

void GlobalSupportService::setPublishedNode(QRemoteObjectNode* node)
{
    s_publishedNode = node;
}

QRemoteObjectNode* GlobalSupportService::publishedNode()
{
    return s_publishedNode;
}

bool GlobalSupportService::connected() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, s_publishedNode);
}

void GlobalSupportService::setNode(QRemoteObjectNode* node)
{
    if (m_node == node && m_state && (node || !m_nodeDestroyed))
        return;

    disconnect(m_nodeDestroyed);
    m_nodeDestroyed = {};
    failPending();
    if (m_state) {
        disconnect(m_state, nullptr, this, nullptr);
        m_state->deleteLater();
    }

    m_node = node;
    m_state = node ? node->acquire<GlobalSupportStateReplica>()
                   : new GlobalSupportStateReplica;
    m_state->setParent(this);
    if (node) {
        m_nodeDestroyed = connect(node, &QObject::destroyed, this,
                                  [this] { setNode(nullptr); });
    }

    connect(m_state, &QRemoteObjectReplica::stateChanged, this, [this] {
        if (!connected())
            failPending();
        emit stateChanged();
    });
    connect(m_state, &QRemoteObjectReplica::notified,
            this, &GlobalSupportService::stateChanged);
    connect(m_state, &GlobalSupportStateReplica::readyChanged,
            this, &GlobalSupportService::stateChanged);
    connect(m_state, &GlobalSupportStateReplica::crashAvailableChanged,
            this, &GlobalSupportService::stateChanged);
    connect(m_state, &GlobalSupportStateReplica::crashSubmittingChanged,
            this, &GlobalSupportService::stateChanged);
    connect(m_state, &GlobalSupportStateReplica::crashSubmissionStatusChanged,
            this, &GlobalSupportService::stateChanged);
    connect(m_state, &GlobalSupportStateReplica::crashIssueUrlChanged,
            this, &GlobalSupportService::stateChanged);
    connect(m_state, &GlobalSupportStateReplica::crashErrorChanged,
            this, &GlobalSupportService::stateChanged);
    connect(m_state, &GlobalSupportStateReplica::de1CommunicationFailureChanged,
            this, &GlobalSupportService::stateChanged);
    connect(m_state, &GlobalSupportStateReplica::de1CommunicationEventIdChanged,
            this, &GlobalSupportService::stateChanged);
    connect(m_state, &GlobalSupportStateReplica::profileRefusalEventIdChanged,
            this, &GlobalSupportService::stateChanged);
    connect(m_state, &GlobalSupportStateReplica::profileRefusalChanged,
            this, &GlobalSupportService::stateChanged);
    connect(m_state, &GlobalSupportStateReplica::mcpConfirmationIdChanged,
            this, &GlobalSupportService::stateChanged);
    connect(m_state, &GlobalSupportStateReplica::mcpToolDescriptionChanged,
            this, &GlobalSupportService::stateChanged);
    connect(m_state, &GlobalSupportStateReplica::mcpConfirmationDeadlineMsChanged,
            this, &GlobalSupportService::stateChanged);
    connect(m_state, &GlobalSupportStateReplica::operationCompleted, this,
            [this](const QString& requestId, const QString& operation, bool success,
                   const QVariantMap& result, const QString& reason) {
        const auto pending = m_pending.find(requestId);
        if (pending == m_pending.end() || pending.value() != operation)
            return;
        m_pending.erase(pending);
        emit operationCompleted(requestId, operation, success, result, reason);
    }, Qt::QueuedConnection);

    emit stateChanged();
}

QString GlobalSupportService::openCrashReport()
{
    return execute(QStringLiteral("openCrashReport"));
}

void GlobalSupportService::failPending()
{
    const auto pending = std::exchange(m_pending, {});
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        const QString requestId = it.key();
        const QString operation = it.value();
        QMetaObject::invokeMethod(this, [this, requestId, operation] {
            emit operationCompleted(requestId, operation, false, {},
                                    QStringLiteral("disconnected"));
        }, Qt::QueuedConnection);
    }
}

QString GlobalSupportService::newRequestId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString GlobalSupportService::execute(const QString& operation,
                                      const QVariantMap& arguments)
{
    if (operation == QLatin1String("openCrashReport")) {
        if (!arguments.isEmpty() || !connected() || m_pending.size() >= kMaxClientPending)
            return {};
        const QString requestId = newRequestId();
        m_pending.insert(requestId, operation);
        auto* watcher = new QRemoteObjectPendingCallWatcher(
            m_state->requestCrashReport(requestId), this);
        connect(watcher, &QRemoteObjectPendingCallWatcher::finished, this,
                [this, watcher, requestId, operation] {
            watcher->deleteLater();
            const auto pending = m_pending.find(requestId);
            if (pending == m_pending.end() || pending.value() != operation)
                return;
            m_pending.erase(pending);
            bool success = false;
            QVariantMap result;
            QString reason;
            if (watcher->error() == QRemoteObjectPendingCall::NoError) {
                const QVariantMap response = watcher->returnValue().toMap();
                success = response.value(QStringLiteral("success")).toBool();
                result = response.value(QStringLiteral("result")).toMap();
                reason = response.value(QStringLiteral("reason")).toString();
                if (!success && reason.isEmpty())
                    reason = QStringLiteral("requestFailed");
            } else {
                reason = QStringLiteral("disconnected");
            }
            emit operationCompleted(requestId, operation, success, result, reason);
        });
        return requestId;
    }

    if (!connected() || m_pending.size() >= kMaxClientPending)
        return {};

    const QString requestId = newRequestId();
    m_pending.insert(requestId, operation);
    m_state->execute(requestId, operation, arguments);
    return requestId;
}

void GlobalSupportService::refresh()
{
    if (connected())
        m_state->refresh();
}
