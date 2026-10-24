#pragma once

#include "remoteservice.h"
#include "rep_mqttservice_replica.h"

#include "serviceoperation.h"
#include <QPointer>
#include <QVariantMap>

class QQmlEngine;
class QJSEngine;
class QRemoteObjectNode;

class MqttService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(qint64 revision READ revision NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
public:
    static MqttService* create(QQmlEngine* engine, QJSEngine* jsEngine);
    static void setPublishedNode(QRemoteObjectNode* node);

    bool ready() const;
    qint64 revision() const;
    QVariantMap snapshot() const;
    bool busy() const { return !m_pending.isEmpty(); }
    void setNode(QRemoteObjectNode* node) override;

    Q_INVOKABLE QString newRequestId() const;
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString execute(const QString& operation,
                                const QVariantMap& arguments = {},
                                qint64 expectedRevision = -1,
                                const QString& requestId = {});

signals:
    void stateChanged();
    void busyChanged();
    void operationCompleted(const QString& requestId, const QString& operation,
                            bool success, const QVariantMap& result,
                            const QString& reason, qint64 revision);

private:
    friend struct MqttServiceForeign;
    explicit MqttService(QRemoteObjectNode* node, QObject* parent);
    static QRemoteObjectNode* publishedNode();
    bool connected() const;
    void updateFromReplica();
    void failPending(const QString& reason);

    QPointer<QRemoteObjectNode> m_node;
    QPointer<MqttStateReplica> m_state;
    QMetaObject::Connection m_nodeDestroyed;
    ServiceRequests m_pending;
    QVariantMap m_snapshot;
    qint64 m_revision = -1;
    bool m_ready = false;
};
