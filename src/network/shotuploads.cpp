#include "shotuploads.h"

#include "core/settings_upload.h"
#include "core/diagnosticlogging.h"
#include "history/shothistorystorage.h"
#include "network/shotpayloadhelpers.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QPointer>
#include <QSqlError>
#include <QSqlQuery>
#include "core/dbutils.h"

#include <QTimer>
#include <algorithm>
#include <limits>

ShotUploads::ShotUploads(SettingsUpload* settings, ShotHistoryStorage* storage,
                         QList<ShotUploadDestination*> destinations, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_storage(storage)
    , m_destinations(std::move(destinations))
{
    for (ShotUploadDestination* destination : std::as_const(m_destinations)) {
        // Queued: the attempt's own signals run before anything follows it.
        destination->setAttemptCallback([this, destination](Attempt attempt) {
            QMetaObject::invokeMethod(this, [this, destination, attempt]() { onAttempt(destination, attempt); },
                                      Qt::QueuedConnection);
        });
    }
    connect(storage, &ShotHistoryStorage::shotMetadataUpdated, this, &ShotUploads::onShotEdited);
    connect(storage, &ShotHistoryStorage::shotPulledFromVisualizer, this, &ShotUploads::onShotPulled);
    // Whatever can change what a destination is missing.
    for (auto changed : {&ShotHistoryStorage::readyChanged, &ShotHistoryStorage::historyDataChanged})
        connect(storage, changed, this, &ShotUploads::refreshMissing);
    for (auto changed : {&ShotHistoryStorage::shotSaved, &ShotHistoryStorage::shotDeleted})
        connect(storage, changed, this, &ShotUploads::refreshMissing);
    for (auto changed : {&ShotHistoryStorage::shotMetadataUpdated, &ShotHistoryStorage::visualizerInfoUpdated,
                         &ShotHistoryStorage::uploadOutcomeUpdated, &ShotHistoryStorage::decentUploadStateUpdated})
        connect(storage, changed, this, &ShotUploads::refreshMissing);
    connect(storage, &ShotHistoryStorage::shotsDeleted, this, &ShotUploads::refreshMissing);
    connect(storage, &ShotHistoryStorage::shotPulledFromVisualizer, this, &ShotUploads::refreshMissing);
    connect(settings, &SettingsUpload::minDurationChanged, this, &ShotUploads::refreshMissing);
    // The history may already be ready (MainController initializes it first): readyChanged would not come.
    refreshMissing();
}

void ShotUploads::uploadNow(qint64 shotId) {
    if (shotId <= 0) return;
    // The page's saves so far, including those still being written, go with this
    // upload: it reads the row after every queued write.
    const auto held = m_held.find(shotId);
    if (held != m_held.end()) {
        held->edited = false;
        held->uploaded = held->pending;
    }
    enqueue(shotId, Send::UploadOrUpdate);
}

void ShotUploads::shotSaved(qint64 shotId) {
    if (shotId > 0 && m_settings->autoUpload()) enqueue(shotId, Send::UploadOrUpdate);
}

QStringList ShotUploads::activeDestinations() const {
    QStringList names;
    for (const ShotUploadDestination* destination : m_destinations)
        if (destination->isActive()) names << destination->name();
    return names;
}

QStringList ShotUploads::destinationsHolding(QSqlDatabase& db, qint64 shotId) const {
    QStringList names;
    for (const ShotUploadDestination* destination : m_destinations)
        if (destination->holdsShot(db, shotId)) names << destination->name();
    return names;
}

QStringList ShotUploads::autoUpdateDestinations(const QStringList& holding) const {
    QStringList names;
    if (!m_settings->autoUpdate()) return names;
    for (const QString& name : activeDestinations())
        if (holding.contains(name)) names << name;
    return names;
}

void ShotUploads::holdUpdates(qint64 shotId) {
    if (shotId > 0 && !m_held.contains(shotId)) m_held.insert(shotId, Held{});
}

void ShotUploads::expectHeldEdit(qint64 shotId) {
    const auto held = m_held.find(shotId);
    if (held != m_held.end()) ++held->pending;
}

void ShotUploads::releaseUpdates(qint64 shotId) {
    const auto it = m_held.constFind(shotId);
    if (it == m_held.constEnd()) return;
    const bool edited = it->edited;
    m_held.erase(it);
    if (edited && m_settings->autoUpdate()) enqueue(shotId, Send::UpdateOnly);
}

void ShotUploads::onShotEdited(qint64 shotId, bool success) {
    if (shotId <= 0) return;
    // A save the holding page announced waits for release, unless an upload
    // already took it. Counted even when it failed, so the count stays matched.
    const auto held = m_held.find(shotId);
    const bool pageSave = held != m_held.end() && held->pending > 0;
    bool takenByUpload = false;
    if (pageSave) {
        --held->pending;
        if (held->uploaded > 0) {
            --held->uploaded;
            takenByUpload = true;
        }
    }
    if (!success || takenByUpload) return;
    noteEdited(shotId);
    if (pageSave)
        held->edited = true;
    else if (m_settings->autoUpdate())
        enqueue(shotId, Send::UpdateOnly);
}

// An edit made on Visualizer reaches the other destinations as any edit does.
// Visualizer itself is sent nothing: a pull leaves no field dirty.
void ShotUploads::onShotPulled(qint64 shotId, const QVariantMap& previous, const QVariantMap& written) {
    Q_UNUSED(previous);
    if (shotId <= 0 || written.isEmpty()) return;
    noteEdited(shotId);
    const auto held = m_held.find(shotId);
    if (held != m_held.end())
        held->edited = true;
    else if (m_settings->autoUpdate())
        enqueue(shotId, Send::UpdateOnly);
}

void ShotUploads::noteEdited(qint64 shotId) {
    m_storage->requestClearUploadRejections(shotId);
    for (ShotUploadDestination* destination : std::as_const(m_destinations))
        destination->noteEdited(shotId);
}

void ShotUploads::enqueue(qint64 shotId, Send how) {
    for (ShotUploadDestination* destination : std::as_const(m_destinations))
        if (destination->isActive()) enqueueTo(destination, shotId, how);
}

void ShotUploads::enqueueTo(ShotUploadDestination* destination, qint64 shotId, Send how) {
    QList<Job>& queue = m_queues[destination];
    auto queued = std::find_if(queue.begin(), queue.end(), [shotId](const Job& job) { return job.shotId == shotId; });
    if (queued == queue.end())
        queue.append({shotId, how});
    else if (how == Send::UploadOrUpdate)
        queued->how = how;
    pump(destination);
}

// The run's lines go under the destination's own marker, so [Decent] or
// [Visualizer] at INFO tells the whole story of its uploads.
static void logUploads(const QString& destination, const QString& text, bool warn = false) {
    if (destination == QLatin1String("decent")) {
        if (warn) DIAG_WARN(DECENT, "ShotUploads") << text;
        else DIAG_INFO(DECENT, "ShotUploads") << text;
    } else {
        if (warn) DIAG_WARN(VISUALIZER, "ShotUploads") << text;
        else DIAG_INFO(VISUALIZER, "ShotUploads") << text;
    }
}

void ShotUploads::pump(ShotUploadDestination* destination) {
    if (m_current.value(destination).shotId != 0) return;
    if (!destination->isActive()) {
        dropQueue(destination, QStringLiteral("switched off or signed out"));
        return;
    }
    QList<Job>& queue = m_queues[destination];
    if (queue.isEmpty()) return;
    const Job job = queue.takeFirst();
    m_current.insert(destination, Current{job.shotId, job.how, 1, {}});
    destination->attemptSavedShot(job.shotId, job.how);
}

void ShotUploads::dropQueue(ShotUploadDestination* destination, const QString& why) {
    QList<Job>& queue = m_queues[destination];
    if (!queue.isEmpty())
        logUploads(destination->name(), QStringLiteral("%1 queued shot(s) not sent (%2); Upload missing shots offers them")
                                            .arg(queue.size()).arg(why));
    queue.clear();
    if (m_runs.contains(destination)) endRun(destination, why);
}

void ShotUploads::onAttempt(ShotUploadDestination* destination, Attempt attempt) {
    const auto current = m_current.find(destination);
    if (current == m_current.end() || current->shotId == 0) return;
    if (attempt.outcome == Outcome::Transient && current->attempt < kAttempts) {
        const int delayMs = m_retryDelayMs * current->attempt;
        ++current->attempt;
        current->last = attempt;
        QTimer::singleShot(delayMs, this, [this, destination]() {
            const Current retry = m_current.value(destination);
            if (retry.shotId == 0) return;
            // Switched off during the wait: the send ends on the attempt that failed.
            if (!destination->isActive())
                finishSend(destination, retry.last);
            else
                destination->attemptSavedShot(retry.shotId, retry.how);
        });
        return;
    }
    finishSend(destination, attempt);
}

void ShotUploads::finishSend(ShotUploadDestination* destination, Attempt last) {
    const qint64 shotId = m_current.take(destination).shotId;
    using Record = ShotHistoryStorage::UploadOutcome;
    switch (last.outcome) {
    case Outcome::Sent:
        m_storage->requestRecordUploadOutcome(shotId, destination->name(), Record::Sent);
        break;
    case Outcome::Transient:
        m_storage->requestRecordUploadOutcome(shotId, destination->name(), Record::Failed, last.httpStatus);
        break;
    case Outcome::Rejected:
        m_storage->requestRecordUploadOutcome(shotId, destination->name(), Record::Rejected, last.httpStatus);
        break;
    case Outcome::NothingToSend:
    case Outcome::AuthFailed:
    case Outcome::AccountRefused:
        break;
    }
    destination->sendFinished(shotId, last);
    const bool refused = last.outcome == Outcome::AuthFailed || last.outcome == Outcome::AccountRefused;
    const auto run = m_runs.find(destination);
    if (run != m_runs.end() && run->outstanding.removeIf([shotId](const Job& job) { return job.shotId == shotId; }) > 0) {
        ++run->done;
        if (last.outcome == Outcome::Sent) ++run->sent;
        else if (last.outcome != Outcome::NothingToSend) ++run->failed;
        emit missingChanged();
        if (last.outcome == Outcome::Transient && last.httpStatus == 429)
            pauseForRateLimit(destination, *run);
        else if (!refused && run->outstanding.isEmpty())
            nextBatch(destination);
    }
    // The account needs attention: nothing queued for it can succeed until then,
    // whichever send was refused.
    if (refused)
        dropQueue(destination, last.outcome == Outcome::AuthFailed ? QStringLiteral("the account needs signing in again")
                                                                    : QStringLiteral("the account refused the upload"));
    QMetaObject::invokeMethod(this, [this, destination]() { pump(destination); }, Qt::QueuedConnection);
}

ShotUploadDestination* ShotUploads::destinationNamed(const QString& name) const {
    for (ShotUploadDestination* destination : m_destinations)
        if (destination->name() == name) return destination;
    return nullptr;
}

bool ShotUploads::uploadMissing(const QString& name) {
    ShotUploadDestination* destination = destinationNamed(name);
    if (!destination || !destination->isActive() || m_runs.contains(destination)) return false;
    m_settings->setMissingRunStartedAt(name, QDateTime::currentSecsSinceEpoch());
    startRun(destination, 0);
    return true;
}

void ShotUploads::resumeMissingRuns() {
    if (!m_storage->isReady()) return;   // MainController calls again on readyChanged
    for (ShotUploadDestination* destination : std::as_const(m_destinations)) {
        const qint64 startedAt = m_settings->missingRunStartedAt(destination->name());
        if (startedAt > 0 && destination->isActive() && !m_runs.contains(destination))
            startRun(destination, startedAt);
    }
}

void ShotUploads::setMachineOperating(bool operating) {
    m_machineOperating = operating;
    if (operating) return;
    for (ShotUploadDestination* destination : m_runs.keys()) nextBatch(destination);
}

QVariantMap ShotUploads::missing() const {
    QVariantMap all;
    for (ShotUploadDestination* destination : m_destinations) {
        const auto count = m_counts.constFind(destination->name());
        const auto run = m_runs.constFind(destination);
        if (count == m_counts.constEnd() && run == m_runs.constEnd()) continue;
        QVariantMap entry;
        if (count != m_counts.constEnd()) {
            entry[QStringLiteral("count")] = int(count->shotIds.size());
            entry[QStringLiteral("failed")] = count->failed;
            entry[QStringLiteral("unsentEdits")] = count->unsentEdits;
        }
        entry[QStringLiteral("running")] = run != m_runs.constEnd();
        if (run != m_runs.constEnd()) {
            entry[QStringLiteral("done")] = run->done;
            entry[QStringLiteral("total")] = run->total;
            if (run->resumeAtMs > 0) entry[QStringLiteral("resumeAtMs")] = run->resumeAtMs;
        }
        all[destination->name()] = entry;
    }
    return all;
}

void ShotUploads::refreshMissing() {
    if (m_counting) {
        m_countAgain = true;
        return;
    }
    if (!m_storage->isReady()) return;   // readyChanged counts once the history is readable
    struct Target { QString name, held, unsent; };
    QList<Target> targets;
    for (const ShotUploadDestination* destination : std::as_const(m_destinations))
        if (destination->isActive())
            targets.append({destination->name(), destination->heldCondition(), destination->unsentEditCondition()});
    m_counting = true;
    const QString dbPath = m_storage->databasePath();
    const double minDuration = m_settings->minDuration();
    QPointer<ShotUploads> self(this);
    m_storage->runAfterQueuedWrites([self, dbPath, minDuration, targets]() {
        QHash<QString, Missing> counts;
        withTempDb(dbPath, "missing_count", [&](QSqlDatabase& db) {
            for (const Target& t : targets) counts.insert(t.name, findMissingFor(db, t.name, t.held, t.unsent, minDuration, 0));
        });
        QMetaObject::invokeMethod(qApp, [self, counts, targets]() {
            if (!self) return;
            self->m_counting = false;
            // A count that could not be read keeps the last one rather than reading as nothing missing.
            QHash<QString, Missing> next;
            for (const Target& t : targets) {
                const Missing counted = counts.value(t.name);
                if (counted.readable)
                    next.insert(t.name, counted);
                else if (self->m_counts.contains(t.name))
                    next.insert(t.name, self->m_counts.value(t.name));
            }
            self->m_counts = next;
            emit self->missingChanged();
            if (self->m_countAgain) {
                self->m_countAgain = false;
                self->refreshMissing();
            }
        }, Qt::QueuedConnection);
    });
}

void ShotUploads::startRun(ShotUploadDestination* destination, qint64 skipFailedSince) {
    Run run;
    run.id = ++m_nextRunId;
    m_runs.insert(destination, run);
    emit missingChanged();
    const QString dbPath = m_storage->databasePath();
    const double minDuration = m_settings->minDuration();
    const QString name = destination->name(), held = destination->heldCondition(),
                  unsent = destination->unsentEditCondition();
    QPointer<ShotUploads> self(this);
    // After every queued write, so outcomes just recorded are read.
    m_storage->runAfterQueuedWrites([self, destination, dbPath, minDuration, skipFailedSince, name, held, unsent]() {
        Missing missing;
        withTempDb(dbPath, "missing_shots", [&](QSqlDatabase& db) {
            missing = findMissingFor(db, name, held, unsent, minDuration, skipFailedSince);
        });
        QMetaObject::invokeMethod(qApp, [self, destination, missing, name, skipFailedSince]() {
            if (!self) return;
            const auto run = self->m_runs.find(destination);
            if (run == self->m_runs.end()) return;
            if (!missing.readable) {
                // Kept for the next press or launch: one bad read must not lose the run.
                self->m_runs.erase(run);
                emit self->missingChanged();
                logUploads(name, QStringLiteral("Upload missing shots not started: the history could not be read"), true);
                return;
            }
            run->selecting = false;
            run->pending = missing.shotIds;
            run->unsentLeft = missing.unsentEdits;
            run->total = int(missing.shotIds.size());
            emit self->missingChanged();
            logUploads(name, QStringLiteral("Upload missing shots %1: %2 shot(s), %3 of them unsent edits")
                                 .arg(skipFailedSince > 0 ? QStringLiteral("resumed after a restart") : QStringLiteral("started"))
                                 .arg(run->total).arg(missing.unsentEdits));
            self->nextBatch(destination);
        }, Qt::QueuedConnection);
    });
}

void ShotUploads::nextBatch(ShotUploadDestination* destination) {
    const auto run = m_runs.find(destination);
    if (run == m_runs.end() || run->selecting || run->waiting || !run->outstanding.isEmpty()) return;
    if (!destination->isActive()) {
        endRun(destination, QStringLiteral("switched off or signed out"));
        return;
    }
    if (run->pending.isEmpty()) {
        endRun(destination, QStringLiteral("finished"));
        return;
    }
    if (m_machineOperating) {   // setMachineOperating(false) comes back here
        if (!run->paused) logUploads(destination->name(), QStringLiteral("Upload missing shots paused while the machine is operating"));
        run->paused = true;
        return;
    }
    if (run->sinceBatch.isValid()) {
        const qint64 wait = m_batchSpacingMs - run->sinceBatch.elapsed();
        if (wait > 0) {
            waitBeforeNextBatch(destination, *run, wait);
            return;
        }
    }
    run->paused = false;
    run->sinceBatch.start();
    const qint64 sending = m_current.value(destination).shotId;
    for (int i = 0; i < kBatchSize && !run->pending.isEmpty(); ++i) {
        const qint64 shotId = run->pending.takeFirst();
        // Unsent edits come first, and go as updates: only their edited fields, so
        // edits made on the destination's side are not overwritten.
        const Send how = run->unsentLeft > 0 ? Send::UpdateOnly : Send::UploadOrUpdate;
        if (run->unsentLeft > 0) --run->unsentLeft;
        run->outstanding.append({shotId, how});
        // Already being sent: its finish counts for the run, and a second send would repeat it.
        if (shotId != sending) enqueueTo(destination, shotId, how);
    }
}

void ShotUploads::endRun(ShotUploadDestination* destination, const QString& why) {
    const Run run = m_runs.take(destination);
    m_settings->setMissingRunStartedAt(destination->name(), 0);
    emit missingChanged();
    QString tally = QStringLiteral("%1 of %2 sent").arg(run.sent).arg(run.total);
    if (run.failed > 0) tally += QStringLiteral(", %1 failed").arg(run.failed);
    if (const int skipped = run.done - run.sent - run.failed; skipped > 0)
        tally += QStringLiteral(", %1 had nothing to send").arg(skipped);
    logUploads(destination->name(), QStringLiteral("Upload missing shots ended (%1): %2").arg(why, tally));
}

void ShotUploads::waitBeforeNextBatch(ShotUploadDestination* destination, Run& run, qint64 ms) {
    run.waiting = true;
    QTimer::singleShot(int(ms), this, [this, destination, id = run.id]() {
        const auto waited = m_runs.find(destination);
        if (waited == m_runs.end() || waited->id != id) return;
        waited->waiting = false;
        if (waited->resumeAtMs > 0) {
            waited->resumeAtMs = 0;
            emit missingChanged();
            logUploads(destination->name(), QStringLiteral("Upload missing shots continuing after the rate-limit pause"));
        }
        nextBatch(destination);
    });
}

void ShotUploads::pauseForRateLimit(ShotUploadDestination* destination, Run& run) {
    if (run.outstanding.isEmpty() && run.pending.isEmpty()) {   // nothing left to wait for
        nextBatch(destination);
        return;
    }
    // The batch's untried shots leave the queue; nothing else queued does.
    QList<Job>& queue = m_queues[destination];
    queue.removeIf([&run](const Job& queued) {
        return std::any_of(run.outstanding.cbegin(), run.outstanding.cend(),
                           [&queued](const Job& job) { return job.shotId == queued.shotId; });
    });
    // The batch was taken from the front in order, unsent edits first, so it goes back the same way.
    for (auto job = run.outstanding.crbegin(); job != run.outstanding.crend(); ++job) {
        run.pending.prepend(job->shotId);
        if (job->how == Send::UpdateOnly) ++run.unsentLeft;
    }
    const qsizetype returned = run.outstanding.size();
    run.outstanding.clear();
    run.resumeAtMs = QDateTime::currentMSecsSinceEpoch() + m_rateLimitPauseMs;
    waitBeforeNextBatch(destination, run, m_rateLimitPauseMs);
    emit missingChanged();
    logUploads(destination->name(), QStringLiteral("Upload missing shots paused for %1 min: the server asked to slow down "
                                                   "(HTTP 429); the batch's %2 other shot(s) go first when it continues")
                                        .arg(qMax(1, m_rateLimitPauseMs / 60000)).arg(returned));
}

ShotUploads::Missing ShotUploads::findMissing(QSqlDatabase& db, const ShotUploadDestination& destination,
                                              double minDurationSec, qint64 skipFailedSince) {
    return findMissingFor(db, destination.name(), destination.heldCondition(), destination.unsentEditCondition(),
                          minDurationSec, skipFailedSince);
}

ShotUploads::Missing ShotUploads::findMissingFor(QSqlDatabase& db, const QString& name, const QString& held,
                                                 const QString& unsent, double minDurationSec, qint64 skipFailedSince) {
    Missing missing;
    // On the DB worker. Measured 2026-10-05 (MacBook Pro, warm cache, rows
    // rewritten so these late-added columns follow profile_json/debug_log):
    // 1,217 shots / 21 MB median 1.3 ms, worst 4.3 ms; 4,962 shots / 57 MB
    // median 5.0 ms, worst 7.2 ms. It grows with the history's bytes.
    // profile_json is read only where beverage_type is empty (uploadBeverageType's
    // fallback); migration 6 and the import fill that column.
    QSqlQuery q(db);
    const QString sql = QStringLiteral(
        "SELECT id, beverage_type, duration_seconds, "
        "CASE WHEN COALESCE(beverage_type, '') = '' THEN profile_json END, "
        "((%3) AND (%2)) AS unsent, %1_failed_at IS NOT NULL "
        "FROM shots WHERE %1_rejected_at IS NULL AND (NOT (%3) OR (%2)) "
        "AND (%1_failed_at IS NULL OR %1_failed_at < :since) "
        "ORDER BY unsent DESC, timestamp DESC, id DESC").arg(name, unsent, held);
    const bool prepared = q.prepare(sql);
    q.bindValue(":since", skipFailedSince > 0 ? skipFailedSince : std::numeric_limits<qint64>::max());
    if (!prepared || !q.exec()) {
        logUploads(name, QStringLiteral("missing shots could not be read - %1").arg(q.lastError().text()), true);
        return missing;
    }
    while (q.next()) {
        ShotProjection shot;
        shot.beverageType = q.value(1).toString();
        shot.durationSec = q.value(2).toDouble();
        shot.profileJson = q.value(3).toString();
        if (uploadIneligibility(shot, minDurationSec) != UploadIneligible::None) continue;
        missing.shotIds.append(q.value(0).toLongLong());
        if (q.value(4).toBool())
            ++missing.unsentEdits;
        else if (q.value(5).toBool())
            ++missing.failed;
    }
    missing.readable = true;
    return missing;
}
