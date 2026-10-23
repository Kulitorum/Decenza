#include "shotuploadservice.h"
#include "serviceoperation.h"

#include <QJSEngine>
#include <QMetaObject>
#include <QRemoteObjectNode>
#include <QRemoteObjectPendingCallWatcher>
#include <QQmlEngine>
#include <QUuid>

namespace { QPointer<QRemoteObjectNode> s_publishedNode; }

ShotUploadService* ShotUploadService::create(QQmlEngine* engine, QJSEngine*)
{
    auto* service = new ShotUploadService(s_publishedNode, engine);
    QJSEngine::setObjectOwnership(service, QJSEngine::CppOwnership);
    return service;
}

ShotUploadService::ShotUploadService(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent)
{
    setNode(node);
}

ShotUploadService::~ShotUploadService()
{
    if (m_state)
        disconnect(m_state, nullptr, this, nullptr);
}

void ShotUploadService::setPublishedNode(QRemoteObjectNode* node) { s_publishedNode = node; }
QRemoteObjectNode* ShotUploadService::publishedNode() { return s_publishedNode; }

bool ShotUploadService::connected() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, publishedNode());
}

QVariantMap ShotUploadService::snapshot() const
{
    return connected() && m_state->ready() ? m_state->snapshot() : QVariantMap();
}

bool ShotUploadService::ready() const { return connected() && m_state->ready(); }

void ShotUploadService::setNode(QRemoteObjectNode* node)
{
    if (m_node == node && m_state && (node || !m_nodeDestroyed))
        return;
    disconnect(m_nodeDestroyed);
    m_nodeDestroyed = {};
    failPending(QStringLiteral("disconnected"));
    m_decentChoiceOwners.clear();
    if (m_state) {
        auto* old = m_state.data();
        disconnect(old, nullptr, this, nullptr);
        old->deleteLater();
    }
    m_node = node;
    if (node) {
        m_nodeDestroyed = connect(node, &QObject::destroyed, this,
                                  [this] { setNode(nullptr); });
        m_state = node->acquire<ShotUploadStateReplica>();
    } else {
        m_state = new ShotUploadStateReplica;
    }
    if (m_state) {
        m_state->setParent(this);
        connect(m_state, &QRemoteObjectReplica::stateChanged,
                this, &ShotUploadService::changed);
        connect(m_state, &QRemoteObjectReplica::notified,
                this, &ShotUploadService::changed);
        connect(m_state, &ShotUploadStateReplica::snapshotChanged,
                this, &ShotUploadService::stateChanged);
        connect(m_state, &ShotUploadStateReplica::readyChanged,
                this, &ShotUploadService::stateChanged);
        connect(m_state, &ShotUploadStateReplica::operationCompleted, this,
                [this](const QString& id, const QString& operation, bool success,
                       const QVariantMap& result, const QString& reason) {
            if (!m_pending.contains(id) || m_pending.value(id) != operation)
                return;
            if (operation == QLatin1String("openDecentAccount") && success) {
                const QString receipt = m_privateReceipts.value(id);
                if (receipt.isEmpty()) {
                    m_pending.remove(id);
                    emit busyChanged();
                    emit operationCompleted(id, operation, false, {},
                                            QStringLiteral("privateResultUnavailable"));
                    return;
                }
                auto* watcher = new QRemoteObjectPendingCallWatcher(
                    m_state->takePrivateResult(receipt), this);
                connect(watcher, &QRemoteObjectPendingCallWatcher::finished, this,
                        [this, watcher, id, operation] {
                    watcher->deleteLater();
                    if (!m_pending.contains(id) || m_pending.value(id) != operation)
                        return;
                    m_pending.remove(id);
                    m_privateReceipts.remove(id);
                    QVariantMap privateResult;
                    QString failure;
                    if (watcher->error() == QRemoteObjectPendingCall::NoError) {
                        privateResult = watcher->returnValue().toMap();
                        const QString url = privateResult.value(QStringLiteral("url")).toString();
                        if (url.isEmpty() || url.size() > 4096) {
                            privateResult.clear();
                            failure = QStringLiteral("privateResultUnavailable");
                        }
                    } else {
                        failure = QStringLiteral("disconnected");
                    }
                    emit busyChanged();
                    emit operationCompleted(id, operation, failure.isEmpty(), privateResult,
                                            failure);
                });
                return;
            }
            m_pending.remove(id);
            m_privateReceipts.remove(id);
            if (operation == QLatin1String("linkDecent")
                && (!success || result.value(QStringLiteral("errorCode")).toInt() != 0))
                m_decentChoiceOwners.remove(id);
            if (operation == QLatin1String("unlinkDecent"))
                m_decentChoiceOwners.clear();
            emit busyChanged();
            emit operationCompleted(id, operation, success, result, reason);
        }, Qt::QueuedConnection);
        connect(m_state, &ShotUploadStateReplica::recoveryProgress,
                this, &ShotUploadService::recoveryProgress, Qt::QueuedConnection);
        connect(m_state, &ShotUploadStateReplica::recoveryComplete,
                this, &ShotUploadService::recoveryComplete, Qt::QueuedConnection);
        connect(m_state, &ShotUploadStateReplica::machineChoiceNeeded, this,
                [this](const QString& requestId, const QStringList& serials,
                       const QStringList& labels) {
            // Native QML keeps its established root-level modal. The browser
            // shell handles this requester-owned event with the same chooser.
            if (m_node != publishedNode() && m_decentChoiceOwners.remove(requestId))
                emit machineChoiceNeeded(requestId, serials, labels);
        }, Qt::QueuedConnection);
    }
    changed();
}

