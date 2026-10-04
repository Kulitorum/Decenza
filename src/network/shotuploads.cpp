#include "shotuploads.h"

#include "core/settings_upload.h"
#include "history/shothistorystorage.h"

#include <algorithm>

ShotUploads::ShotUploads(SettingsUpload* settings, ShotHistoryStorage* storage,
                         QList<ShotUploadDestination*> destinations, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_destinations(std::move(destinations))
{
    for (ShotUploadDestination* destination : std::as_const(m_destinations)) {
        // Queued: the next shot starts after the finished one's signals have run.
        destination->setIdleCallback([this, destination]() {
            QMetaObject::invokeMethod(this, [this, destination]() { pump(destination); }, Qt::QueuedConnection);
        });
    }
    connect(storage, &ShotHistoryStorage::shotMetadataUpdated, this, &ShotUploads::onShotEdited);
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
    for (ShotUploadDestination* destination : std::as_const(m_destinations))
        destination->noteEdited(shotId);
    if (pageSave)
        held->edited = true;
    else if (m_settings->autoUpdate())
        enqueue(shotId, Send::UpdateOnly);
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
    if (destination->busy()) return;
    QList<Job>& queue = m_queues[destination];
    // Switched off or signed out since the shot was queued: drop its queue.
    if (!destination->isActive()) {
        queue.clear();
        return;
    }
    if (queue.isEmpty()) return;
    const Job job = queue.takeFirst();
    destination->sendSavedShot(job.shotId, job.how);
}
