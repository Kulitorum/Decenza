#include "shothistoryexporter.h"

#include "shothistorystorage.h"
#include "../core/profilestorage.h"
#include "../core/settings.h"
#include "../core/dbutils.h"
#include "../network/visualizeruploader.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
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

    connect(m_settings, &Settings::exportShotsToFileChanged,
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
    if (m_settings->exportShotsToFile()) {
        startBulkExport();
    }
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
    // ShotHistoryStorage emits shotSaved(-1) on precondition failures.
    if (shotId <= 0) return;
    if (!m_settings->exportShotsToFile()) return;
    // Bulk export will pick up any IDs that arrive during its run via its
    // own post-loop catch-up query, so skip the live handler to avoid races.
    if (m_bulkRunning.load()) return;
    exportSingleShot(shotId);
}

void ShotHistoryExporter::onShotMetadataUpdated(qint64 shotId, bool success)
{
    if (!success) return;
    if (shotId <= 0) return;
    if (!m_settings->exportShotsToFile()) return;
    if (m_bulkRunning.load()) return;
    exportSingleShot(shotId);
}

void ShotHistoryExporter::onShotDeleted(qint64 shotId)
{
    if (shotId <= 0) return;
    deleteExportedShot(shotId);
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
        auto selectAllIds = [&dbPath](QList<qint64>& out) {
            withTempDb(dbPath, "she_ids", [&](QSqlDatabase& db) {
                QSqlQuery q(db);
                if (!q.exec(QStringLiteral("SELECT id FROM shots ORDER BY id ASC"))) {
                    qWarning() << "ShotHistoryExporter: id enumeration failed:" << q.lastError().text();
                    return;
                }
                while (q.next()) out.append(q.value(0).toLongLong());
            });
        };

        QList<qint64> ids;
        selectAllIds(ids);

        QSet<qint64> seen;
        int ok = 0, failed = 0;
        for (qint64 id : ids) {
            if (*destroyed) return;
            seen.insert(id);
            if (writeShotJson(dbPath, historyDir, id)) ok++;
            else failed++;
        }

        // Catch-up pass: shots saved after the initial SELECT but before we
        // flip m_bulkRunning to false are ignored by onShotSaved (which
        // early-returns while bulk is running). Re-query and process any
        // that landed during this run so they still end up on disk.
        QList<qint64> latest;
        selectAllIds(latest);
        for (qint64 id : latest) {
            if (*destroyed) return;
            if (seen.contains(id)) continue;
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
