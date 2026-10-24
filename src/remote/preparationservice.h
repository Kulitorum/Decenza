#pragma once

#include "remoteservice.h"
#include "core/preparationstate.h"
#include "rep_preparationservice_replica.h"

#include <QHash>
#include <QPointer>

class PreparationService;

class PreparationServiceView final : public PreparationState
{
public:
    explicit PreparationServiceView(PreparationService* service);
    bool selectMode(const QString& mode) override;
    bool startMilkAttempt() override;
    bool cancelMilkAttempt() override;
    bool setMilkTimeLocked(bool locked) override;
    void update(const PreparationSnapshot& snapshot, bool available, bool deliverEvents)
    { applySnapshot(snapshot, available, deliverEvents); }
private:
    PreparationService* m_service;
};

class PreparationService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(PreparationState* state READ state CONSTANT FINAL)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged FINAL)
public:
    ~PreparationService() override;
    PreparationState* state() { return &m_view; }
    bool ready() const { return m_view.available(); }
    void setNode(QRemoteObjectNode* node) override;
    static void setPublishedNode(QRemoteObjectNode* node);

signals:
    void readyChanged();
    void completed(const QString& requestId, const QString& action,
                   bool success, const QString& message);

private:
    friend class PreparationServiceView;
    friend struct PreparationServiceForeign;
    explicit PreparationService(QRemoteObjectNode* node, QObject* parent);
    static QRemoteObjectNode* publishedNode();
    void refresh();
    bool send(const QString& action);
    void failPending();
    void report(const QString& requestId, const QString& action,
                bool success, const QString& message);

    QPointer<QRemoteObjectNode> m_node;
    QPointer<PreparationServiceStateReplica> m_replica;
    QMetaObject::Connection m_nodeDestroyed;
    PreparationServiceView m_view;
    QHash<QString, QString> m_pending;
    QString m_sourceId;
};
