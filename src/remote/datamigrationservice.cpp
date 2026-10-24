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
    if (m_node == node && m_state && (node || !m_nodeDestroyed)) return;
    disconnect(m_nodeDestroyed);
    m_nodeDestroyed = {};
    failPending(QStringLiteral("disconnected"));
    if (m_state) {
        auto* old = m_state.data();
        disconnect(old, nullptr, this, nullptr);
        old->deleteLater();
    }
    m_node = node;
    if (node) {
        m_nodeDestroyed = connect(node, &QObject::destroyed, this, [this] { setNode(nullptr); });
        m_state = node->acquire<DataMigrationStateReplica>();
    } else {
        m_state = new DataMigrationStateReplica;
    }
    if (m_state) {
        m_state->setParent(this);
        connect(m_state, &QRemoteObjectReplica::stateChanged, this, &DataMigrationService::changed);
        connect(m_state, &QRemoteObjectReplica::notified, this, &DataMigrationService::changed);
        connect(m_state, &DataMigrationStateReplica::snapshotChanged, this, &DataMigrationService::stateChanged);
        connect(m_state, &DataMigrationStateReplica::readyChanged, this, &DataMigrationService::stateChanged);
        connect(m_state, &DataMigrationStateReplica::operationCompleted, this,
                [this](const QString& id, const QString& operation, bool success,
                       const QVariantMap& result, const QString& reason) {
            if (!m_pending.finish(id, operation)) return;
            emit busyChanged();
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
    if (!connected()) failPending(QStringLiteral("disconnected"));
    emit stateChanged();
}

void DataMigrationService::failPending(const QString& reason)
{
    m_pending.failAll([this] { emit busyChanged(); },
                      [this, reason](const QString& id, const QString& operation) {
        emit operationCompleted(id, operation, false, {}, reason);
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
    m_pending.insert(id, operation);
    emit busyChanged();
    m_state->execute(id, operation, arguments);
    return id;
}

void DataMigrationService::refresh() { if (connected()) m_state->refresh(); }
