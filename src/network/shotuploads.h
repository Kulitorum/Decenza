#pragma once

#include "shotuploaddestination.h"

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

class QSqlDatabase;
class QTimer;
class SettingsUpload;
class ShotHistoryStorage;

// The one path every shot upload takes, for every destination. It applies the
// shared upload settings (SettingsUpload) once, then queues the shot for each
// active destination, which receives one shot at a time. Every send gets the
// same kAttempts attempts and its outcome is recorded the same way (D15), except
// that a 429 ends the send at once: the destination's queue then waits until
// SettingsUpload::rateLimitedUntilMs(), whoever got the 429.
//
//  - shotSaved: a finished shot, when automatic upload is on.
//  - an edit (ShotHistoryStorage::shotMetadataUpdated, or one pulled from
//    Visualizer), when automatic update is on: only destinations already
//    holding the shot are updated.
//  - uploadNow: the Upload button, the layout action and MCP.
//  - holdUpdates/expectHeldEdit/releaseUpdates: the review page saves on every
//    field, so while it is open ITS saves are collected and sent once when it
//    closes. Edits from anywhere else still go out at once.
//  - uploadMissing/resumeMissingRuns: Upload missing shots (D14, below).
class ShotUploads : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("ShotUploads is created in C++ and reached via MainController")

    // Per active destination, by name: {count, failed, unsentEdits, running, done,
    // sent, total, resumeAtMs, waitingForMachine}, for the Upload missing shots
    // button. Counted on a worker thread. resumeAtMs (ms since the epoch) is set
    // while a 429 holds the queue; waitingForMachine while a run waits for the DE1.
    Q_PROPERTY(QVariantMap missing READ missing NOTIFY missingChanged FINAL)

public:
    ShotUploads(SettingsUpload* settings, ShotHistoryStorage* storage,
                QList<ShotUploadDestination*> destinations, QObject* parent = nullptr);

    // Uploads, or updates, the shot on every active destination now.
    Q_INVOKABLE void uploadNow(qint64 shotId);
    // A shot just finished and saved.
    void shotSaved(qint64 shotId);

    // Names of the destinations switched on and connected.
    QStringList activeDestinations() const;
    // Names of the destinations already holding the shot. Reads only `db`, so it
    // runs on a worker thread.
    QStringList destinationsHolding(QSqlDatabase& db, qint64 shotId) const;
    // Of `holding`, those an edit is sent to now: none while automatic update is off.
    QStringList autoUpdateDestinations(const QStringList& holding) const;

    Q_INVOKABLE void holdUpdates(qint64 shotId);
    // The holding page is about to save the shot; that save waits for release.
    Q_INVOKABLE void expectHeldEdit(qint64 shotId);
    Q_INVOKABLE void releaseUpdates(qint64 shotId);

    // What a destination is missing (D14): edits it has not received, then shots
    // it does not hold, newest first; never a rejected or ineligible shot.
    struct Missing {
        QList<qint64> shotIds;   // unsent edits first, then missing shots
        int unsentEdits = 0;
        int failed = 0;          // of the missing shots, those whose upload failed
        bool readable = false;   // false: the history could not be read, which is not "nothing missing"
    };
    // Reads only `db`, so it runs on a worker thread. A shot that failed at or
    // after `skipFailedSince` (seconds since the epoch; 0 = none) is left out.
    static Missing findMissing(QSqlDatabase& db, const ShotUploadDestination& destination, double minDurationSec,
                               qint64 skipFailedSince = 0);

    // Upload missing shots (D14): sends what findMissing lists for the
    // destination through its queue, kBatchSize at a time, batches at least the
    // batch spacing apart, each send paced with the destination's other background
    // requests (paceBackground), starting none while the machine is operating. A
    // shot that fails its attempts is recorded and the run moves on; a sign-in or
    // account refusal ends it. Never starts on its own: only from this, or from
    // resumeMissingRuns() for a run the user started before a restart.
    // False when it starts nothing: unknown or inactive destination, or a run already going.
    Q_INVOKABLE bool uploadMissing(const QString& destination);
    // At startup: picks up runs a restart interrupted, leaving out the shots that
    // already failed during them.
    void resumeMissingRuns();
    // MachineState::isOperating(): no Upload missing shots send starts while it is true.
    void setMachineOperating(bool operating);
    // A destination's backgroundSendReady() may have changed (Decent: the DE1 connected).
    void readinessChanged();
    QVariantMap missing() const;
    // Counts what each active destination is missing again; a call while a count
    // is running counts once more after it.
    Q_INVOKABLE void refreshMissing();

    static constexpr int kBatchSize = 5;
    // Decaid's cadence: a rate limit, so the timer is not a guard.
    void setBatchSpacingMs(int ms) { m_batchSpacingMs = ms; }

    static constexpr int kAttempts = 3;
    // The first retry waits this long, the second twice as long (Decaid: 2 s, 4 s).
    void setRetryDelayMs(int ms) { m_retryDelayMs = ms; }

