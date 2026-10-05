#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>

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

    // Thirty minutes: an edit made on visualizer.coffee is back within a coffee
    // break, since a screen showing it refreshes on its own. An idle pass costs
    // the shot list, the bag list and one read per synced bag in inventory.
    static constexpr int kPassIntervalMs = 30 * 60 * 1000;
    // How far back the first pass on an account looks. Every shot uploaded in
    // the window has changed since, so each costs a read; two weeks keeps a
    // daily-espresso account's first pass inside the shared rate budget.
    static constexpr qint64 kFirstPassWindowSecs = 14 * 24 * 3600;

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
    };

    bool enabled() const;
    qint64 loadCursor() const;
    void saveCursor(qint64 cursor) const;
    void paced(std::function<void()> send);

    void fetchListPage(int page);
    void lookUpLinkedShots();
    void readNextShot();
    // GETs one shot and applies it. done(failure): empty failure = read (or gone there).
    void readShot(const ShotToRead& shot, std::function<void(const QString& failure)> done);
    void finishShots(bool complete, const QString& failure);
    void startBags();
    void fetchBagListPage(int page);
    void applyBagArchiveState();
    void readNextBag();
    // GETs one bag and applies it (archive state too when `withArchive`).
    void readBag(const BagToRead& bag, bool withArchive, std::function<void(const QString& failure)> done);
    void syncBagPhoto(const BagToRead& bag, const QString& remoteImageUrl, std::function<void()> done);
    void endPass();
    // Reports a failure once, and its repeats only at DEBUG until that half of
    // a pass (shots or bags) next succeeds.
    void noteFailure(QString* last, const QString& failure);

    VisualizerUploader* m_uploader;
    ShotHistoryStorage* m_shots;
    CoffeeBagStorage* m_bags;
    BeanBaseClient* m_beanbase;
    QNetworkAccessManager* m_networkManager;
    Settings* m_settings;
    QTimer m_timer;

    bool m_running = false;
    qint64 m_cursor = 0;
    qint64 m_newestChange = 0;
    qint64 m_listCount = -1;  // paging.count on the first list page
    QStringList m_changedIds;
    QList<ShotToRead> m_shotQueue;
    QList<BagToRead> m_bagQueue;
    QHash<QString, QString> m_remoteArchivedAt;  // Visualizer bag id -> archived_at ("" = active)
    int m_shotsRead = 0;
    QString m_lastShotFailure;
    QString m_lastBagFailure;
};
