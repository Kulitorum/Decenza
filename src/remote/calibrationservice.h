#pragma once

#include "remoteservice.h"
#include "rep_calibrationservice_replica.h"

#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <QHash>
#include <QtQmlIntegration/qqmlintegration.h>

class QRemoteObjectNode;
class QQmlEngine;

class CalibrationService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    Q_PROPERTY(qint64 revision READ revision NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged FINAL)
public:
    ~CalibrationService() override;
    bool ready() const;
    bool busy() const { return !m_pending.isEmpty(); }
    qint64 revision() const;
    QVariantMap snapshot() const;
    QString errorMessage() const;
    void setNode(QRemoteObjectNode* node) override;
    static void setPublishedNode(QRemoteObjectNode* node);
    static CalibrationService* create(QQmlEngine* engine);
    Q_INVOKABLE QString newRequestId() const;
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString execute(const QString& operation, const QVariantMap& arguments = {});
signals:
    void stateChanged();
    void busyChanged();
    void operationCompleted(const QString& requestId, const QString& operation,
                            bool success, const QVariantMap& result,
                            const QString& reason, qint64 revision);
private:
    explicit CalibrationService(QRemoteObjectNode* node, QObject* parent);
    static QRemoteObjectNode* publishedNode();
    bool connected() const;
    void updateFromReplica();
    void failPending(const QString& reason);
    QPointer<CalibrationStateReplica> m_state;
    QPointer<QRemoteObjectNode> m_node;
    QMetaObject::Connection m_nodeDestroyed;
    QHash<QString, QString> m_pending;
    QVariantMap m_snapshot;
    QString m_error;
    qint64 m_revision = -1;
    bool m_ready = false;
    bool m_busy = false;
    friend struct CalibrationServiceForeign;
};