signals:
    void missingChanged();

private:
    using Send = ShotUploadDestination::Send;
    struct Job {
        qint64 shotId;
        Send how;
        bool background;   // Upload missing shots: paced; anything else goes at once
    };

    using Attempt = ShotUploadDestination::Attempt;
    using Outcome = ShotUploadDestination::Outcome;
    struct Current {
        qint64 shotId = 0;
        Send how = Send::UploadOrUpdate;
        int attempt = 0;
        Attempt last;   // the attempt a retry waits after
    };

    void onShotEdited(qint64 shotId, bool success);
    void onShotPulled(qint64 shotId, const QVariantMap& previous, const QVariantMap& written);
    void noteEdited(qint64 shotId);
    void enqueue(qint64 shotId, Send how);
    void enqueueTo(ShotUploadDestination* destination, qint64 shotId, Send how, bool background);
    void pump(ShotUploadDestination* destination);
    ShotUploadDestination* destinationNamed(const QString& name) const;
    void startRun(ShotUploadDestination* destination, qint64 skipFailedSince);
    void nextBatch(ShotUploadDestination* destination);
    void endRun(ShotUploadDestination* destination, const QString& why);
    struct Run;
    void waitBeforeNextBatch(ShotUploadDestination* destination, Run& run, qint64 ms);
    // Holds or releases the destination's queue to match SettingsUpload::rateLimitedUntilMs().
    void followRateLimit(ShotUploadDestination* destination);
    // Drops what is queued for a destination, and its run; logs how many and why.
    void dropQueue(ShotUploadDestination* destination, const QString& why);
    // findMissing over a destination's name and conditions, which a worker can hold by value.
    static Missing findMissingFor(QSqlDatabase& db, const QString& name, const QString& held, const QString& unsent,
                                  double minDurationSec, qint64 skipFailedSince);
    void onAttempt(ShotUploadDestination* destination, Attempt attempt);
    void finishSend(ShotUploadDestination* destination, Attempt last);

    SettingsUpload* m_settings;
    ShotHistoryStorage* m_storage;
    QList<ShotUploadDestination*> m_destinations;
    QHash<ShotUploadDestination*, QList<Job>> m_queues;
    QHash<ShotUploadDestination*, Current> m_current;
    QHash<ShotUploadDestination*, Job> m_pacing;   // a background send waiting for its pacer turn
    int m_retryDelayMs = 2000;
    // A destination's queue is held while it has an entry here; the timer ends the hold.
    QHash<ShotUploadDestination*, QTimer*> m_rateLimitWaits;

    struct Run {
        int id = 0;                   // the batch-spacing timer acts only on the run that set it
        bool selecting = true;        // findMissing is still running
        bool waiting = false;         // a batch-spacing wait is pending
        bool paused = false;          // waiting for the machine to stop operating (logged once)
        int unsentLeft = 0;           // unsent edits still at the front of `pending`
        QList<qint64> pending;        // still to send, in order
        QSet<qint64> outstanding;     // the batch being sent
        int done = 0;                 // finished, however they went
        int sent = 0;
        int failed = 0;
        int total = 0;
        QElapsedTimer sinceBatch;
    };
    int m_nextRunId = 0;
    QHash<ShotUploadDestination*, Run> m_runs;
    QHash<QString, Missing> m_counts;   // by destination name, active ones only
    bool m_counting = false;
    bool m_countAgain = false;
    bool m_machineOperating = false;
    int m_batchSpacingMs = 30000;
    struct Held {
        bool edited = false;   // a page save to send on release
        int pending = 0;       // page saves whose shotMetadataUpdated is still to come
        int uploaded = 0;      // of those, how many an uploadNow already took
    };
    QHash<qint64, Held> m_held;
};
