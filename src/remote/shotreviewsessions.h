#pragma once

#include <QHash>
#include <QList>
#include <QMap>
#include <QString>
#include <QUuid>

#include <algorithm>

// Host-owned upload leases and the one refractometer result destination.
// A view's lease is independent of every other view, including views of the
// same shot. Only transport admission can open a client again after closure.
class ShotReviewSessions final
{
public:
    enum class Hold { Rejected, Existing, FirstForShot, AdditionalForShot };
    static constexpr qsizetype MaximumLeases = 128;
    static constexpr qsizetype MaximumLeasesPerClient = 16;

    void openClient(const QString& clientId, bool native = false)
    {
        if (!validId(clientId))
            return;
        m_clients.insert(clientId, native || m_clients.value(clientId));
        selectOwner();
    }

    QList<qint64> closeClient(const QString& clientId)
    {
        QList<qint64> releasedShots;
        if (!m_clients.contains(clientId))
            return releasedShots;
        const auto reviewIds = m_reviews.keys();
        for (const QString& reviewId : reviewIds) {
            if (m_reviews.value(reviewId).clientId != clientId)
                continue;
            const qint64 shotId = release(clientId, reviewId);
            if (shotId > 0)
                releasedShots.append(shotId);
        }
        m_clients.remove(clientId);
        selectOwner();
        std::sort(releasedShots.begin(), releasedShots.end());
        return releasedShots;
    }

    Hold hold(const QString& clientId, const QString& reviewId, qint64 shotId)
    {
        if (!m_clients.contains(clientId) || !validId(reviewId) || shotId <= 0)
            return Hold::Rejected;
        const auto existing = m_reviews.constFind(reviewId);
        if (existing != m_reviews.cend())
            return existing->clientId == clientId && existing->shotId == shotId
                ? Hold::Existing : Hold::Rejected;
        if (m_measuring && reviewId == m_owner)
            return Hold::Rejected;
        if (m_reviews.size() >= MaximumLeases)
            return Hold::Rejected;
        qsizetype clientLeases = 0;
        for (const Review& review : m_reviews) {
            if (review.clientId == clientId)
                ++clientLeases;
        }
        if (clientLeases >= MaximumLeasesPerClient)
            return Hold::Rejected;
        m_reviews.insert(reviewId, {clientId, shotId, false});
        const int previous = m_shotLeases.value(shotId);
        m_shotLeases.insert(shotId, previous + 1);
        return previous == 0 ? Hold::FirstForShot : Hold::AdditionalForShot;
    }

    qint64 release(const QString& clientId, const QString& reviewId)
    {
        if (!owns(clientId, reviewId))
            return 0;
        const qint64 shotId = m_reviews.take(reviewId).shotId;
        const int remaining = m_shotLeases.value(shotId) - 1;
        if (remaining > 0)
            m_shotLeases.insert(shotId, remaining);
        else
            m_shotLeases.remove(shotId);
        selectOwner();
        return remaining == 0 ? shotId : 0;
    }

    bool owns(const QString& clientId, const QString& reviewId, qint64 shotId = 0) const
    {
        const auto review = m_reviews.constFind(reviewId);
        return m_clients.contains(clientId) && review != m_reviews.cend()
            && review->clientId == clientId && (shotId == 0 || review->shotId == shotId);
    }

    bool setActive(const QString& clientId, const QString& reviewId, bool active)
    {
        if (!owns(clientId, reviewId))
            return false;
        m_reviews[reviewId].active = active;
        selectOwner();
        return true;
    }

    bool beginMeasurement(const QString& clientId, const QString& reviewId)
    {
        if (m_measuring || !owns(clientId, reviewId) || !m_reviews.value(reviewId).active)
            return false;
        m_owner = reviewId;
        m_measuring = true;
        return true;
    }

    void noteMeasurementStarted()
    {
        selectOwner();
        m_measuring = true;
    }

    void finishMeasurement()
    {
        m_measuring = false;
        selectOwner();
    }

    QString measurementOwner() const { return m_owner; }
    bool measuring() const { return m_measuring; }
    bool hasActiveReviews() const
    {
        for (const Review& review : m_reviews) {
            if (review.active)
                return true;
        }
        return false;
    }

private:
    struct Review { QString clientId; qint64 shotId; bool active; };

    static bool validId(const QString& id)
    {
        const QUuid parsed(id);
        return !parsed.isNull() && parsed.toString(QUuid::WithoutBraces) == id;
    }

    void selectOwner()
    {
        // Keep even a departed owner's identity until the in-flight result has
        // been consumed; a newly visible review must not inherit that reading.
        if (m_measuring)
            return;
        const auto current = m_reviews.constFind(m_owner);
        const bool currentActive = current != m_reviews.cend() && current->active;
        if (currentActive && m_clients.value(current->clientId))
            return;
        for (auto it = m_reviews.cbegin(); it != m_reviews.cend(); ++it) {
            if (it->active && m_clients.value(it->clientId)) {
                m_owner = it.key();
                return;
            }
        }
        if (currentActive)
            return;
        m_owner.clear();
        for (auto it = m_reviews.cbegin(); it != m_reviews.cend(); ++it) {
            if (it->active) {
                m_owner = it.key();
                return;
            }
        }
    }

    QHash<QString, bool> m_clients;
    QMap<QString, Review> m_reviews;
    QHash<qint64, int> m_shotLeases;
    QString m_owner;
    bool m_measuring = false;
};
