#pragma once

#include "remoteservice.h"
#include "rep_machinecontrolservice_replica.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVariantMap>

class QRemoteObjectNode;

class MachineControlService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(qint64 selectedRecipeId READ selectedRecipeId NOTIFY stateChanged FINAL)
    Q_PROPERTY(qint64 espressoOperationId READ espressoOperationId NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged FINAL)
    Q_PROPERTY(int phase READ phase NOTIFY stateChanged FINAL)
    Q_PROPERTY(int currentFrameIndex READ currentFrameIndex NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString currentFrameName READ currentFrameName NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString pendingDrinkStep READ pendingDrinkStep NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool drinkSequenceActive READ drinkSequenceActive NOTIFY stateChanged FINAL)
public:
    ~MachineControlService() override;

    qint64 selectedRecipeId() const;
    qint64 espressoOperationId() const;
    bool ready() const;
    bool busy() const { return !m_pending.isEmpty(); }
    QString errorMessage() const { return m_errorMessage; }
    int phase() const;
    int currentFrameIndex() const;
    QString currentFrameName() const;
    QString pendingDrinkStep() const;
    bool drinkSequenceActive() const;

    void setNode(QRemoteObjectNode* node) override;
    static void setPublishedNode(QRemoteObjectNode* node);

    Q_INVOKABLE QString selectSteamPitcher(int index, const QVariantMap& expectedPreset);
    Q_INVOKABLE QString selectWaterVessel(int index, const QVariantMap& expectedPreset);
    Q_INVOKABLE QString selectFlushPreset(int index, const QVariantMap& expectedPreset);
    Q_INVOKABLE QString startRecipe(const QString& expectedContextId, qint64 recipeId);
    Q_INVOKABLE QString startEspresso(const QString& expectedContextId,
                                      const QString& profileFilename);
    Q_INVOKABLE QString startSteam(const QString& expectedContextId, int pitcherIndex,
                                   const QVariantMap& expectedPreset);
    Q_INVOKABLE QString startHotWater(const QString& expectedContextId, int waterVesselIndex,
                                      const QVariantMap& expectedPreset);
    Q_INVOKABLE QString startFlush(const QString& expectedContextId, int flushPresetIndex,
                                   const QVariantMap& expectedPreset);

    Q_INVOKABLE QString sleepMachine(const QString& expectedContextId);
    Q_INVOKABLE QString wakeMachine(const QString& expectedContextId);
    Q_INVOKABLE QString requestIdle(const QString& expectedContextId);
    Q_INVOKABLE QString disconnectMachine(const QString& expectedContextId);
    Q_INVOKABLE QString scanForDevices();
    Q_INVOKABLE QString toggleSteamHeater(const QString& expectedContextId);
    Q_INVOKABLE QString toggleCharging(const QString& expectedContextId);
    Q_INVOKABLE QString loadPreviousProfile(const QString& expectedContextId);
    Q_INVOKABLE QString generateFakeShot(const QString& expectedContextId);
    Q_INVOKABLE QString applyTemperatureOverride(const QString& expectedContextId,
                                                  const QVariantMap& expectedProfile,
                                                  const QVariantMap& expectedOverride,
                                                  double valueC);
    Q_INVOKABLE QString requestBrewSettings();
    Q_INVOKABLE QString applyBrewSettings(const QString& expectedRevision,
                                          const QVariantMap& values,
                                          const QString& expectedContextId);
    Q_INVOKABLE QString updateProfileTemperature(const QString& expectedRevision,
                                                 double valueC,
                                                 const QString& expectedContextId);

    Q_INVOKABLE QString setHotWaterFlow(const QString& expectedContextId, int flow);
    Q_INVOKABLE QString stopHotWater(const QString& expectedContextId);
    Q_INVOKABLE QString stopFlush(const QString& expectedContextId);
    Q_INVOKABLE QString stopEspresso(const QString& expectedContextId);
    Q_INVOKABLE QString bumpTargetWeight(const QString& expectedContextId, double deltaG);
    Q_INVOKABLE QString bumpFlushSeconds(const QString& expectedContextId, double deltaSec);
    Q_INVOKABLE QString bumpHotWaterTarget(const QString& expectedContextId, double deltaG);
    Q_INVOKABLE QString skipFrame(const QString& expectedContextId);
    Q_INVOKABLE QString skipDrinkStep(const QString& expectedContextId);
    Q_INVOKABLE QString startSteamPurge(const QString& expectedContextId);
    Q_INVOKABLE QString applyHotWaterSettings(const QString& expectedContextId);
    Q_INVOKABLE QString applyFlushSettings(const QString& expectedContextId);

signals:
    void stateChanged();
    void busyChanged();
    void completed(const QString& requestId, const QString& operation, bool success,
                   const QString& message, const QVariantMap& result);
    void presentationEvent(const QString& sourceId, qint64 sequence,
                           const QString& event, const QVariantMap& payload);
    void presentationEventsMissed(qint64 count);

private:
    friend struct MachineControlServiceForeign;
    explicit MachineControlService(QRemoteObjectNode* node, QObject* parent = nullptr);
    static QRemoteObjectNode* publishedNode();
    QString send(const QString& operation, const QVariantMap& fields = {},
                 const QString& expectedContextId = {});
    void onOperationCompleted(const QString& requestId, const QString& operation,
                              bool success, const QString& message,
                              const QVariantMap& result);
    void failPending(const QString& message);
    void updatePresentationReadiness();
    void receivePresentationEvent(const QString& sourceId, qint64 sequence,
                                  const QString& event, const QVariantMap& payload);

    QPointer<QRemoteObjectNode> m_node;
    QPointer<MachineControlStateReplica> m_state;
    QMetaObject::Connection m_nodeDestroyedConnection;
    QHash<QString, QString> m_pending;
    QString m_errorMessage;
    QString m_eventSourceId;
    qint64 m_lastEventSequence = 0;
    bool m_hasEventBaseline = false;
    bool m_wasReady = false;
};
