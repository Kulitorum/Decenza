#pragma once

#include <QObject>
#include <QString>
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
//  * Toggle off -> on, and app startup with the toggle already on:
//    bulk pass that self-heals anything missing or out of date. Each shot's
//    existing file is kept if its mtime is >= the shot's updated_at column,
//    so a warm run with no DB changes performs no writes (and no shot-record
//    load / JSON parse, which is where the real cost lived).
//  * Toggle on: incrementally export each new shot when shotSaved fires,
//               refresh on metadata updates.
//  * Shot deletions always remove matching exported files regardless of the
//    toggle state, so files don't become orphaned if the user turns the
//    toggle off and later deletes shots from the DB.
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
    // written + skipped + failed together equal the total shots attempted.
    // "skipped" counts shots whose on-disk export was already current
    // (file mtime >= shot updated_at), so no write was performed.
    void bulkExportFinished(int written, int skipped, int failed);

private slots:
    void onExportToggleChanged();
    void onShotSaved(qint64 shotId);
    void onShotMetadataUpdated(qint64 shotId, bool success);
    void onShotDeleted(qint64 shotId);

private:
    void startBulkExport();
    void exportSingleShot(qint64 shotId);
    void deleteExportedShot(qint64 shotId);

    Settings* m_settings;
    ProfileStorage* m_profileStorage;
    ShotHistoryStorage* m_storage;

    std::shared_ptr<std::atomic<bool>> m_destroyed;
    std::atomic<bool> m_bulkRunning{false};
};
