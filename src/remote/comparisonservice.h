#pragma once

#include "remoteservice.h"
#include "rep_comparisonservice_replica.h"

#include <QPointer>
#include <QVariantList>
#include <QVariantMap>

class QRemoteObjectNode;

// ShotComparisonModel-shaped facade backed by a request-scoped canonical model
// on the paired host. Selection and paging remain local UI state; graph data is
// published only after the matching host query completes.
class ComparisonService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(QVariantList selectedShotIds READ selectedShotIds NOTIFY shotsChanged FINAL)
    Q_PROPERTY(int shotCount READ shotCount NOTIFY shotsChanged FINAL)
    Q_PROPERTY(double maxTime READ maxTime NOTIFY shotsChanged FINAL)
    Q_PROPERTY(qint64 baseShotId READ baseShotId NOTIFY shotsChanged FINAL)
    Q_PROPERTY(QVariantMap comparison READ comparison NOTIFY shotsChanged FINAL)
    Q_PROPERTY(QVariantMap texts READ texts CONSTANT FINAL)
    Q_PROPERTY(int windowStart READ windowStart NOTIFY windowChanged FINAL)
    Q_PROPERTY(int totalShots READ totalShots NOTIFY shotsChanged FINAL)
    Q_PROPERTY(int otherWindowSize READ otherWindowSize CONSTANT FINAL)
    Q_PROPERTY(bool canShiftLeft READ canShiftLeft NOTIFY windowChanged FINAL)
    Q_PROPERTY(bool canShiftRight READ canShiftRight NOTIFY windowChanged FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged FINAL)
public:
    ~ComparisonService() override;
    void setNode(QRemoteObjectNode* node) override;
    static void setPublishedNode(QRemoteObjectNode* node);

    QVariantList selectedShotIds() const { return m_selectedShotIds; }
    int shotCount() const;
    double maxTime() const;
    qint64 baseShotId() const { return m_baseShotId; }
    QVariantMap comparison() const;
    QVariantMap texts() const;
    int windowStart() const { return m_windowStart; }
    int totalShots() const { return static_cast<int>(m_selectedShotIds.size()); }
    int otherWindowSize() const { return 2; }
    bool canShiftLeft() const { return m_windowStart > 0; }
    bool canShiftRight() const { return m_windowStart + otherWindowSize() < qMax(0, totalShots() - 1); }
    bool loading() const { return m_loading; }
    QString errorMessage() const { return m_error; }

    Q_INVOKABLE void setShots(const QVariantList& shotIds);
    Q_INVOKABLE void restoreSelection(const QVariantList& shotIds, qint64 baseShotId, int windowStart);
    Q_INVOKABLE void addShots(const QVariantList& shotIds);
    Q_INVOKABLE void clearAll();
    Q_INVOKABLE void setBaseShot(qint64 shotId);
    Q_INVOKABLE void shiftWindowLeft();
    Q_INVOKABLE void shiftWindowRight();
    Q_INVOKABLE QVariantList getPressureData(int index) const;
    Q_INVOKABLE QVariantList getFlowData(int index) const;
    Q_INVOKABLE QVariantList getTemperatureData(int index) const;
    Q_INVOKABLE QVariantList getWeightData(int index) const;
    Q_INVOKABLE QVariantList getWeightFlowRateData(int index) const;
    Q_INVOKABLE QVariantList getResistanceData(int index) const;
    Q_INVOKABLE QVariantList getConductanceData(int index) const;
    Q_INVOKABLE QVariantList getConductanceDerivativeData(int index) const;
    Q_INVOKABLE QVariantList getDarcyResistanceData(int index) const;
    Q_INVOKABLE QVariantList getTemperatureMixData(int index) const;
    Q_INVOKABLE QVariantList getTemperatureMixGoalData(int index) const;
    Q_INVOKABLE QVariantList getPhaseMarkers(int index) const;
    Q_INVOKABLE QVariantMap getShotInfo(int index) const;
    Q_INVOKABLE QVariantMap getValuesAtTime(int index, double time) const;

signals:
    void shotsChanged();
    void windowChanged();
    void loadingChanged();
    void errorChanged();

private:
    friend struct ComparisonServiceForeign;
    explicit ComparisonService(QRemoteObjectNode* node, QObject* parent);
    static QRemoteObjectNode* publishedNode();
    bool connected() const;
    void requestSnapshot();
    QVariantList seriesAt(const QString& name, int index) const;
    QVariantList pointSeriesAt(int index) const;
    void setError(const QString& error);
    void clearForDisconnect();

    QPointer<QRemoteObjectNode> m_node;
    QPointer<ComparisonStateReplica> m_state;
    QMetaObject::Connection m_nodeDestroyed;
    QVariantMap m_snapshot;
    QVariantList m_selectedShotIds;
    qint64 m_baseShotId = 0;
    int m_windowStart = 0;
    bool m_loading = false;
    bool m_wasConnected = false;
    QString m_error;
    QString m_activeRequestId;
    quint64 m_generation = 0;
    static constexpr int MaximumSelection = 500;
};
