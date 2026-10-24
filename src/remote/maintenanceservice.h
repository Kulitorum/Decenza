#pragma once

#include "remoteservice.h"
#include "rep_maintenanceservice_replica.h"

#include <QHash>
#include <QPointer>
#include <QVariantMap>

class QRemoteObjectNode;
class QQmlEngine;

class MaintenanceService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(qint64 revision READ revision NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
public:
    ~MaintenanceService() override;
    bool ready() const;
    qint64 revision() const;
    QVariantMap snapshot() const;
    QString errorMessage() const;
    bool busy() const { return !m_pending.isEmpty(); }

    void setNode(QRemoteObjectNode* node) override;
    static void setPublishedNode(QRemoteObjectNode* node);
    static QRemoteObjectNode* publishedNode();
    static MaintenanceService* create(QQmlEngine* engine);

    Q_INVOKABLE QString newRequestId() const;
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString execute(const QString& operation,
                                const QVariantMap& arguments = {},
                                const QString& requestId = {});

signals:
    void stateChanged();
    void busyChanged();
    void operationCompleted(const QString& requestId, const QString& operation,
                            bool success, const QVariantMap& result,
                            const QString& reason, qint64 revision);

private:
    friend struct MaintenanceServiceForeign;
    explicit MaintenanceService(QRemoteObjectNode* node, QObject* parent);
    bool connected() const;
    void updateFromReplica();
    void failPending(const QString& reason);

    QPointer<QRemoteObjectNode> m_node;
    QPointer<MaintenanceStateReplica> m_state;
    QMetaObject::Connection m_nodeDestroyed;
    QHash<QString, QString> m_pending;
    QVariantMap m_snapshot;
    QString m_error;
    qint64 m_revision = -1;
    bool m_ready = false;
};
