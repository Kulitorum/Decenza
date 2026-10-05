#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QElapsedTimer>
#include <QTimer>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>
#include <optional>

class BeanBaseClient;
class CoffeeBagStorage;
class QNetworkAccessManager;
class Settings;
class ShotHistoryStorage;
class VisualizerUploader;

// Brings edits made on visualizer.coffee back to Decenza (visualizer-two-way-sync):
//  - shots: every shot changed there since the last pass (GET /api/shots with
//    updated_after) that is linked to a local shot is read, and the fields
//    VisualizerSync::shotPullChanges selects are written locally;
//  - coffee bags: the bag list gives every bag's archive state
//    (VisualizerSync::bagArchivePullChanges); each bag still in inventory is
//    read for fields changed there (VisualizerSync::bagFieldPullChanges) and a
//    photo either side lacks (downloaded into, or uploaded from, the cache).
// Edits flow the other way through VisualizerUploader. A pass runs at startup,
// every kPassIntervalMs and on account connect; a screen showing a shot or bags
// refreshes them at once. Only while Visualizer is on and automatic update is
// on — the setting that already means "keep Visualizer and Decenza in step".
// Requests go through VisualizerUploader::paceApiRequest and share its account.
class VisualizerShotSync : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("VisualizerShotSync is created in C++ and reached via MainController")

public:
    VisualizerShotSync(VisualizerUploader* uploader, ShotHistoryStorage* shots, CoffeeBagStorage* bags,
                       BeanBaseClient* beanbase, QNetworkAccessManager* networkManager, Settings* settings,
                       QObject* parent = nullptr);

    // Starts a pass unless one is running or sync is off.
    void start();

    // A screen is about to show these: read them from Visualizer now rather
    // than at the next pass. No-ops when sync is off or the item is not linked.
    Q_INVOKABLE void refreshShot(qint64 shotId);
    Q_INVOKABLE void refreshBag(qint64 bagId);
    // The bag half of a pass (archive state of every bag, then the bags in use).
    Q_INVOKABLE void refreshBags();

    // Screens refresh what they show on open, so the background pass can be
    // slow. An idle pass costs the shot list, the bag list and one read per
    // synced bag in inventory.
    static constexpr int kPassIntervalMs = 30 * 60 * 1000;
    // How far back the first pass on an account looks. Every shot uploaded in
    // the window has changed since, so each costs a read; two weeks bounds how
    // long that first pass holds most of the shared rate budget.
    static constexpr qint64 kFirstPassWindowSecs = 14 * 24 * 3600;
    // A screen asking again within this re-reads nothing: each bag pass costs a
    // request per bag in use.
    static constexpr qint64 kBagRefreshMinIntervalMs = 3 * 60 * 1000;

private:
    struct ShotToRead {
        QString visualizerId;
        qint64 shotId;
    };
    struct BagToRead {
        qint64 bagId;
        QString visualizerBagId;
        QString beanBaseId;
        bool inInventory;
        // archived_at as last seen on Visualizer, when it has been seen at all.
        std::optional<QString> seenArchivedAt = std::nullopt;
    };

    struct Failure {
        QString message;
        int repeats = 0;
    };

    bool enabled() const;
    qint64 loadCursor() const;
    void saveCursor(qint64 cursor) const;
    void paced(std::function<void()> send);
    static bool isAccountWideFailure(int httpStatus);

    void fetchListPage(int page);
    void lookUpLinkedShots();
    void readNextShot();
    // GETs one shot and applies it, then done(failure, accountWide): an empty
    // failure means read and saved, gone there, or skipped for a push since.
    void readShot(const ShotToRead& shot, std::function<void(const QString&, bool)> done);
    void finishShots(bool complete, const QString& failure);
    void startBags();
    void fetchBagListPage(int page);
    void applyBagArchiveState();
    void readNextBag();
    // GETs one bag and applies it (archive state too when `withArchive`).
    void readBag(const BagToRead& bag, bool withArchive, std::function<void(const QString&, bool)> done);
    void syncBagPhoto(const BagToRead& bag, const QString& remoteImageUrl, std::function<void()> done);
    void endPass();
    void noteFailure(Failure* failure, const QString& message);
    void noteRecovered(Failure* failure);

    VisualizerUploader* m_uploader;
    ShotHistoryStorage* m_shots;
    CoffeeBagStorage* m_bags;
    BeanBaseClient* m_beanbase;
    QNetworkAccessManager* m_networkManager;
    Settings* m_settings;
    QTimer m_timer;

    bool m_running = false;
    QString m_passAccount;  // the account a pass started on
    qint64 m_cursor = 0;
    qint64 m_newestChange = 0;
    qint64 m_listCount = -1;  // paging.count on the first list page
    QStringList m_changedIds;
    QList<ShotToRead> m_shotQueue;
    QList<BagToRead> m_bagQueue;
    QHash<QString, QString> m_remoteArchivedAt;  // Visualizer bag id -> archived_at ("" = active)
    QHash<qint64, quint64> m_bagGenerationAtList;  // bag push generation when the bag list was read
    QElapsedTimer m_lastBagPass;
    int m_shotsRead = 0;
    bool m_photoUploadRefused = false;
    Failure m_shotFailure;
    Failure m_bagFailure;
    Failure m_photoFailure;
};
