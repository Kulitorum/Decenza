#pragma once

#include "remoteservice.h"
#include "rep_flowcalibrationservice_replica.h"

#include "serviceoperation.h"
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

class QRemoteObjectNode;
class QQmlEngine;

class FlowCalibrationService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged FINAL)
    Q_PROPERTY(double savedMultiplier READ savedMultiplier NOTIFY stateChanged FINAL)
    Q_PROPERTY(qint64 calibrationRevision READ calibrationRevision NOTIFY stateChanged FINAL)
    Q_PROPERTY(quint64 shotListRevision READ shotListRevision NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantMap flowSnapshot READ flowSnapshot NOTIFY flowSnapshotChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
public:
    ~FlowCalibrationService() override;
    bool ready() const;
    bool loading() const;
    double savedMultiplier() const;
    qint64 calibrationRevision() const;
    quint64 shotListRevision() const;
    QString errorMessage() const;
    QVariantMap flowSnapshot() const { return m_flowSnapshot; }
    bool busy() const { return !m_pending.isEmpty(); }

    void setNode(QRemoteObjectNode* node) override;
    static void setPublishedNode(QRemoteObjectNode* node);
    static FlowCalibrationService* create(QQmlEngine* engine);

    Q_INVOKABLE QString newRequestId() const;
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString loadRecentShots(const QString& requestId = QString());
    Q_INVOKABLE QString loadShot(int index, quint64 expectedShotListRevision,
                                 const QString& requestId = QString());
    Q_INVOKABLE QString save(double multiplier, qint64 expectedCalibrationRevision,
                             const QString& requestId = QString());
    // Local, pure preview using the same transform as FlowCalibrationModel.
    Q_INVOKABLE QVariantList replayFlowData(const QVariantList& original,
                                            double recordedMultiplier,
                                            double proposedMultiplier) const;

signals:
    void stateChanged();
    void flowSnapshotChanged();
    void busyChanged();
    void operationCompleted(const QString& requestId, const QString& operation,
                            bool success, const QVariantMap& result, const QString& reason);

private:
    friend struct FlowCalibrationServiceForeign;
    explicit FlowCalibrationService(QRemoteObjectNode* node, QObject* parent);
    static QRemoteObjectNode* publishedNode();
    bool connected() const;
    QString send(const QString& operation, const std::function<void(FlowCalibrationStateReplica*, const QString&)>& call,
                 const QString& requestId);
    void failPending(const QString& reason);

    QPointer<QRemoteObjectNode> m_node;
    QPointer<FlowCalibrationStateReplica> m_state;
    QMetaObject::Connection m_nodeDestroyed;
    ServiceRequests m_pending;
    QVariantMap m_flowSnapshot;
};
