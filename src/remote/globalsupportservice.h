#pragma once

#include "remoteservice.h"
#include "rep_globalsupportservice_replica.h"

#include <QHash>
#include <QMetaObject>
#include <QPointer>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

class QRemoteObjectNode;

class GlobalSupportService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(GlobalSupportStateReplica* state READ state NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(qint64 mcpConfirmationDeadlineMs READ mcpConfirmationDeadlineMs
               NOTIFY stateChanged FINAL)

public:
    GlobalSupportStateReplica* state() const { return m_state; }
    bool ready() const;
    qint64 mcpConfirmationDeadlineMs() const;

    void setNode(QRemoteObjectNode* node) override;
    static void setPublishedNode(QRemoteObjectNode* node);

    Q_INVOKABLE QString newRequestId() const;
    Q_INVOKABLE QString execute(const QString& operation,
                                const QVariantMap& arguments = {});
    Q_INVOKABLE QString openCrashReport();
    Q_INVOKABLE void refresh();

signals:
    void stateChanged();
    void operationCompleted(const QString& requestId, const QString& operation,
                            bool success, const QVariantMap& result,
                            const QString& reason);

private:
    friend struct GlobalSupportServiceForeign;
    explicit GlobalSupportService(QRemoteObjectNode* node, QObject* parent);

    static QRemoteObjectNode* publishedNode();
    bool connected() const;
    void failPending();

    QPointer<QRemoteObjectNode> m_node;
    QPointer<GlobalSupportStateReplica> m_state;
    QMetaObject::Connection m_nodeDestroyed;
    QHash<QString, QString> m_pending;
};
