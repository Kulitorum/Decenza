#include "shothistoryexporter.h"

#include "shothistorystorage.h"
#include "../core/profilestorage.h"
#include "../core/settings.h"
#include "../core/settings_network.h"
#include "../core/dbutils.h"
#include "../network/visualizeruploader.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
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

    connect(m_settings->network(), &SettingsNetwork::exportShotsToFileChanged,
            this, &ShotHistoryExporter::onExportToggleChanged);
    connect(m_storage, &ShotHistoryStorage::shotSaved,
            this, &ShotHistoryExporter::onShotSaved);
    connect(m_storage, &ShotHistoryStorage::shotMetadataUpdated,
            this, &ShotHistoryExporter::onShotMetadataUpdated);
    // shotsDeleted fires in addition to per-id shotDeleted for batch deletes,
    // so we intentionally subscribe only to shotDeleted to avoid N+1 racing
    // threads trying to remove the same files.
    connect(m_storage, &ShotHistoryStorage::shotDeleted,
            this, &ShotHistoryExporter::onShotDeleted);

    // If the toggle was already on from a previous session, re-export on
    // startup so files are self-healed even if the user cleared the folder
    // while the app was closed.
    if (m_settings->network()->exportShotsToFile()) {
        startBulkExport();
    }
}

ShotHistoryExporter::~ShotHistoryExporter()
{
    *m_destroyed = true;
}

void ShotHistoryExporter::onExportToggleChanged()
{
    if (m_settings->network()->exportShotsToFile()) {
        startBulkExport();
    }
}

void ShotHistoryExporter::onShotSaved(qint64 shotId)
{
    // ShotHistoryStorage emits shotSaved(-1) on precondition failures.
    if (shotId <= 0) return;
    if (!m_settings->network()->exportShotsToFile()) return;
    // Bulk export will pick up any IDs that arrive during its run via its
    // own post-loop catch-up query, so skip the live handler to avoid races.
    if (m_bulkRunning.load()) return;
    exportSingleShot(shotId);
}

void ShotHistoryExporter::onShotMetadataUpdated(qint64 shotId, bool success)
{
    if (!success) return;
    if (shotId <= 0) return;
    if (!m_settings->network()->exportShotsToFile()) return;
    if (m_bulkRunning.load()) return;
    exportSingleShot(shotId);
}

void ShotHistoryExporter::onShotDeleted(qint64 shotId)
{
    if (shotId <= 0) return;
    deleteExportedShot(shotId);
}

namespace {

struct ShotRefreshInfo {
    qint64 id = 0;
    qint64 timestamp = 0;
    qint64 updatedAt = 0;
};

QString exportedFilePath(const QString& historyDir, qint64 timestamp, qint64 shotId)
{
    const QDateTime dt = QDateTime::fromSecsSinceEpoch(timestamp);
    return historyDir + "/" + QString("%1_%2.json")
        .arg(dt.toString("yyyyMMddTHHmmss"))
        .arg(shotId);
}

// Returns true if the exported file is present and was written after the
// shot's last DB update (so its contents are known to be current).
bool exportedFileIsFresh(const QString& historyDir, const ShotRefreshInfo& info)
{
    const QFileInfo fi(exportedFilePath(historyDir, info.timestamp, info.id));
    if (!fi.exists() || fi.size() <= 0) return false;
    return fi.lastModified().toSecsSinceEpoch() >= info.updatedAt;
}

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

    ShotProjection shotData = ShotHistoryStorage::convertShotRecord(record);
    QByteArray payload = VisualizerUploader::buildHistoryShotJson(shotData);

    const QString fullPath = exportedFilePath(historyDir, record.summary.timestamp, shotId);

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
        auto selectAllShots = [&dbPath](QList<ShotRefreshInfo>& out) {
            withTempDb(dbPath, "she_ids", [&](QSqlDatabase& db) {
                QSqlQuery q(db);
                if (!q.exec(QStringLiteral(
                        "SELECT id, timestamp, COALESCE(updated_at, 0) "
                        "FROM shots ORDER BY id ASC"))) {
                    qWarning() << "ShotHistoryExporter: id enumeration failed:" << q.lastError().text();
                    return;
                }
                while (q.next()) {
                    ShotRefreshInfo info;
                    info.id = q.value(0).toLongLong();
                    info.timestamp = q.value(1).toLongLong();
                    info.updatedAt = q.value(2).toLongLong();
                    out.append(info);
                }
            });
        };

        QList<ShotRefreshInfo> shots;
        selectAllShots(shots);

        QSet<qint64> seen;
        int written = 0, skipped = 0, failed = 0;
        for (const ShotRefreshInfo& info : shots) {
            if (*destroyed) return;
            seen.insert(info.id);
            if (exportedFileIsFresh(historyDir, info)) {
                skipped++;
                continue;
            }
            if (writeShotJson(dbPath, historyDir, info.id)) written++;
            else failed++;
        }

        // Catch-up pass: shots saved after the initial SELECT but before we
        // flip m_bulkRunning to false are ignored by onShotSaved (which
        // early-returns while bulk is running). Re-query and process any
        // that landed during this run so they still end up on disk.
        QList<ShotRefreshInfo> latest;
        selectAllShots(latest);
        for (const ShotRefreshInfo& info : latest) {
            if (*destroyed) return;
            if (seen.contains(info.id)) continue;
            if (exportedFileIsFresh(historyDir, info)) {
                skipped++;
                continue;
            }
            if (writeShotJson(dbPath, historyDir, info.id)) written++;
            else failed++;
        }

        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, written, skipped, failed, destroyed]() {
            if (*destroyed) return;
            m_bulkRunning.store(false);
            emit bulkExportFinished(written, skipped, failed);
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

void ShotHistoryExporter::deleteExportedShot(qint64 shotId)
{
    // Skip entirely if the feature was never used — avoids creating an empty
    // history folder as a side effect of a deletion.
    const QString historyDir = m_profileStorage->userHistoryPathIfExists();
    if (historyDir.isEmpty()) return;
    auto destroyed = m_destroyed;

    QThread* thread = QThread::create([historyDir, shotId, destroyed]() {
        if (*destroyed) return;
        QDir dir(historyDir);
        const QStringList matches = dir.entryList(
            {QString("*_%1.json").arg(shotId)}, QDir::Files);
        for (const QString& name : matches) {
            if (!QFile::remove(dir.filePath(name))) {
                qWarning() << "ShotHistoryExporter: remove failed for" << name;
            }
        }
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}
