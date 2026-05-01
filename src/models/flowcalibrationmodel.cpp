#include "flowcalibrationmodel.h"
#include "../history/shothistorystorage.h"
#include "../core/settings_calibration.h"
#include "../ble/de1device.h"

#include <QDateTime>
#include <QDebug>
#include <QThread>
#include <QSqlDatabase>
#include "../core/dbutils.h"
#include <QSqlError>
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

void FlowCalibrationModel::setSettings(SettingsCalibration* settings) {
    m_settings = settings;
}

void FlowCalibrationModel::setDevice(DE1Device* device) {
    m_device = device;
}

void FlowCalibrationModel::setMultiplier(double m) {
    m = qBound(0.35, m, 3.0);
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
    if (!m_storage || m_loading) return;

    setLoading(true);

    const QString dbPath = m_storage->databasePath();
    auto destroyed = m_destroyed;

    QThread* thread = QThread::create([this, dbPath, destroyed]() {
        QVector<qint64> shotIds;
        ShotRecord firstRecord;
        bool dbFailed = !withTempDb(dbPath, "fcm_recent", [&](QSqlDatabase& db) {
            QSqlQuery query(db);
            if (!query.prepare("SELECT id FROM shots ORDER BY timestamp DESC LIMIT 50")) {
                qWarning() << "FlowCalibrationModel: query prepare failed:" << query.lastError().text();
            } else if (query.exec()) {
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
            } else {
                qWarning() << "FlowCalibrationModel: query exec failed:" << query.lastError().text();
            }
        });

        QMetaObject::invokeMethod(this, [this, shotIds = std::move(shotIds),
                                         firstRecord = std::move(firstRecord), dbFailed, destroyed]() {
            if (*destroyed) {
                qDebug() << "FlowCalibrationModel: loadRecentShots callback dropped (object destroyed)";
                return;
            }

            m_shotIds = shotIds;

            if (m_shotIds.isEmpty()) {
                m_errorMessage = dbFailed
                    ? tr("Could not access shot database. Try restarting the app.")
                    : tr("No shots with scale data found. Run a shot with a Bluetooth scale connected.");
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

                applyShotRecord(firstRecord);
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
        m_shotInfo.clear();
        emit navigationChanged();
        loadCurrentShot();
    }
}

void FlowCalibrationModel::nextShot() {
    if (m_currentIndex < m_shotIds.size() - 1 && !m_loading) {
        m_currentIndex++;
        m_shotInfo.clear();
        emit navigationChanged();
        loadCurrentShot();
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
    if (m_currentIndex < 0 || m_currentIndex >= m_shotIds.size() || !m_storage || m_loading) return;

    setLoading(true);

    const QString dbPath = m_storage->databasePath();
    const qint64 shotId = m_shotIds[m_currentIndex];
    auto destroyed = m_destroyed;

    QThread* thread = QThread::create([this, dbPath, shotId, destroyed]() {
        ShotRecord record;
        bool dbFailed = !withTempDb(dbPath, "fcm_shot", [&](QSqlDatabase& db) {
            record = ShotHistoryStorage::loadShotRecordStatic(db, shotId);
        });

        QMetaObject::invokeMethod(this, [this, record = std::move(record), dbFailed, destroyed]() {
            if (*destroyed) {
                qDebug() << "FlowCalibrationModel: loadCurrentShot callback dropped (object destroyed)";
                return;
            }

            if (dbFailed || record.summary.id == 0) {
                m_errorMessage = tr("Failed to load shot data. The database may be unavailable.");
                emit errorChanged();
                setLoading(false);
                return;
            }

            applyShotRecord(record);
            setLoading(false);
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void FlowCalibrationModel::applyShotRecord(const ShotRecord& record) {
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
