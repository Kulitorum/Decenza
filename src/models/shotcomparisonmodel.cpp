#include "shotcomparisonmodel.h"
#include "../history/shothistorystorage.h"
#include "../history/shotcomparison.h"
#include "../history/shotcomparisontext.h"
#include "../history/shotprojection.h"
#include "../history/shotcomparisonreadout.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlQuery>
#include "../core/dbutils.h"
#include <QThread>
#include <algorithm>
#include <utility>

ShotComparisonModel::ShotComparisonModel(QObject* parent)
    : QObject(parent)
{
}

ShotComparisonModel::~ShotComparisonModel()
{
    // A request-scoped comparison model can be released as soon as its owner
    // disconnects. Join any worker before destroying the QObject captured by
    // the queued completion functor.
    const auto threads = m_loadThreads;
    m_loadThreads.clear();
    for (const QPointer<QThread>& guarded : threads) {
        if (!guarded) continue;
        QThread* thread = guarded.data();
        if (thread->isRunning()) thread->wait();
        delete thread;
    }
}

QVariantMap ShotComparisonModel::texts() const
{
    static const QVariantMap t = ShotComparisonText::toJson().toVariantMap();
    return t;
}

void ShotComparisonModel::setStorage(ShotHistoryStorage* storage)
{
    m_storage = storage;
}

void ShotComparisonModel::addShots(const QVariantList& shotIds)
{
    if (!m_storage) {
        emit errorOccurred("Storage not available");
        return;
    }

    bool changed = false;
    for (const QVariant& v : shotIds) {
        qint64 id = v.toLongLong();
        if (!m_shotIds.contains(id)) {
            m_shotIds.append(id);
            changed = true;
        }
    }
    if (!changed) return;

    m_baseShotId = 0;   // the loader picks the oldest
    m_windowStart = 0;
    scheduleLoad();
    emit shotsChanged();
    emit windowChanged();
}

void ShotComparisonModel::setSelection(const QVariantList& shotIds, qint64 baseShotId, int windowStart)
{
    QList<qint64> selected;
    selected.reserve(shotIds.size());
    for (const QVariant& value : shotIds) {
        bool ok = false;
        const qint64 id = value.toLongLong(&ok);
        if (!ok || id <= 0 || selected.contains(id)) continue;
        selected.append(id);
    }
    m_shotIds = std::move(selected);
    m_baseShotId = m_shotIds.contains(baseShotId) ? baseShotId : 0;
    m_windowStart = std::max(0, windowStart);
    scheduleLoad();
    emit shotsChanged();
    emit windowChanged();
}

QVariantList ShotComparisonModel::selectedShotIds() const
{
    QVariantList result;
    result.reserve(m_shotIds.size());
    for (qint64 id : m_shotIds) result.append(id);
    return result;
}

void ShotComparisonModel::clearAll()
{
    ++m_loadSerial;  // Invalidate any in-flight background loads
    m_shotIds.clear();
    m_baseShotId = 0;
    m_displayShots.clear();
    m_comparison.clear();
    m_windowStart = 0;
    m_maxTime = 60.0;
    if (m_loading) {
        m_loading = false;
        emit loadingChanged();
    }
    emit shotsChanged();
    emit windowChanged();
}

void ShotComparisonModel::setBaseShot(qint64 shotId)
{
    if (shotId == m_baseShotId || !m_shotIds.contains(shotId)) return;
    m_baseShotId = shotId;
    // The others restart from the earliest, so the previous base reappears in its
    // place by date rather than behind a page the user has moved past.
    m_windowStart = 0;
    scheduleLoad();
    emit shotsChanged();
    emit windowChanged();
}

void ShotComparisonModel::shiftWindowLeft()
{
    if (canShiftLeft()) {
        m_windowStart--;
        scheduleLoad();
        emit shotsChanged();
        emit windowChanged();
    }
}

void ShotComparisonModel::shiftWindowRight()
{
    if (canShiftRight()) {
        m_windowStart++;
        scheduleLoad();
        emit shotsChanged();
        emit windowChanged();
    }
}

