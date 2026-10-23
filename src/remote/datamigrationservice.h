#pragma once

#include "remoteservice.h"
#include "rep_datamigrationservice_replica.h"
#include <QHash>
#include <QMetaObject>
#include <QPointer>
#include <QVariantMap>

class QQmlEngine;
class QJSEngine;
class QRemoteObjectNode;

class DataMigrationService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
public:
    ~DataMigrationService() override;
    static DataMigrationService* create(QQmlEngine* engine, QJSEngine* jsEngine);
    static void setPublishedNode(QRemoteObjectNode* node);
    QVariantMap snapshot() const;
    bool ready() const;
    bool busy() const { return !m_pending.isEmpty(); }
    void setNode(QRemoteObjectNode* node) override;
    Q_INVOKABLE QString newRequestId() const;
    Q_INVOKABLE QString execute(const QString& operation, const QVariantMap& arguments = {});
    Q_INVOKABLE void refresh();
signals:
    void stateChanged();
    void busyChanged();
    void operationCompleted(const QString& requestId, const QString& operation,
                            bool success, const QVariantMap& result, const QString& reason);
    void operationProgress(const QString& requestId, double progress, const QString& currentOperation);
private:
    friend struct DataMigrationServiceForeign;
    explicit DataMigrationService(QRemoteObjectNode* node, QObject* parent);
    static QRemoteObjectNode* publishedNode();
    bool connected() const;
    void changed();
    void failPending(const QString& reason);
    QPointer<QRemoteObjectNode> m_node;
    QPointer<DataMigrationStateReplica> m_state;
    QMetaObject::Connection m_nodeDestroyed;
    QHash<QString, QString> m_pending;
};
