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
    // The page's edits so far go with this upload.
    if (m_held.contains(shotId)) m_held[shotId] = false;
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

QStringList ShotUploads::autoUpdateDestinations() const {
    return m_settings->autoUpdate() ? activeDestinations() : QStringList();
}

void ShotUploads::holdUpdates(qint64 shotId) {
    if (shotId > 0 && !m_held.contains(shotId)) m_held.insert(shotId, false);
}

void ShotUploads::releaseUpdates(qint64 shotId) {
    const auto it = m_held.constFind(shotId);
    if (it == m_held.constEnd()) return;
    const bool edited = it.value();
    m_held.erase(it);
    if (edited && m_settings->autoUpdate()) enqueue(shotId, Send::UpdateOnly);
}

void ShotUploads::onShotEdited(qint64 shotId, bool success) {
    if (!success || shotId <= 0) return;
    for (ShotUploadDestination* destination : std::as_const(m_destinations))
        destination->noteEdited(shotId);
    const auto held = m_held.find(shotId);
    if (held != m_held.end()) {
        held.value() = true;
        return;
    }
    if (m_settings->autoUpdate()) enqueue(shotId, Send::UpdateOnly);
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