void ShotComparisonModel::scheduleLoad()
{
    // Increment serial so any in-flight load knows its result is stale
    ++m_loadSerial;
    int serial = m_loadSerial;

    if (m_shotIds.isEmpty() || !m_storage) {
        m_displayShots.clear();
        m_comparison.clear();
        calculateMaxValues();
        if (m_loading) {
            m_loading = false;
            emit loadingChanged();
        }
        return;
    }

    const QList<qint64> ids = m_shotIds;
    const qint64 requestedBase = m_baseShotId;
    const int requestedStart = m_windowStart;
    const QString dbPath = m_storage->databasePath();

    if (!m_loading) {
        m_loading = true;
        emit loadingChanged();
    }

    // Open a dedicated SQLite connection on the worker thread, order the selection, load
    // the base and the window, and deliver results back to the main thread via a queued
    // invocation. Qt guarantees the functor is not called if `this` is already destroyed.
    QThread* thread = QThread::create([this, dbPath, ids, requestedBase, requestedStart, serial]() {
        QList<qint64> ordered;
        qint64 base = 0;
        int windowStart = 0;
        QList<ComparisonShot> shots;
        QList<ShotProjection> projections;
        withTempDb(dbPath, "scm_load", [&](QSqlDatabase& db) {
            // Oldest first by when each shot was pulled, not by id: an imported shot gets
            // a new id but keeps its old timestamp. A deleted shot drops out here, so a
            // base that no longer exists falls back to the oldest one that does.
            QStringList marks;
            for (qsizetype i = 0; i < ids.size(); ++i) marks << QStringLiteral("?");
            QSqlQuery q(db);
            q.prepare(QStringLiteral("SELECT id FROM shots WHERE id IN (%1) ORDER BY timestamp ASC, id ASC")
                      .arg(marks.join(QLatin1Char(','))));
            for (qint64 id : ids) q.addBindValue(id);
            if (q.exec())
                while (q.next()) ordered << q.value(0).toLongLong();
            if (ordered.isEmpty()) return;

            base = ordered.contains(requestedBase) ? requestedBase : ordered.first();
            QList<qint64> others = ordered;
            others.removeAll(base);
            windowStart = std::clamp(requestedStart, 0, std::max(0, int(others.size()) - OTHER_WINDOW_SIZE));
            QList<qint64> windowIds{ base };
            for (qsizetype i = windowStart; i < std::min<qsizetype>(windowStart + OTHER_WINDOW_SIZE, others.size()); ++i)
                windowIds.append(others[i]);

            for (qint64 id : windowIds) {
                ShotRecord record = ShotHistoryStorage::loadShotRecordStatic(db, id, nullptr, Q_FUNC_INFO);
                if (record.summary.id == 0) continue;
                projections.append(ShotHistoryStorage::convertShotRecord(record));

                ComparisonShot shot;
                shot.id = record.summary.id;
                shot.profileName = record.summary.profileName;
                shot.timestamp = record.summary.timestamp;
                shot.duration = record.summary.duration;
                shot.pourStartSec = record.cachedAnalysis ? record.cachedAnalysis->detectors.pourStartSec : 0.0;
                shot.pressure = record.pressure;
                shot.flow = record.flow;
                shot.temperature = record.temperature;
                shot.weight = record.weight;
                shot.weightFlowRate = record.weightFlowRate;
                shot.resistance = record.resistance;
                shot.conductance = record.conductance;
                shot.conductanceDerivative = record.conductanceDerivative;
                shot.darcyResistance = record.darcyResistance;
                shot.temperatureMix = record.temperatureMix;
                shot.temperatureMixGoal = record.temperatureMixGoal;

                for (const auto& phase : record.phases) {
                    ComparisonShot::PhaseMarker marker;
                    marker.time = phase.time;
                    marker.label = phase.label;
                    marker.transitionReason = phase.transitionReason;
                    shot.phases.append(marker);
                }

                shots.append(shot);
            }
        });

        // Column 0 is the base only if it loaded; otherwise there is nothing to
        // compare against, and no column may pose as the base.
        const bool haveBase = !shots.isEmpty() && shots.first().id == base;
        if (!haveBase) shots.clear();
        const QVariantMap comparison = haveBase
            ? ShotComparison::compare(projections, 0).toVariantMap() : QVariantMap();

        QMetaObject::invokeMethod(this, [this, ordered, base, windowStart, ownedShots = std::move(shots),
                                         comparison, serial]() mutable {
            if (serial != m_loadSerial) return;  // superseded by a newer load
            m_shotIds = ordered;
            m_baseShotId = base;
            m_windowStart = windowStart;
            m_displayShots = std::move(ownedShots);
            m_comparison = comparison;
            calculateMaxValues();
            m_loading = false;
            emit loadingChanged();
            emit shotsChanged();
            emit windowChanged();
        }, Qt::QueuedConnection);
    });

    m_loadThreads.append(thread);
    connect(thread, &QThread::finished, this, [this, thread] {
        for (qsizetype i = m_loadThreads.size() - 1; i >= 0; --i) {
            if (m_loadThreads.at(i).data() == thread) m_loadThreads.removeAt(i);
        }
        thread->deleteLater();
    }, Qt::QueuedConnection);
    thread->start();
}

