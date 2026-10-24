#pragma once
#include "remoteservice.h"

#include "rep_equipmentservice_replica.h"
#include "serviceoperation.h"
#include <QObject>
#include <QPointer>
#include <QHash>
#include <QSet>

class QRemoteObjectNode;

class EquipmentService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(QVariantList packages READ packages NOTIFY packagesChanged FINAL)
    Q_PROPERTY(QVariantMap options READ options NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantMap createSeed READ createSeed NOTIFY stateChanged FINAL)
    Q_PROPERTY(qint64 activePackageId READ activePackageId NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantList puckPrepFlags READ puckPrepFlags NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString operationError READ operationError NOTIFY stateChanged FINAL)
public:
    ~EquipmentService() override;
    QVariantList packages() const;
    QVariantMap options() const;
    QVariantMap createSeed() const;
    qint64 activePackageId() const;
    QVariantList puckPrepFlags() const;
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
    Q_INVOKABLE void requestPackage(qint64 packageId);
    Q_INVOKABLE void requestArchived();
    Q_INVOKABLE void requestGrindCalibration(qint64 packageId);
    Q_INVOKABLE void requestGrindSuggestion(qint64 packageId, const QString& beanBrand,
                                           const QString& beanType, const QString& roastDate,
                                           const QString& profileTitle);
    Q_INVOKABLE void requestSetGrindAnchor(qint64 shotId, const QString& side,
                                          qint64 expectedPackageId);
    Q_INVOKABLE void requestClearGrindAnchor(qint64 packageId, const QString& side);
    Q_INVOKABLE void createPackage(const QVariantMap& fields, bool applyToActiveBag, const QString& requestId);
    Q_INVOKABLE void updatePackage(qint64 id, const QVariantMap& fields,
                                   const QVariantMap& expectedEditable, const QString& requestId);
    Q_INVOKABLE void activatePackage(qint64 id, bool applyToActiveBag, const QString& requestId = {});
    Q_INVOKABLE void requestMarkRemoved(qint64 id);
    Q_INVOKABLE void requestDeletePackage(qint64 id);
    Q_INVOKABLE void requestRestorePackage(qint64 id);
signals:
    void stateChanged();
    void packagesChanged();
    void busyChanged();
    void inventoryReady(const QVariantList& packages);
    void inventoryFailed(const QString& reason);
    void archivedReady(const QVariantList& packages);
    void archivedFailed(const QString& reason);
    void grindCalibrationReady(qint64 packageId, const QVariantMap& calibration);
    void grindSuggestionReady(const QVariantMap& suggestion);
    void grindAnchorMarked(qint64 shotId, const QString& side, const QString& error);
    void grindAnchorCleared(qint64 packageId, const QString& side, bool ok);
    void grindCalibrationChanged(qint64 packageId);
    void packageReady(qint64 packageId, const QVariantMap& package);
    void packageFailed(qint64 packageId, const QString& reason);
    void operationCompleted(const QString& requestId, bool success, qint64 resultId, const QString& reason);
    void packageDeleted(qint64 packageId, bool success);
    void operationFailed(qint64 packageId, const QString& operation, const QString& reason);
private:
    friend struct EquipmentServiceForeign;
    explicit EquipmentService(QRemoteObjectNode* node, QObject* parent);
    static QRemoteObjectNode* publishedNode();
    bool connected() const;
    void changed();
    void failPending();
    void requestGrindContext(const QString& kind, const QVariantMap& query);
    void reportGrindContext(const QVariantMap& query, QVariantMap result, const QString& reason);
    void send(const QString& operation, qint64 id, const QVariantMap& fields,
              const QVariantMap& expected, bool apply, const QString& requestId);
    void report(const QString& requestId, const QString& operation, qint64 id,
                bool success, qint64 resultId, const QString& reason);
    QPointer<QRemoteObjectNode> m_node;
    QPointer<EquipmentStateReplica> m_state;
    QMetaObject::Connection m_nodeDestroyed;
    ServiceOperation m_operation;
    QHash<QString, qint64> m_reads;
    QSet<QString> m_archivedReads;
    QHash<QString, QVariantMap> m_grindReads;
    QHash<QString, QString> m_grindWrites;
    QVariantList m_notifiedPackages;
};
