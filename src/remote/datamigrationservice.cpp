#include "datamigrationservice.h"
#include "datamigrationcommandpolicy.h"

#include <QJSEngine>
#include <QMetaObject>
#include <QRemoteObjectNode>
#include <QQmlEngine>

namespace { QPointer<QRemoteObjectNode> s_publishedNode; }

DataMigrationService* DataMigrationService::create(QQmlEngine* engine, QJSEngine*)
{
    auto* service = new DataMigrationService(s_publishedNode, engine);
    QJSEngine::setObjectOwnership(service, QJSEngine::CppOwnership);
    return service;
}

DataMigrationService::DataMigrationService(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent) { setNode(node); }

DataMigrationService::~DataMigrationService()
{
    if (m_state) disconnect(m_state, nullptr, this, nullptr);
}

void DataMigrationService::setPublishedNode(QRemoteObjectNode* node) { s_publishedNode = node; }
QRemoteObjectNode* DataMigrationService::publishedNode() { return s_publishedNode; }

bool DataMigrationService::connected() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, publishedNode());
}

QVariantMap DataMigrationService::snapshot() const
{
    return connected() && m_state->ready() ? m_state->snapshot() : QVariantMap();
}

bool DataMigrationService::ready() const { return connected() && m_state->ready(); }

void DataMigrationService::setNode(QRemoteObjectNode* node)
{
    if (!RemoteServiceReplica::replace(this, node, m_node, m_state, m_nodeDestroyed,
            [this] { failPending(QStringLiteral("disconnected")); }))
        return;
    if (m_state) {
        connect(m_state, &QRemoteObjectReplica::stateChanged, this, &DataMigrationService::changed);
        connect(m_state, &QRemoteObjectReplica::notified, this, &DataMigrationService::changed);
        connect(m_state, &DataMigrationStateReplica::snapshotChanged, this, &DataMigrationService::stateChanged);
        connect(m_state, &DataMigrationStateReplica::readyChanged, this, &DataMigrationService::stateChanged);
        connect(m_state, &DataMigrationStateReplica::operationCompleted, this,
                [this](const QString& id, const QString& operation, bool success,
                       const QVariantMap& result, const QString& reason) {
            if (!m_pending.finish(id, operation)) return;
            const QPointer<DataMigrationService> completionGuard(this);
            emit busyChanged();
            if (!completionGuard) return;
            emit operationCompleted(id, operation, success, result, reason);
        }, Qt::QueuedConnection);
        connect(m_state, &DataMigrationStateReplica::operationProgress, this,
                [this](const QString& id, double progress, const QString& operation) {
            if (m_pending.contains(id)) emit operationProgress(id, progress, operation);
        }, Qt::QueuedConnection);
    }
    changed();
}

void DataMigrationService::changed()
{
    const QPointer<DataMigrationService> guard(this);
    if (!connected()) failPending(QStringLiteral("disconnected"));
    if (guard) emit stateChanged();
}

void DataMigrationService::failPending(const QString& reason)
{
    const QPointer<DataMigrationService> guard(this);
    m_pending.failAll([guard] { if (guard) emit guard->busyChanged(); },
                      [guard, reason](const QString& id, const QString& operation) {
        if (guard) emit guard->operationCompleted(id, operation, false, {}, reason);
    });
}

QString DataMigrationService::newRequestId() const { return ServiceOperation::newId(); }

QString DataMigrationService::execute(const QString& operation, const QVariantMap& arguments)
{
    const QString id = newRequestId();
    QString reason;
    if (!DataMigrationCommandPolicy::validate(operation, arguments, &reason)) {
        QMetaObject::invokeMethod(this, [this, id, operation, reason] {
            emit operationCompleted(id, operation, false, {}, reason);
        }, Qt::QueuedConnection);
        return id;
    }
    if (!connected() || !m_state->ready()) {
        QMetaObject::invokeMethod(this, [this, id, operation] {
            emit operationCompleted(id, operation, false, {}, QStringLiteral("disconnected"));
        }, Qt::QueuedConnection);
        return id;
    }
    const bool interrupt = operation == QLatin1String("cancel")
        || operation == QLatin1String("disconnect") || operation == QLatin1String("stopDiscovery");
    if (!m_pending.isEmpty() && !interrupt) {
        QMetaObject::invokeMethod(this, [this, id, operation] {
            emit operationCompleted(id, operation, false, {}, QStringLiteral("busy"));
        }, Qt::QueuedConnection);
        return id;
    }
    if (m_pending.size() >= 16) {
        QMetaObject::invokeMethod(this, [this, id, operation] {
            emit operationCompleted(id, operation, false, {}, QStringLiteral("tooManyRequests"));
        }, Qt::QueuedConnection);
        return id;
    }
    const QPointer<DataMigrationStateReplica> requested = m_state;
    const QPointer<DataMigrationService> guard(this);
    m_pending.insert(id, operation);
    emit busyChanged();
    if (!guard || requested != m_state || !m_pending.contains(id)) return id;
    if (!ready()) { failPending(QStringLiteral("disconnected")); return id; }
    requested->execute(id, operation, arguments);
    return id;
}

void DataMigrationService::refresh() { if (connected()) m_state->refresh(); }
