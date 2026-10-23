#include "comparisonservice.h"

#include "../history/shotcomparisonreadout.h"
#include "../history/shotcomparisontext.h"

#include <QRemoteObjectNode>
#include <QUuid>
#include <QtGlobal>

namespace { QPointer<QRemoteObjectNode> s_comparisonNode; }

ComparisonService::ComparisonService(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent)
{
    setNode(node);
}

ComparisonService::~ComparisonService()
{
    if (m_state) disconnect(m_state, nullptr, this, nullptr);
}

void ComparisonService::setPublishedNode(QRemoteObjectNode* node) { s_comparisonNode = node; }
QRemoteObjectNode* ComparisonService::publishedNode() { return s_comparisonNode; }

bool ComparisonService::connected() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, s_comparisonNode);
}

void ComparisonService::setNode(QRemoteObjectNode* node)
{
    if (m_node == node && m_state && (node || !m_nodeDestroyed)) return;
    disconnect(m_nodeDestroyed);
    m_nodeDestroyed = {};
    ++m_generation;
    m_activeRequestId.clear();
    const bool wasLoading = m_loading;
    m_loading = false;
    m_snapshot.clear();
    m_wasConnected = false;
    if (wasLoading) emit loadingChanged();
    if (m_state) {
        disconnect(m_state, nullptr, this, nullptr);
        m_state->deleteLater();
    }
    m_node = node;
    m_state = node ? node->acquire<ComparisonStateReplica>() : new ComparisonStateReplica;
    m_state->setParent(this);
    if (node) m_nodeDestroyed = connect(node, &QObject::destroyed, this, [this] { setNode(nullptr); });
    const auto synchronize = [this] {
        const bool nowConnected = connected();
        if (!nowConnected) {
            m_wasConnected = false;
            clearForDisconnect();
        } else if (!m_wasConnected) {
            m_wasConnected = true;
            m_snapshot.clear();
            setError({});
            if (!m_selectedShotIds.isEmpty()) requestSnapshot();
            else { emit shotsChanged(); emit windowChanged(); }
        }
    };
    connect(m_state, &QRemoteObjectReplica::stateChanged, this, synchronize);
    connect(m_state, &QRemoteObjectReplica::notified, this, synchronize);
    connect(m_state, &ComparisonStateReplica::snapshotReady, this,
        [this](const QString& requestId, const QVariantMap& snapshot, const QString& reason) {
            if (requestId.isEmpty() || requestId != m_activeRequestId) return;
            m_activeRequestId.clear();
            m_loading = false;
            emit loadingChanged();
            if (reason.isEmpty()) {
                m_snapshot = snapshot;
                m_selectedShotIds = snapshot.value(QStringLiteral("selectedShotIds")).toList();
                m_baseShotId = snapshot.value(QStringLiteral("baseShotId")).toLongLong();
                m_windowStart = snapshot.value(QStringLiteral("windowStart")).toInt();
                setError({});
                emit shotsChanged();
                emit windowChanged();
            } else {
                setError(reason);
            }
        }, Qt::QueuedConnection);
    synchronize();
}

void ComparisonService::clearForDisconnect()
{
    ++m_generation;
    m_activeRequestId.clear();
    m_snapshot.clear();
    const bool wasLoading = m_loading;
    m_loading = false;
    if (wasLoading) emit loadingChanged();
    if (!connected()) {
        setError(tr("Shot comparison is disconnected. Reconnect to load the selected shots."));
    }
    emit shotsChanged();
    emit windowChanged();
}

int ComparisonService::shotCount() const { return m_snapshot.value(QStringLiteral("shotCount")).toInt(); }
double ComparisonService::maxTime() const { return m_snapshot.value(QStringLiteral("maxTime")).toDouble(); }
QVariantMap ComparisonService::comparison() const { return m_snapshot.value(QStringLiteral("comparison")).toMap(); }
QVariantMap ComparisonService::texts() const
{
    static const QVariantMap result = ShotComparisonText::toJson().toVariantMap();
    return result;
}

void ComparisonService::setError(const QString& error)
{
    if (m_error == error) return;
    m_error = error;
    emit errorChanged();
}

void ComparisonService::setShots(const QVariantList& shotIds)
{
    restoreSelection(shotIds, 0, 0);
}

void ComparisonService::restoreSelection(const QVariantList& shotIds, qint64 baseShotId, int windowStart)
{
    if (shotIds.size() > MaximumSelection) {
        setError(tr("A comparison can include at most 500 shots."));
        return;
    }
    QVariantList normalized;
    normalized.reserve(shotIds.size());
    for (const QVariant& value : shotIds) {
        bool ok = false;
        const qint64 id = value.toLongLong(&ok);
        if (!ok || id <= 0 || normalized.contains(id)) continue;
        normalized.append(id);
    }
    m_selectedShotIds = normalized;
    m_baseShotId = normalized.contains(baseShotId) ? baseShotId : 0;
    m_windowStart = qBound(0, windowStart, qMax(0, totalShots() - 1 - otherWindowSize()));
    emit shotsChanged();
    emit windowChanged();
    requestSnapshot();
}

