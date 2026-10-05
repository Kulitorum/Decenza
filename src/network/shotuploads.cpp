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

void ShotUploads::pump(ShotUploadDestination* destination) {
    if (m_current.value(destination).shotId != 0) return;
    QList<Job>& queue = m_queues[destination];
    // Switched off or signed out since the shot was queued: drop its queue, and its run.
    if (!destination->isActive()) {
        queue.clear();
        if (m_runs.contains(destination)) endRun(destination);
        return;
    }
    if (queue.isEmpty()) return;
    const Job job = queue.takeFirst();
    m_current.insert(destination, Current{job.shotId, job.how, 1});
    destination->attemptSavedShot(job.shotId, job.how);
}

void ShotUploads::onAttempt(ShotUploadDestination* destination, Attempt attempt) {
    const auto current = m_current.find(destination);
    if (current == m_current.end() || current->shotId == 0) return;
    if (attempt.outcome == Outcome::Transient && current->attempt < kAttempts) {
        const int delayMs = m_retryDelayMs * current->attempt;
        ++current->attempt;
        QTimer::singleShot(delayMs, this, [this, destination]() {
            const Current retry = m_current.value(destination);
            // The destination reports a sign-out or switch-off during the wait itself.
            if (retry.shotId != 0) destination->attemptSavedShot(retry.shotId, retry.how);
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
    // The account needs attention: nothing queued for it can succeed until then.
    if (last.outcome == Outcome::AuthFailed || last.outcome == Outcome::AccountRefused)
        m_queues[destination].clear();
    const auto run = m_runs.find(destination);
    if (run != m_runs.end() && run->outstanding.remove(shotId)) {
        ++run->done;
        emit missingChanged();
        if (last.outcome == Outcome::AuthFailed || last.outcome == Outcome::AccountRefused)
            endRun(destination);
        else if (run->outstanding.isEmpty())
            nextBatch(destination);
    }
    QMetaObject::invokeMethod(this, [this, destination]() { pump(destination); }, Qt::QueuedConnection);
}

ShotUploadDestination* ShotUploads::destinationNamed(const QString& name) const {
    for (ShotUploadDestination* destination : m_destinations)
        if (destination->name() == name) return destination;
    return nullptr;
}

void ShotUploads::uploadMissing(const QString& name) {
    ShotUploadDestination* destination = destinationNamed(name);
    if (!destination || !destination->isActive() || m_runs.contains(destination)) return;
    m_settings->setMissingRunStartedAt(name, QDateTime::currentSecsSinceEpoch());
    startRun(destination, 0);
}

void ShotUploads::resumeMissingRuns() {
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
        QMetaObject::invokeMethod(qApp, [self, counts]() {
            if (!self) return;
            self->m_counting = false;
            self->m_counts = counts;
            emit self->missingChanged();
            if (self->m_countAgain) {
                self->m_countAgain = false;
                self->refreshMissing();
            }
        }, Qt::QueuedConnection);
    });
}

void ShotUploads::startRun(ShotUploadDestination* destination, qint64 skipFailedSince) {
    m_runs.insert(destination, Run{});
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
        QMetaObject::invokeMethod(qApp, [self, destination, missing]() {
            if (!self) return;
            const auto run = self->m_runs.find(destination);
            if (run == self->m_runs.end()) return;
            run->selecting = false;
            run->pending = missing.shotIds;
            run->total = int(missing.shotIds.size());
            emit self->missingChanged();
            self->nextBatch(destination);
        }, Qt::QueuedConnection);
    });
}

void ShotUploads::nextBatch(ShotUploadDestination* destination) {
    const auto run = m_runs.find(destination);
    if (run == m_runs.end() || run->selecting || run->waiting || !run->outstanding.isEmpty()) return;
    if (run->pending.isEmpty() || !destination->isActive()) {
        endRun(destination);
        return;
    }
    if (m_machineOperating) return;   // setMachineOperating(false) comes back here
    if (run->sinceBatch.isValid()) {
        const qint64 wait = m_batchSpacingMs - run->sinceBatch.elapsed();
        if (wait > 0) {
            run->waiting = true;
            QTimer::singleShot(int(wait), this, [this, destination]() {
                const auto waited = m_runs.find(destination);
                if (waited == m_runs.end()) return;
                waited->waiting = false;
                nextBatch(destination);
            });
            return;
        }
    }
    run->sinceBatch.start();
    QList<qint64> batch;
    while (batch.size() < kBatchSize && !run->pending.isEmpty()) batch.append(run->pending.takeFirst());
    for (qint64 shotId : std::as_const(batch)) run->outstanding.insert(shotId);
    // Enqueued after `outstanding` holds them all: a send can finish within enqueueTo.
    for (qint64 shotId : std::as_const(batch)) enqueueTo(destination, shotId, Send::UploadOrUpdate);
}

void ShotUploads::endRun(ShotUploadDestination* destination) {
    m_runs.remove(destination);
    m_settings->setMissingRunStartedAt(destination->name(), 0);
    emit missingChanged();
}

ShotUploads::Missing ShotUploads::findMissing(QSqlDatabase& db, const ShotUploadDestination& destination,
                                              double minDurationSec, qint64 skipFailedSince) {
    return findMissingFor(db, destination.name(), destination.heldCondition(), destination.unsentEditCondition(),
                          minDurationSec, skipFailedSince);
}

ShotUploads::Missing ShotUploads::findMissingFor(QSqlDatabase& db, const QString& name, const QString& held,
                                                 const QString& unsent, double minDurationSec, qint64 skipFailedSince) {
    Missing missing;
    // Measured 2026-10-05 on a 1,217-shot, 21 MB history (MacBook Pro, warm
    // cache): median 1.4 ms, worst 2.9 ms. Large columns stay in overflow pages
    // this scan never reads, so it does not grow with the blobs.
    // The profile snapshot is read only where the beverage-type column is empty,
    // which uploadBeverageType falls back to; the import fills that column.
    QSqlQuery q(db);
    const QString sql = QStringLiteral(
        "SELECT id, beverage_type, duration_seconds, "
        "CASE WHEN COALESCE(beverage_type, '') = '' THEN profile_json END, "
        "(%2) AS unsent, %1_failed_at IS NOT NULL "
        "FROM shots WHERE %1_rejected_at IS NULL AND (NOT (%3) OR (%2)) "
        "AND (%1_failed_at IS NULL OR %1_failed_at < :since) "
        "ORDER BY unsent DESC, timestamp DESC, id DESC").arg(name, unsent, held);
    const bool prepared = q.prepare(sql);
    q.bindValue(":since", skipFailedSince > 0 ? skipFailedSince : std::numeric_limits<qint64>::max());
    if (!prepared || !q.exec()) {
        DIAG_WARN(STORAGE, "ShotUploads") << "missing shots for" << name
                                          << "could not be read -" << q.lastError().text();
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
    return missing;
}

