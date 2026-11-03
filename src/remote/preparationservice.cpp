#include "preparationservice.h"

#include "serviceoperation.h"
#include "core/diagnosticlogging.h"

#include <QJsonObject>
#include <QMetaObject>
#include <utility>

namespace { QPointer<QRemoteObjectNode> s_publishedNode; }

PreparationServiceView::PreparationServiceView(PreparationService* service)
    : PreparationState(service), m_service(service) {}

bool PreparationServiceView::selectMode(const QString& mode)
{ return PreparationSnapshot::isMode(mode) && m_service->send(mode); }
bool PreparationServiceView::startMilkAttempt(const QString& surface)
{ return m_service->sendMilk(QStringLiteral("milk-start"), surface); }
bool PreparationServiceView::cancelMilkAttempt(const QString& surface)
{ return m_service->sendMilk(QStringLiteral("milk-cancel"), surface); }
bool PreparationServiceView::setMilkTimeLocked(bool locked)
{ return m_service->send(locked ? QStringLiteral("milk-lock") : QStringLiteral("milk-unlock")); }

PreparationService::PreparationService(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent), m_view(this)
{
    connect(&m_view, &PreparationState::availableChanged, this, &PreparationService::readyChanged);
    setNode(node);
}

PreparationService::~PreparationService()
{
    if (m_replica) disconnect(m_replica, nullptr, this, nullptr);
}

void PreparationService::setPublishedNode(QRemoteObjectNode* node) { s_publishedNode = node; }
QRemoteObjectNode* PreparationService::publishedNode() { return s_publishedNode; }

void PreparationService::setNode(QRemoteObjectNode* node)
{
    if (!RemoteServiceReplica::replace(this, node, m_node, m_replica, m_nodeDestroyed,
            [this] {
                failPending();
                m_sourceId.clear();
                m_view.update({}, false, false);
            }))
        return;
    connect(m_replica, &QRemoteObjectReplica::stateChanged, this, &PreparationService::refresh);
    connect(m_replica, &QRemoteObjectReplica::notified, this, &PreparationService::refresh);
    connect(m_replica, &PreparationServiceStateReplica::sourceIdChanged,
            this, &PreparationService::refresh);
    connect(m_replica, &PreparationServiceStateReplica::readyChanged,
            this, &PreparationService::refresh);
    connect(m_replica, &PreparationServiceStateReplica::snapshotChanged,
            this, &PreparationService::refresh);
    connect(m_replica, &PreparationServiceStateReplica::operationCompleted, this,
        [this](const QString& requestId, const QString& action, bool success,
               const QString& message) {
            if (!m_pending.contains(requestId) || m_pending.value(requestId) != action) return;
            m_pending.remove(requestId);
            report(requestId, action, success, message);
        }, Qt::QueuedConnection);
    refresh();
}

void PreparationService::refresh()
{
    PreparationSnapshot snapshot;
    const bool published = RemoteServiceReplica::isReady(m_node, m_replica, s_publishedNode)
        && m_replica->ready() && !m_replica->sourceId().isEmpty();
    QString parseError;
    const bool available = published
        && PreparationSnapshot::parse(QJsonObject::fromVariantMap(m_replica->snapshot()),
                                      &snapshot, &parseError);
    if (published && !available && parseError != m_loggedParseError)
        DIAG_WARN(APP, "PreparationService") << "Published preparation state rejected:" << parseError;
    m_loggedParseError = published ? parseError : QString();
    if (!available) {
        m_sourceId.clear();
        m_view.update({}, false, false);
        failPending();
        return;
    }
    const QString sourceId = m_replica->sourceId();
    const bool deliverEvents = m_view.available() && sourceId == m_sourceId;
    if (!m_sourceId.isEmpty() && sourceId != m_sourceId) failPending();
    m_sourceId = sourceId;
    m_view.update(snapshot, true, deliverEvents);
}

// The client id lets the host release this client's offers when its session closes.
bool PreparationService::sendMilk(const QString& action, const QString& surface)
{
    const QString clientId = m_node ? m_node->property("decenzaClientId").toString() : QString();
    if (clientId.isEmpty() || surface.isEmpty()) {
        const QString requestId = ServiceOperation::newId();
        QMetaObject::invokeMethod(this, [this, requestId, action] {
            report(requestId, action, false, tr("The milk sample has no owner."));
        }, Qt::QueuedConnection);
        return false;
    }
    return send(action, PreparationSnapshot::milkOwner(clientId, surface));
}

bool PreparationService::send(const QString& action, const QString& owner)
{
    const QString requestId = ServiceOperation::newId();
    if (!ready() || m_pending.size() >= 128) {
        const QString message = ready() ? tr("Too many preparation requests.")
                                         : tr("Preparation is unavailable.");
        QMetaObject::invokeMethod(this, [this, requestId, action, message] {
            report(requestId, action, false, message);
        }, Qt::QueuedConnection);
        return false;
    }
    m_pending.insert(requestId, action);
    m_replica->execute(requestId, action, owner);
    return true;
}

void PreparationService::failPending()
{
    const auto pending = std::exchange(m_pending, {});
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        const QString requestId = it.key();
        const QString action = it.value();
        QMetaObject::invokeMethod(this, [this, requestId, action] {
            report(requestId, action, false,
                   tr("Connection lost; the preparation action was not retried."));
        }, Qt::QueuedConnection);
    }
}

void PreparationService::report(const QString& requestId, const QString& action,
                                bool success, const QString& message)
{
    emit completed(requestId, action, success, message);
    if (!success) emit m_view.actionFailed(action, message);
}
