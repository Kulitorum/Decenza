#pragma once
#include "remoteservice.h"

#include "rep_steamservice_replica.h"
#include "models/steamdatamodel.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

#include <QtQmlIntegration/qqmlintegration.h>

class QJSEngine;
class QQmlEngine;
class QRemoteObjectNode;
class QRemoteObjectPendingCallWatcher;

class SteamService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(SteamStateReplica* state READ state NOTIFY stateChanged FINAL)
    Q_PROPERTY(SteamDataModel* graph READ graph CONSTANT FINAL)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged FINAL)
    Q_PROPERTY(QString editorRevision READ editorRevision NOTIFY stateChanged FINAL)

public:
    ~SteamService() override;

    SteamStateReplica* state() const;
    SteamDataModel* graph() const { return m_graph; }
    bool ready() const;
    bool busy() const { return m_busy; }
    QString errorMessage() const { return m_errorMessage; }
    QString editorRevision() const;

    Q_INVOKABLE QString request(const QString& operation, const QVariantMap& fields = {},
                                const QString& expectedRevision = {});
    Q_INVOKABLE QVariantMap getSteamPitcherPreset(int index) const;
    Q_INVOKABLE bool steamPitcherNameTaken(const QString& name, int ignoreIndex = -1) const;
    Q_INVOKABLE int effectiveSteamDurationSec(int index, double milkG) const;
    void setNode(QRemoteObjectNode* node) override;

    // Called by the native host or browser connector before the singleton is
    // first requested by QML. The same generated replica path is used in both.
    static void setPublishedNode(QRemoteObjectNode* node);

signals:
    void stateChanged();
    void readyChanged();
    void busyChanged();
    void errorMessageChanged();
    void completed(const QString& requestId, const QString& operation, bool success,
                   const QString& message, const QVariantMap& result,
                   const QString& newRevision);
    void warningSnapshotChanged();
    void pressureTooHigh();
    void temperatureTooHigh();
    void descaleWarning();
    void temperatureWarning(const QString& message);
    void scaleBuildupWarning(const QString& message);
    void warningEventsMissed(int count);

private:
    friend struct SteamServiceForeign;
    explicit SteamService(QRemoteObjectNode* node, QObject* parent = nullptr);
    static QRemoteObjectNode* publishedNode();
    void onReplicaStateChanged();
    void applyGraphSnapshot();
    void consumeGraphUpdate(const QVariantMap& update);
    bool applyGraphUpdate(const QVariantMap& update, bool append);
    void requestGraphSnapshot();
    void resetGraphSync();
    void rejectGraphUpdate();
    void reconcileWarningSnapshot();
    void failPending(const QString& message);
    void finishRequest(QRemoteObjectPendingCallWatcher* watcher, const QString& requestId,
                       const QString& operation);
    void setErrorMessage(const QString& message);
    void setBusy(bool busy);

    QPointer<SteamStateReplica> m_state;
    SteamDataModel* m_graph = nullptr;
    QPointer<QRemoteObjectNode> m_node;
    QMetaObject::Connection m_nodeDestroyedConnection;
    bool m_busy = false;
    QString m_errorMessage;
    QString m_pendingRequestId;
    QString m_pendingOperation;
    quint64 m_generation = 0;
    quint64 m_requestGeneration = 0;
    bool m_graphInitialized = false;
    QString m_graphHostId;
    QString m_graphRequestId;
    QList<QVariantMap> m_graphUpdates;
    qsizetype m_bufferedGraphPoints = 0;
    QString m_warningHostId;
    qint64 m_warningCursor = 0;
    bool m_warningInitialized = false;
};