void ComparisonService::addShots(const QVariantList& shotIds)
{
    QVariantList merged = m_selectedShotIds;
    for (const QVariant& value : shotIds) {
        bool ok = false;
        const qint64 id = value.toLongLong(&ok);
        if (ok && id > 0 && !merged.contains(id) && merged.size() < MaximumSelection) merged.append(id);
    }
    setShots(merged);
}

void ComparisonService::clearAll()
{
    ++m_generation;
    m_activeRequestId.clear();
    const bool wasLoading = m_loading;
    m_loading = false;
    m_snapshot.clear();
    m_selectedShotIds.clear();
    m_baseShotId = 0;
    m_windowStart = 0;
    setError({});
    if (wasLoading) emit loadingChanged();
    emit shotsChanged();
    emit windowChanged();
}

void ComparisonService::setBaseShot(qint64 shotId)
{
    if (shotId == m_baseShotId || !m_selectedShotIds.contains(shotId)) return;
    m_baseShotId = shotId;
    m_windowStart = 0;
    emit shotsChanged();
    emit windowChanged();
    requestSnapshot();
}

void ComparisonService::shiftWindowLeft()
{
    if (!canShiftLeft()) return;
    --m_windowStart;
    emit shotsChanged();
    emit windowChanged();
    requestSnapshot();
}

void ComparisonService::shiftWindowRight()
{
    if (!canShiftRight()) return;
    ++m_windowStart;
    emit shotsChanged();
    emit windowChanged();
    requestSnapshot();
}

void ComparisonService::requestSnapshot()
{
    m_snapshot.clear();
    setError({});
    emit shotsChanged();
    if (!connected()) {
        ++m_generation;
        m_activeRequestId.clear();
        const bool wasLoading = m_loading;
        m_loading = false;
        if (wasLoading) emit loadingChanged();
        setError(tr("Shot comparison is disconnected. Reconnect to load the selected shots."));
        return;
    }
    const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    ++m_generation;
    m_activeRequestId = requestId;
    const quint64 generation = m_generation;
    QPointer<ComparisonService> guard(this);
    if (!m_loading) {
        m_loading = true;
        emit loadingChanged();
    }
    QMetaObject::invokeMethod(this, [guard, generation, requestId] {
        if (!guard || guard->m_generation != generation || !guard->connected()) return;
        guard->m_state->query(requestId, guard->m_selectedShotIds,
                              guard->m_baseShotId, guard->m_windowStart);
    }, Qt::QueuedConnection);
}

QVariantList ComparisonService::seriesAt(const QString& name, int index) const
{
    const QVariantList all = m_snapshot.value(name).toList();
    if (index < 0 || index >= all.size()) return {};
    return all.at(index).toList();
}

QVariantList ComparisonService::pointSeriesAt(int index) const
{
    static const QStringList names{
        QStringLiteral("pressure"), QStringLiteral("flow"), QStringLiteral("temperature"),
        QStringLiteral("weight"), QStringLiteral("weightFlow"), QStringLiteral("resistance"),
        QStringLiteral("conductance"), QStringLiteral("conductanceDerivative"),
        QStringLiteral("darcyResistance"), QStringLiteral("temperatureMix"),
        QStringLiteral("temperatureMixGoal")
    };
    QVariantList result;
    result.reserve(names.size());
    for (const QString& name : names) {
        const QVariantList all = m_snapshot.value(name).toList();
        result.append(index >= 0 && index < all.size() ? all.at(index).toList() : QVariantList{});
    }
    return result;
}

#define COMPARISON_SERIES_GETTER(method, wireName) \
QVariantList ComparisonService::method(int index) const { return seriesAt(QStringLiteral(wireName), index); }

COMPARISON_SERIES_GETTER(getPressureData, "pressure")
COMPARISON_SERIES_GETTER(getFlowData, "flow")
COMPARISON_SERIES_GETTER(getTemperatureData, "temperature")
COMPARISON_SERIES_GETTER(getWeightData, "weight")
COMPARISON_SERIES_GETTER(getWeightFlowRateData, "weightFlow")
COMPARISON_SERIES_GETTER(getResistanceData, "resistance")
COMPARISON_SERIES_GETTER(getConductanceData, "conductance")
COMPARISON_SERIES_GETTER(getConductanceDerivativeData, "conductanceDerivative")
COMPARISON_SERIES_GETTER(getDarcyResistanceData, "darcyResistance")
COMPARISON_SERIES_GETTER(getTemperatureMixData, "temperatureMix")
COMPARISON_SERIES_GETTER(getTemperatureMixGoalData, "temperatureMixGoal")
#undef COMPARISON_SERIES_GETTER

QVariantList ComparisonService::getPhaseMarkers(int index) const
{
    const QVariantList markers = m_snapshot.value(QStringLiteral("phaseMarkers")).toList();
    return index >= 0 && index < markers.size() ? markers.at(index).toList() : QVariantList{};
}

QVariantMap ComparisonService::getShotInfo(int index) const
{
    const QVariantList infos = m_snapshot.value(QStringLiteral("shotInfo")).toList();
    return index >= 0 && index < infos.size() ? infos.at(index).toMap() : QVariantMap{};
}

QVariantMap ComparisonService::getValuesAtTime(int index, double time) const
{
    return ShotComparisonReadout::valuesAtTime(pointSeriesAt(index), time);
}
