#pragma once

#include "remoteservice.h"
#include "rep_systemlogservice_replica.h"

#include <QHash>
#include <QPointer>
#include <QtQmlIntegration/qqmlintegration.h>

class QObject;
class QJSEngine;
class QQmlEngine;
class QRemoteObjectNode;

class SystemLogService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(SystemLogStateReplica* state READ state NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)

public:
    ~SystemLogService() override;
    SystemLogStateReplica* state() const { return m_state; }
    bool ready() const;
    void setNode(QRemoteObjectNode* node) override;
    static void setPublishedNode(QRemoteObjectNode* node);

    Q_INVOKABLE void setViewActive(QObject* view, bool active);
    Q_INVOKABLE QString requestSnapshot(const QStringList& markers,
                                       const QString& minLevel, int maxLines);
    Q_INVOKABLE QString requestBoundary();
    Q_INVOKABLE bool lineMatches(const QString& line, const QStringList& markers,
                                 const QString& minLevel) const;

signals:
    void stateChanged();
    void snapshotCompleted(const QString& requestId, bool success,
                           const QStringList& lines, qint64 sequence,
                           const QString& reason);
    void boundaryCompleted(const QString& requestId, bool success,
                           qint64 sequence, const QString& reason);
    void lineAppended(const QString& line, qint64 sequence);

private:
    friend struct SystemLogServiceForeign;
    explicit SystemLogService(QRemoteObjectNode* node, QObject* parent = nullptr);
    static QRemoteObjectNode* publishedNode();

    QPointer<QRemoteObjectNode> m_node;
    QPointer<SystemLogStateReplica> m_state;
    void syncReplica();
    void releaseReplica();

    QHash<QObject*, QMetaObject::Connection> m_activeViews;
    QMetaObject::Connection m_nodeDestroyed;
    quint64 m_nodeGeneration = 0;
};
