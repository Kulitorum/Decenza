#pragma once

#include "remoteservice.h"
#include "rep_sensorcalibrationservice_replica.h"

#include <QHash>
#include <QPointer>
#include <QVariantMap>
#include <QVariantList>
#include <functional>

class QRemoteObjectNode;
class QQmlEngine;

class SensorCalibrationService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantList sensors READ sensors NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
public:
    ~SensorCalibrationService() override;
    bool ready() const;
    QVariantMap snapshot() const;
    QVariantList sensors() const;
    QString errorMessage() const;
    bool connected() const;
    bool busy() const { return !m_pending.isEmpty(); }
    QVariantMap sensorState(int sensor) const;
    int sensorCount() const;

    void setNode(QRemoteObjectNode* node) override;
    static void setPublishedNode(QRemoteObjectNode* node);
    static SensorCalibrationService* create(QQmlEngine* engine);

    Q_INVOKABLE QString newRequestId() const;
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString readCalibration(int sensor, const QString& requestId = QString());
    Q_INVOKABLE QString validateCorrection(int sensor, double instrumentReading,
                                           qint64 expectedContextRevision,
                                           const QString& requestId = QString());
    Q_INVOKABLE QString applyCorrection(int sensor, double instrumentReading,
                                        qint64 expectedContextRevision,
                                        qint64 expectedCalibrationRevision,
                                        const QString& requestId = QString());

signals:
    void stateChanged();
    void busyChanged();
    void operationCompleted(const QString& requestId, const QString& operation,
                            bool success, const QVariantMap& result, const QString& reason);

private:
    friend struct SensorCalibrationServiceForeign;
    explicit SensorCalibrationService(QRemoteObjectNode* node, QObject* parent);
    static QRemoteObjectNode* publishedNode();
    bool replicaConnected() const;
    QString send(const QString& operation,
                 const std::function<void(SensorCalibrationStateReplica*, const QString&)>& call,
                 const QString& requestId);
    void failPending(const QString& reason);

    QPointer<QRemoteObjectNode> m_node;
    QPointer<SensorCalibrationStateReplica> m_state;
    QMetaObject::Connection m_nodeDestroyed;
    QHash<QString, QString> m_pending;
};
