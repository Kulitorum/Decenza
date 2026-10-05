#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>

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
//    read for a freeze or thaw done there, descriptive fields Decenza is
//    missing (VisualizerSync::bagFieldPullChanges), and a photo either side
//    lacks (downloaded into, or uploaded from, the bag photo cache).
// Edits flow the other way through VisualizerUploader. A pass runs at startup
// and every kPassIntervalMs, only while Visualizer is on and automatic update
// is on — the setting that already means "keep Visualizer and Decenza in step".
// Requests are paced by VisualizerUploader::kApiRequestIntervalMs and share
// its account.
class VisualizerShotSync : public QObject {
    Q_OBJECT

public:
    VisualizerShotSync(VisualizerUploader* uploader, ShotHistoryStorage* shots, CoffeeBagStorage* bags,
                       BeanBaseClient* beanbase, QNetworkAccessManager* networkManager, Settings* settings,
                       QObject* parent = nullptr);

    // Starts a pass unless one is running or sync is off.
    void start();

    // Thirty minutes: an edit made on visualizer.coffee is back within a coffee
    // break, at one list request per pass when nothing changed.
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

    bool enabled() const;
    qint64 loadCursor() const;
    void saveCursor(qint64 cursor) const;
    void paced(std::function<void()> send);

    void fetchListPage(int page);
    void lookUpLinkedShots();
    void readNextShot();
    void finishShots(bool complete, const QString& failure);
    void startBags();
    void fetchBagListPage(int page);
    void applyBagArchiveState();
    void readNextBag();
    void syncBagPhoto(qint64 bagId, const QString& beanBaseId, const QString& remoteId,
                      const QString& remoteImageUrl);
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
    QStringList m_changedIds;
    QList<ShotToRead> m_shotQueue;
    QList<QVariantMap> m_bagQueue;
    QHash<QString, QString> m_remoteArchivedAt;  // Visualizer bag id -> archived_at ("" = active)
    int m_shotsRead = 0;
    QString m_lastShotFailure;
    QString m_lastBagFailure;
};
