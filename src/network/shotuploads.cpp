#include "shotuploads.h"

#include "core/settings_upload.h"
#include "history/shothistorystorage.h"

#include <QTimer>
#include <algorithm>

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
    for (ShotUploadDestination* destination : std::as_const(m_destinations)) {
        if (!destination->isActive()) continue;
        QList<Job>& queue = m_queues[destination];
        auto queued = std::find_if(queue.begin(), queue.end(), [shotId](const Job& job) { return job.shotId == shotId; });
        if (queued == queue.end())
            queue.append({shotId, how});
        else if (how == Send::UploadOrUpdate)
            queued->how = how;
        pump(destination);
    }
}

void ShotUploads::pump(ShotUploadDestination* destination) {
    if (m_current.value(destination).shotId != 0) return;
    QList<Job>& queue = m_queues[destination];
    // Switched off or signed out since the shot was queued: drop its queue.
    if (!destination->isActive()) {
        queue.clear();
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
    QMetaObject::invokeMethod(this, [this, destination]() { pump(destination); }, Qt::QueuedConnection);
}
