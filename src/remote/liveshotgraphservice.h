#pragma once

#include "remoteservice.h"
#include "rep_liveshotgraphservice_replica.h"

#include <array>
#include <QMetaObject>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QtQmlIntegration/qqmlintegration.h>

class FastLineRenderer;
class QJSEngine;
class QQmlEngine;
class QRemoteObjectNode;

Q_MOC_INCLUDE("rendering/fastlinerenderer.h")

class LiveShotGraphService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY graphChanged FINAL)
    Q_PROPERTY(bool stale READ stale NOTIFY graphChanged FINAL)
    Q_PROPERTY(QString packageId READ packageId NOTIFY graphChanged FINAL)
    Q_PROPERTY(QString hostId READ hostId NOTIFY graphChanged FINAL)
    Q_PROPERTY(qint64 epoch READ epoch NOTIFY graphChanged FINAL)
    Q_PROPERTY(qint64 sequence READ sequence NOTIFY graphChanged FINAL)
    Q_PROPERTY(double rawTime READ rawTime NOTIFY graphChanged FINAL)
    Q_PROPERTY(double maxTime READ maxTime NOTIFY graphChanged FINAL)
    Q_PROPERTY(double stopTime READ stopTime NOTIFY graphChanged FINAL)
    Q_PROPERTY(double weightAtStop READ weightAtStop NOTIFY graphChanged FINAL)
    Q_PROPERTY(double finalWeight READ finalWeight NOTIFY graphChanged FINAL)
    Q_PROPERTY(QVariantList phaseMarkers READ phaseMarkers NOTIFY phaseMarkersChanged FINAL)
    Q_PROPERTY(QVariantList pressureGoalPoints READ pressureGoalPoints NOTIFY pressureGoalPointsChanged FINAL)
    Q_PROPERTY(QVariantList flowGoalPoints READ flowGoalPoints NOTIFY flowGoalPointsChanged FINAL)
    Q_PROPERTY(QVariantList temperatureGoalPoints READ temperatureGoalPoints NOTIFY temperatureGoalPointsChanged FINAL)
    Q_PROPERTY(QVariantList temperatureMixGoalPoints READ temperatureMixGoalPoints NOTIFY temperatureMixGoalPointsChanged FINAL)
    Q_PROPERTY(double profileTargetWeight READ profileTargetWeight NOTIFY graphChanged FINAL)
    Q_PROPERTY(double machineTargetWeight READ machineTargetWeight NOTIFY graphChanged FINAL)
    Q_PROPERTY(QVariantList portalSamples READ portalSamples NOTIFY graphChanged FINAL)
    Q_PROPERTY(int portalSampleCount READ portalSampleCount NOTIFY graphChanged FINAL)
    Q_PROPERTY(double portalEcMin READ portalEcMin NOTIFY graphChanged FINAL)
    Q_PROPERTY(double portalEcMax READ portalEcMax NOTIFY graphChanged FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY graphChanged FINAL)

public:
    ~LiveShotGraphService() override;

    bool ready() const { return m_ready; }
    bool stale() const { return !m_ready && !m_graph.isEmpty(); }
    QString packageId() const;
    QString hostId() const;
    qint64 epoch() const { return m_epoch; }
    qint64 sequence() const { return m_sequence; }
    double rawTime() const;
    double maxTime() const;
    double stopTime() const;
    double weightAtStop() const;
    double finalWeight() const;
    QVariantList phaseMarkers() const;
    QVariantList pressureGoalPoints() const;
    QVariantList flowGoalPoints() const;
    QVariantList temperatureGoalPoints() const;
    QVariantList temperatureMixGoalPoints() const;
    double profileTargetWeight() const;
    double machineTargetWeight() const;
    QVariantList portalSamples() const;
    int portalSampleCount() const;
    double portalEcMin() const;
    double portalEcMax() const;
    QString errorMessage() const { return m_errorMessage; }

    void setNode(QRemoteObjectNode* node) override;
    static void setPublishedNode(QRemoteObjectNode* node);

    Q_INVOKABLE void requestSnapshot();
    Q_INVOKABLE void registerFastSeries(FastLineRenderer* pressure, FastLineRenderer* flow,
                                        FastLineRenderer* temperature, FastLineRenderer* weight,
                                        FastLineRenderer* weightFlow, FastLineRenderer* resistance,
                                        FastLineRenderer* conductance, FastLineRenderer* darcyResistance,
                                        FastLineRenderer* temperatureMix);

signals:
    void graphChanged();
    void phaseMarkersChanged();
    void pressureGoalPointsChanged();
    void flowGoalPointsChanged();
    void temperatureGoalPointsChanged();
    void temperatureMixGoalPointsChanged();
    void portalSampleAdded(double time, double ecRaw, double temperatureC, bool breakBefore);
    void cleared();

private:
    friend struct LiveShotGraphServiceForeign;
    friend class tst_LiveShotGraphService;
    explicit LiveShotGraphService(QRemoteObjectNode* node, QObject* parent);
    static QRemoteObjectNode* publishedNode();
    QString newRequestId() const;
    bool connected() const;
    void changed();
    void clearGraph(const QString& error = {});
    using Collections = std::array<QVariantList, 5>;
    Collections collectionState() const;
    void publishGraphChanges(const Collections& before);
    void requestSnapshotFor(const QString& requestId);
    void receiveSnapshot(const QString& requestId, qint64 epoch, qint64 sequence,
                         const QVariantMap& snapshot, const QString& reason);
    void receiveDelta(const QVariantMap& delta);
    bool parseSnapshot(const QVariantMap& snapshot, QHash<QString, QVector<QPointF>>* series) const;
    bool applyAppend(const QVariantMap& delta);
    void applyRenderers();
    void replacePortalSamples(const QVariantList& samples);
    void setRendererPoints(FastLineRenderer* renderer, const QVector<QPointF>& points);

    QPointer<QRemoteObjectNode> m_node;
    QPointer<LiveShotGraphStateReplica> m_state;
    QMetaObject::Connection m_nodeDestroyed;
    QString m_pendingSnapshotId;
    quint64 m_snapshotGeneration = 0;
    QString m_packageId;
    QString m_hostId;
    QString m_errorMessage;
    QVariantMap m_graph;
    QHash<QString, QVector<QPointF>> m_series;
    QVariantList m_portalSamples;
    QList<QVariantMap> m_bufferedDeltas;
    qsizetype m_bufferedBytes = 0;
    qint64 m_epoch = -1;
    qint64 m_sequence = -1;
    bool m_ready = false;
    bool m_snapshotRequested = false;
    bool m_bufferOverflowed = false;
    QPointer<FastLineRenderer> m_pressure;
    QPointer<FastLineRenderer> m_flow;
    QPointer<FastLineRenderer> m_temperature;
    QPointer<FastLineRenderer> m_weight;
    QPointer<FastLineRenderer> m_weightFlow;
    QPointer<FastLineRenderer> m_resistance;
    QPointer<FastLineRenderer> m_conductance;
    QPointer<FastLineRenderer> m_darcyResistance;
    QPointer<FastLineRenderer> m_temperatureMix;
};