void ShotUploadService::changed()
{
    if (!connected()) {
        failPending(QStringLiteral("disconnected"));
        m_decentChoiceOwners.clear();
    }
    emit stateChanged();
}

void ShotUploadService::failPending(const QString& reason)
{
    if (m_pending.isEmpty())
        return;
    const auto pending = m_pending;
    m_pending.clear();
    m_privateReceipts.clear();
    emit busyChanged();
    for (auto it = pending.cbegin(); it != pending.cend(); ++it)
        emit operationCompleted(it.key(), it.value(), false, {}, reason);
}

QString ShotUploadService::newRequestId() const { return ServiceOperation::newId(); }

QString ShotUploadService::execute(const QString& operation, const QVariantMap& arguments)
{
    const QString id = newRequestId();
    if (!connected() || !m_state->ready()) {
        QMetaObject::invokeMethod(this, [this, id, operation] {
            emit operationCompleted(id, operation, false, {}, QStringLiteral("disconnected"));
        }, Qt::QueuedConnection);
        return id;
    }
    if (m_pending.size() >= 16) {
        QMetaObject::invokeMethod(this, [this, id, operation] {
            emit operationCompleted(id, operation, false, {}, QStringLiteral("tooManyRequests"));
        }, Qt::QueuedConnection);
        return id;
    }
    static const QStringList allowed{
        QStringLiteral("connectVisualizer"), QStringLiteral("disconnectVisualizer"),
        QStringLiteral("linkDecent"), QStringLiteral("unlinkDecent"),
        QStringLiteral("openDecentAccount"), QStringLiteral("recoverVisualizer"),
        QStringLiteral("uploadMissing"), QStringLiteral("chooseDecentMachine")};
    if (!allowed.contains(operation)) {
        QMetaObject::invokeMethod(this, [this, id, operation] {
            emit operationCompleted(id, operation, false, {}, QStringLiteral("unknownOperation"));
        }, Qt::QueuedConnection);
        return id;
    }
    QVariantMap commandArguments = arguments;
    if (operation == QLatin1String("openDecentAccount")) {
        const QString receipt = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
        m_privateReceipts.insert(id, receipt);
        commandArguments.insert(QStringLiteral("receipt"), receipt);
    }
    m_pending.insert(id, operation);
    if (operation == QLatin1String("linkDecent"))
        m_decentChoiceOwners.insert(id);
    else if (operation == QLatin1String("unlinkDecent"))
        m_decentChoiceOwners.clear();
    emit busyChanged();
    m_state->execute(id, operation, commandArguments);
    return id;
}

void ShotUploadService::refresh()
{
    if (connected())
        m_state->refresh();
}
