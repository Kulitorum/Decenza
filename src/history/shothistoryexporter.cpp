#include "shothistoryexporter.h"

#include "shothistorystorage.h"
#include "../core/profilestorage.h"
#include "../core/settings.h"
#include "../core/dbutils.h"
#include "../network/visualizeruploader.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSqlQuery>
#include <QSqlError>
#include <QThread>

ShotHistoryExporter::ShotHistoryExporter(Settings* settings,
                                         ProfileStorage* profileStorage,
                                         ShotHistoryStorage* storage,
                                         QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_profileStorage(profileStorage)
    , m_storage(storage)
    , m_destroyed(std::make_shared<std::atomic<bool>>(false))
{
    Q_ASSERT(m_settings);
    Q_ASSERT(m_profileStorage);
    Q_ASSERT(m_storage);

    connect(m_settings, &Settings::exportShotsToFileChanged,
            this, &ShotHistoryExporter::onExportToggleChanged);
    connect(m_storage, &ShotHistoryStorage::shotSaved,
            this, &ShotHistoryExporter::onShotSaved);
    connect(m_storage, &ShotHistoryStorage::shotMetadataUpdated,
            this, &ShotHistoryExporter::onShotMetadataUpdated);
    connect(m_storage, &ShotHistoryStorage::shotDeleted,
            this, &ShotHistoryExporter::onShotDeleted);
    connect(m_storage, &ShotHistoryStorage::shotsDeleted,
            this, &ShotHistoryExporter::onShotsDeleted);
}

ShotHistoryExporter::~ShotHistoryExporter()
{
    *m_destroyed = true;
}

void ShotHistoryExporter::onExportToggleChanged()
{
    if (m_settings->exportShotsToFile()) {
        startBulkExport();
    }
}

void ShotHistoryExporter::onShotSaved(qint64 shotId)
{
    if (!m_settings->exportShotsToFile()) return;
    if (m_bulkRunning.load()) return;
    exportSingleShot(shotId);
}

void ShotHistoryExporter::onShotMetadataUpdated(qint64 shotId, bool success)
{
    if (!success) return;
    if (!m_settings->exportShotsToFile()) return;
    if (m_bulkRunning.load()) return;
    exportSingleShot(shotId);
}

void ShotHistoryExporter::onShotDeleted(qint64 shotId)
{
    deleteExportedFiles({shotId});
}

void ShotHistoryExporter::onShotsDeleted(const QVariantList& shotIds)
{
    QList<qint64> ids;
    ids.reserve(shotIds.size());
    for (const auto& v : shotIds) ids.append(v.toLongLong());
    deleteExportedFiles(ids);
}

namespace {

bool writeShotJson(const QString& dbPath,
                   const QString& historyDir,
                   qint64 shotId)
{
    ShotRecord record;
    bool opened = withTempDb(dbPath, "she_shot", [&](QSqlDatabase& db) {
        record = ShotHistoryStorage::loadShotRecordStatic(db, shotId);
    });
    if (!opened || record.summary.id == 0) {
        qWarning() << "ShotHistoryExporter: failed to load shot" << shotId;
        return false;
    }

    QVariantMap shotData = ShotHistoryStorage::convertShotRecord(record);
    QByteArray payload = VisualizerUploader::buildHistoryShotJson(shotData);

    const QDateTime dt = QDateTime::fromSecsSinceEpoch(record.summary.timestamp);
    const QString filename = QString("%1_%2.json")
        .arg(dt.toString("yyyyMMddTHHmmss"))
        .arg(shotId);
    const QString fullPath = historyDir + "/" + filename;

    QSaveFile file(fullPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "ShotHistoryExporter: open failed for" << fullPath << ":" << file.errorString();
        return false;
    }
    file.write(payload);
    if (!file.commit()) {
        qWarning() << "ShotHistoryExporter: commit failed for" << fullPath << ":" << file.errorString();
        return false;
    }
    return true;
}

} // namespace

void ShotHistoryExporter::startBulkExport()
{
    bool expected = false;
    if (!m_bulkRunning.compare_exchange_strong(expected, true)) {
        qDebug() << "ShotHistoryExporter: bulk export already running";
        return;
    }

    const QString dbPath = m_storage->databasePath();
    const QString historyDir = m_profileStorage->userHistoryPath();
    auto destroyed = m_destroyed;

    QThread* thread = QThread::create([this, dbPath, historyDir, destroyed]() {
        QList<qint64> ids;
        withTempDb(dbPath, "she_ids", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            if (!q.exec(QStringLiteral("SELECT id FROM shots ORDER BY id ASC"))) {
                qWarning() << "ShotHistoryExporter: id enumeration failed:" << q.lastError().text();
                return;
            }
            while (q.next()) ids.append(q.value(0).toLongLong());
        });

        int ok = 0, failed = 0;
        for (qint64 id : ids) {
            if (*destroyed) return;
            if (writeShotJson(dbPath, historyDir, id)) ok++;
            else failed++;
        }

        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, ok, failed, destroyed]() {
            if (*destroyed) return;
            m_bulkRunning.store(false);
            emit bulkExportFinished(ok, failed);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void ShotHistoryExporter::exportSingleShot(qint64 shotId)
{
    const QString dbPath = m_storage->databasePath();
    const QString historyDir = m_profileStorage->userHistoryPath();
    auto destroyed = m_destroyed;

    QThread* thread = QThread::create([dbPath, historyDir, shotId, destroyed]() {
        if (*destroyed) return;
        writeShotJson(dbPath, historyDir, shotId);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void ShotHistoryExporter::deleteExportedFiles(const QList<qint64>& shotIds)
{
    if (shotIds.isEmpty()) return;
    const QString historyDir = m_profileStorage->userHistoryPath();
    auto destroyed = m_destroyed;

    QThread* thread = QThread::create([historyDir, shotIds, destroyed]() {
        if (*destroyed) return;
        QDir dir(historyDir);
        if (!dir.exists()) return;
        for (qint64 id : shotIds) {
            const QStringList matches = dir.entryList(
                {QString("*_%1.json").arg(id)}, QDir::Files);
            for (const QString& name : matches) {
                if (!QFile::remove(dir.filePath(name))) {
                    qWarning() << "ShotHistoryExporter: remove failed for" << name;
                }
            }
        }
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}
