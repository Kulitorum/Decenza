#pragma once
#include "remoteservice.h"

#include "rep_bagservice_replica.h"
#include "serviceoperation.h"

#include <QObject>
#include <QHash>
#include <QPointer>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

class QRemoteObjectNode;

class BagService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(QVariantList bags READ bags NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantList finishedBags READ finishedBags NOTIFY stateChanged FINAL)
    Q_PROPERTY(qint64 activeBagId READ activeBagId NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString operationError READ operationError NOTIFY stateChanged FINAL)

public:
    ~BagService() override;

    QVariantList bags() const;
    QVariantList finishedBags() const;
    qint64 activeBagId() const;
    bool ready() const;
    bool loading() const;
    bool busy() const { return m_operation.busy(); }
    QString errorMessage() const;
    QString operationError() const { return m_operation.error(); }
    void setNode(QRemoteObjectNode* node) override;
    static void setPublishedNode(QRemoteObjectNode* node);

    Q_INVOKABLE QString newRequestId() const;
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void requestInventory();
    Q_INVOKABLE void requestFinishedBags();
    Q_INVOKABLE void requestFinishedBagCount();
    Q_INVOKABLE void requestBag(qint64 bagId);
    Q_INVOKABLE void requestCreateBag(const QVariantMap& bag);
    Q_INVOKABLE QString requestUpdateBag(qint64 bagId, const QVariantMap& fields,
                                         bool propagateBeanBase = false,
                                         const QVariantMap& expectedEditable = {},
                                         qint64 expectedActiveBagId = -1);
    Q_INVOKABLE void requestMarkEmpty(qint64 bagId);
    Q_INVOKABLE void requestMarkAiPageSearched(qint64 bagId);
    Q_INVOKABLE void requestTouchLastUsed(qint64 bagId);
    Q_INVOKABLE void requestDeleteBag(qint64 bagId);
    Q_INVOKABLE void requestActivateBag(qint64 bagId);

    Q_INVOKABLE QVariantList storageHintOptions() const;
    Q_INVOKABLE QVariantList lifecycleParts(const QVariantMap& bag,
                                           const QString& referenceIso = QString()) const;
    Q_INVOKABLE QVariantMap restockTemplate(const QVariantMap& bag) const;
    Q_INVOKABLE QVariantMap editChanges(const QVariantMap& opened,
                                        const QVariantMap& current,
                                        bool replaceBlob) const;

signals:
    void stateChanged();
    void busyChanged();
    void inventoryReady(const QVariantList& bags);
    void inventoryFailed();
    void finishedBagsReady(const QVariantList& bags);
    void finishedBagsFailed();
    void finishedBagCountReady(int count);
    void finishedBagCountFailed();
    void bagReady(qint64 bagId, const QVariantMap& bag);
    void bagReadFailed(qint64 bagId);
    void bagCreated(qint64 bagId, const QVariantMap& bag);
    void bagActivated(qint64 bagId, bool success, const QVariantMap& bag, const QString& reason);
    void bagUpdated(qint64 bagId, bool success, const QString& refusal);
    void bagDeleted(qint64 bagId, bool success);
    void bagRestocked(qint64 bagId);
    void bagFinished(qint64 bagId);
    void bagPulledFromVisualizer(qint64 bagId);
    void bagVisualizerFieldsChanged(qint64 bagId);
    void bagsChanged();
    void errorOccurred(const QString& message);
    void operationFailed(qint64 bagId, const QString& operation, const QString& reason);
    void operationCompleted(const QString& requestId, const QString& operation, qint64 bagId,
                            bool success, const QVariantMap& result, const QString& reason);

private:
    friend struct BagServiceForeign;
    explicit BagService(QRemoteObjectNode* node, QObject* parent);
    static QRemoteObjectNode* publishedNode();
    bool connected() const;
    void changed();
    void failPending();
    QString send(const QString& operation, qint64 bagId, const QVariantMap& fields,
                 bool propagateBeanBase, const QVariantMap& expectedEditable = {},
                 qint64 expectedActiveBagId = -1);
    void report(const QString& requestId, const QString& operation, qint64 bagId,
                bool success, const QVariantMap& result, const QString& reason);

    struct PendingRead { QString operation; qint64 bagId = -1; };
    QPointer<QRemoteObjectNode> m_node;
    QPointer<BagStateReplica> m_state;
    QMetaObject::Connection m_nodeDestroyed;
    ServiceOperation m_operation;
    QHash<QString, PendingRead> m_reads;
    QVariantList m_notifiedInventory;
    bool m_inventoryWasReady = false;
};
