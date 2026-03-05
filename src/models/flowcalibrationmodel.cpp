#include "flowcalibrationmodel.h"
#include "../history/shothistorystorage.h"
#include "../core/settings.h"
#include "../ble/de1device.h"

#include <QDateTime>
#include <QDebug>
#include <QThread>
#include <QSqlDatabase>
#include <QSqlQuery>

FlowCalibrationModel::FlowCalibrationModel(QObject* parent)
    : QObject(parent)
{
}

FlowCalibrationModel::~FlowCalibrationModel()
{
    *m_destroyed = true;
}

void FlowCalibrationModel::setStorage(ShotHistoryStorage* storage) {
    m_storage = storage;
}

void FlowCalibrationModel::setSettings(Settings* settings) {
    m_settings = settings;
}

void FlowCalibrationModel::setDevice(DE1Device* device) {
    m_device = device;
}

void FlowCalibrationModel::setMultiplier(double m) {
    m = qBound(0.35, m, 2.0);
    if (qAbs(m_multiplier - m) > 0.001) {
        m_multiplier = m;
        recalculateFlow();
        emit multiplierChanged();
    }
}

void FlowCalibrationModel::setLoading(bool loading) {
    if (m_loading != loading) {
        m_loading = loading;
        emit loadingChanged();
    }
}