void ShotComparisonModel::calculateMaxValues()
{
    m_maxTime = 0.0;
    for (const auto& shot : m_displayShots)
        m_maxTime = std::max(m_maxTime, shot.duration);
}

// QPointF, not {x, y} maps: ComparisonGraph binds these straight to LineSeries.values,
// which reads points and would treat maps as bare numbers (qxyseries.cpp:735-738).
QVariantList ShotComparisonModel::pointsToVariant(const QVector<QPointF>& points) const
{
    QVariantList result;
    result.reserve(points.size());
    for (const auto& pt : points)
        result.append(QVariant::fromValue(pt));
    return result;
}

QVariantList ShotComparisonModel::getPressureData(int index) const
{
    if (index < 0 || index >= m_displayShots.size()) return QVariantList();
    return pointsToVariant(m_displayShots[index].pressure);
}

QVariantList ShotComparisonModel::getFlowData(int index) const
{
    if (index < 0 || index >= m_displayShots.size()) return QVariantList();
    return pointsToVariant(m_displayShots[index].flow);
}

QVariantList ShotComparisonModel::getTemperatureData(int index) const
{
    if (index < 0 || index >= m_displayShots.size()) return QVariantList();
    return pointsToVariant(m_displayShots[index].temperature);
}

QVariantList ShotComparisonModel::getWeightData(int index) const
{
    if (index < 0 || index >= m_displayShots.size()) return QVariantList();
    return pointsToVariant(m_displayShots[index].weight);
}

QVariantList ShotComparisonModel::getWeightFlowRateData(int index) const
{
    if (index < 0 || index >= m_displayShots.size()) return QVariantList();
    return pointsToVariant(m_displayShots[index].weightFlowRate);
}

QVariantList ShotComparisonModel::getResistanceData(int index) const
{
    if (index < 0 || index >= m_displayShots.size()) return QVariantList();
    return pointsToVariant(m_displayShots[index].resistance);
}

QVariantList ShotComparisonModel::getConductanceData(int index) const
{
    if (index < 0 || index >= m_displayShots.size()) return QVariantList();
    return pointsToVariant(m_displayShots[index].conductance);
}

QVariantList ShotComparisonModel::getConductanceDerivativeData(int index) const
{
    if (index < 0 || index >= m_displayShots.size()) return QVariantList();
    return pointsToVariant(m_displayShots[index].conductanceDerivative);
}

QVariantList ShotComparisonModel::getDarcyResistanceData(int index) const
{
    if (index < 0 || index >= m_displayShots.size()) return QVariantList();
    return pointsToVariant(m_displayShots[index].darcyResistance);
}

QVariantList ShotComparisonModel::getTemperatureMixData(int index) const
{
    if (index < 0 || index >= m_displayShots.size()) return QVariantList();
    return pointsToVariant(m_displayShots[index].temperatureMix);
}

QVariantList ShotComparisonModel::getTemperatureMixGoalData(int index) const
{
    if (index < 0 || index >= m_displayShots.size()) return QVariantList();
    return pointsToVariant(m_displayShots[index].temperatureMixGoal);
}

QVariantList ShotComparisonModel::getPhaseMarkers(int index) const
{
    if (index < 0 || index >= m_displayShots.size()) return QVariantList();

    QVariantList result;
    for (const auto& phase : m_displayShots[index].phases) {
        QVariantMap p;
        p["time"] = phase.time;
        p["label"] = phase.label;
        p["transitionReason"] = phase.transitionReason;
        result.append(p);
    }
    return result;
}

QVariantMap ShotComparisonModel::getShotInfo(int index) const
{
    QVariantMap result;
    if (index < 0 || index >= m_displayShots.size()) return result;

    const auto& shot = m_displayShots[index];
    result["id"] = shot.id;
    result["isBase"] = shot.id == m_baseShotId;
    result["profileName"] = shot.profileName;
    result["pourStartSec"] = shot.pourStartSec;

    result["dateTime"] = ShotHistoryStorage::shortDateTime(shot.timestamp);
    return result;
}

QVariantMap ShotComparisonModel::getValuesAtTime(int index, double time) const
{
    if (index < 0 || index >= m_displayShots.size()) return {};
    const auto& shot = m_displayShots[index];
    return ShotComparisonReadout::valuesAtTime({
        pointsToVariant(shot.pressure), pointsToVariant(shot.flow),
        pointsToVariant(shot.temperature), pointsToVariant(shot.weight),
        pointsToVariant(shot.weightFlowRate), pointsToVariant(shot.resistance),
        pointsToVariant(shot.conductance), pointsToVariant(shot.conductanceDerivative),
        pointsToVariant(shot.darcyResistance), pointsToVariant(shot.temperatureMix),
        pointsToVariant(shot.temperatureMixGoal)
    }, time);
}
