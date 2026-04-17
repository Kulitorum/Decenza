#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <atomic>
#include <memory>

class Settings;
class ProfileStorage;
class ShotHistoryStorage;

// ShotHistoryExporter mirrors shots from the SQLite DB into individual
// visualizer-format JSON files under ProfileStorage::userHistoryPath(),
// driven by Settings::exportShotsToFile.
//
// Design:
//  * Stateless when off: no tracking of "which shots have been exported."
//  * Toggle off -> on: full re-export overwriting whatever is on disk.
//  * Toggle on: incrementally export each new shot when shotSaved fires,
//               refresh on metadata updates, and delete files when shots
//               are deleted from the DB.
//  * All disk and DB I/O runs on background threads.
class ShotHistoryExporter : public QObject {
    Q_OBJECT
public:
    explicit ShotHistoryExporter(Settings* settings,
                                 ProfileStorage* profileStorage,
                                 ShotHistoryStorage* storage,
                                 QObject* parent = nullptr);
    ~ShotHistoryExporter() override;

signals:
    // Emitted on the main thread after the initial bulk export finishes.
    // ok + failed together equal the total shots attempted.
    void bulkExportFinished(int ok, int failed);

private slots:
    void onExportToggleChanged();
    void onShotSaved(qint64 shotId);
    void onShotMetadataUpdated(qint64 shotId, bool success);
    void onShotDeleted(qint64 shotId);
    void onShotsDeleted(const QVariantList& shotIds);

private:
    void startBulkExport();
    void exportSingleShot(qint64 shotId);
    void deleteExportedFiles(const QList<qint64>& shotIds);

    Settings* m_settings;
    ProfileStorage* m_profileStorage;
    ShotHistoryStorage* m_storage;

    std::shared_ptr<std::atomic<bool>> m_destroyed;
    std::atomic<bool> m_bulkRunning{false};
};