void FlowCalibrationModel::loadRecentShots() {
    if (!m_storage) return;

    setLoading(true);

    const QString dbPath = m_storage->databasePath();
    auto destroyed = m_destroyed;

    QThread* thread = QThread::create([this, dbPath, destroyed]() {
        const QString connName = QString("fcm_recent_%1")
            .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16);

        QVector<qint64> shotIds;
        ShotRecord firstRecord;

        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
            db.setDatabaseName(dbPath);
            if (db.open()) {
                // Get 50 most recent shot IDs
                QSqlQuery query(db);
                query.prepare("SELECT id FROM shots ORDER BY timestamp DESC LIMIT 50");
                if (query.exec()) {
                    while (query.next()) {
                        qint64 id = query.value(0).toLongLong();
                        ShotRecord record = ShotHistoryStorage::loadShotRecordStatic(db, id);
                        if (!record.weightFlowRate.isEmpty()) {
                            shotIds.append(id);
                            if (shotIds.size() == 1) {
                                firstRecord = std::move(record);
                            }
                        }
                        if (shotIds.size() >= 20) break;
                    }
                }
            } else {
                qWarning() << "FlowCalibrationModel: Failed to open DB for async loadRecentShots";
            }
        }
        QSqlDatabase::removeDatabase(connName);

        QMetaObject::invokeMethod(this, [this, shotIds = std::move(shotIds),
                                         firstRecord = std::move(firstRecord), destroyed]() {
            if (*destroyed) return;

            m_shotIds = shotIds;

            if (m_shotIds.isEmpty()) {
                m_errorMessage = tr("No shots with scale data found. Run a shot with a Bluetooth scale connected.");
                m_currentIndex = -1;
                m_originalFlow.clear();
                m_recalculatedFlow.clear();
                m_weightFlowRate.clear();
                m_pressure.clear();
                m_shotInfo.clear();
                emit errorChanged();
                emit dataChanged();
            } else {
                m_errorMessage.clear();
                m_currentIndex = 0;
                m_multiplier = m_settings ? m_settings->flowCalibrationMultiplier() : 1.0;
                emit multiplierChanged();
                emit errorChanged();

                // Apply the first record directly (already loaded on background thread)
                m_originalFlow = firstRecord.flow;
                m_weightFlowRate = firstRecord.weightFlowRate;
                m_pressure = firstRecord.pressure;
                m_shotMultiplier = 1.0;

                m_maxTime = 60.0;
                if (!m_pressure.isEmpty()) {
                    m_maxTime = m_pressure.last().x();
                } else if (!m_originalFlow.isEmpty()) {
                    m_maxTime = m_originalFlow.last().x();
                }

                QDateTime dt = QDateTime::fromSecsSinceEpoch(firstRecord.summary.timestamp);
                m_shotInfo = firstRecord.summary.profileName + " \u2014 " + dt.toString("MMM d, yyyy");

                recalculateFlow();
            }

            emit navigationChanged();
            setLoading(false);
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void FlowCalibrationModel::previousShot() {
    if (m_currentIndex > 0 && !m_loading) {
        m_currentIndex--;
        loadCurrentShot();
        emit navigationChanged();
    }
}

void FlowCalibrationModel::nextShot() {
    if (m_currentIndex < m_shotIds.size() - 1 && !m_loading) {
        m_currentIndex++;
        loadCurrentShot();
        emit navigationChanged();
    }
}

void FlowCalibrationModel::save() {
    if (m_settings) {
        m_settings->setFlowCalibrationMultiplier(m_multiplier);
        // Signal connection in MainController sends it to the machine
    }
}

void FlowCalibrationModel::resetToFactory() {
    setMultiplier(1.0);
}

void FlowCalibrationModel::loadCurrentShot() {
    if (m_currentIndex < 0 || m_currentIndex >= m_shotIds.size() || !m_storage) return;

    setLoading(true);

    const QString dbPath = m_storage->databasePath();
    const qint64 shotId = m_shotIds[m_currentIndex];
    auto destroyed = m_destroyed;

    QThread* thread = QThread::create([this, dbPath, shotId, destroyed]() {
        const QString connName = QString("fcm_shot_%1")
            .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16);

        ShotRecord record;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
            db.setDatabaseName(dbPath);
            if (db.open())
                record = ShotHistoryStorage::loadShotRecordStatic(db, shotId);
            else
                qWarning() << "FlowCalibrationModel: Failed to open DB for async loadCurrentShot";
        }
        QSqlDatabase::removeDatabase(connName);

        QMetaObject::invokeMethod(this, [this, record = std::move(record), destroyed]() {
            if (*destroyed) return;

            m_originalFlow = record.flow;
            m_weightFlowRate = record.weightFlowRate;
            m_pressure = record.pressure;
            m_shotMultiplier = 1.0;

            m_maxTime = 60.0;
            if (!m_pressure.isEmpty()) {
                m_maxTime = m_pressure.last().x();
            } else if (!m_originalFlow.isEmpty()) {
                m_maxTime = m_originalFlow.last().x();
            }

            QDateTime dt = QDateTime::fromSecsSinceEpoch(record.summary.timestamp);
            m_shotInfo = record.summary.profileName + " \u2014 " + dt.toString("MMM d, yyyy");

            recalculateFlow();
            setLoading(false);
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void FlowCalibrationModel::recalculateFlow() {
    m_recalculatedFlow.clear();
    m_recalculatedFlow.reserve(m_originalFlow.size());

    double shotMul = (m_shotMultiplier > 0.001) ? m_shotMultiplier : 1.0;
    for (const auto& pt : m_originalFlow) {
        double newY = m_multiplier * pt.y() / shotMul;
        m_recalculatedFlow.append(QPointF(pt.x(), newY));
    }

    emit dataChanged();
}

QVariantList FlowCalibrationModel::flowData() const {
    return pointsToVariant(m_recalculatedFlow);
}

QVariantList FlowCalibrationModel::weightFlowData() const {
    return pointsToVariant(m_weightFlowRate);
}

QVariantList FlowCalibrationModel::pressureData() const {
    return pointsToVariant(m_pressure);
}

QVariantList FlowCalibrationModel::pointsToVariant(const QVector<QPointF>& points) const {
    QVariantList result;
    for (const auto& pt : points) {
        QVariantMap p;
        p["x"] = pt.x();
        p["y"] = pt.y();
        result.append(p);
    }
    return result;
}
