#pragma once

#include <QString>
#include <utility>

// Small host-owned state machine for the one pending MCP confirmation. Keeping
// deadline checks and ID consumption together makes a delayed QTimer callback
// unable to let a late UI response win after the advertised deadline.
class GlobalSupportConfirmationState final
{
public:
    enum class Resolution { Stale, Accepted, TimedOut };

    bool begin(QString id, QString description, qint64 deadlineEpochMs,
               qint64 deadlineMonotonicMs)
    {
        if (id.isEmpty() || deadlineEpochMs <= 0 || deadlineMonotonicMs <= 0)
            return false;
        m_id = std::move(id);
        m_description = std::move(description);
        m_deadlineEpochMs = deadlineEpochMs;
        m_deadlineMonotonicMs = deadlineMonotonicMs;
        return true;
    }

    const QString& id() const { return m_id; }
    const QString& description() const { return m_description; }
    qint64 deadlineEpochMs() const { return m_deadlineEpochMs; }
    bool isCurrent(const QString& id) const { return !m_id.isEmpty() && id == m_id; }

    bool clearIfCurrent(const QString& id)
    {
        if (!isCurrent(id))
            return false;
        clear();
        return true;
    }

    Resolution resolve(const QString& id, qint64 nowMonotonicMs)
    {
        if (!isCurrent(id))
            return Resolution::Stale;
        const Resolution resolution = nowMonotonicMs >= m_deadlineMonotonicMs
            ? Resolution::TimedOut : Resolution::Accepted;
        clear();
        return resolution;
    }

    // Atomically consumes only an expired pending event. The returned ID is
    // the unique timeout winner; stale timers and duplicate calls return empty.
    QString takeExpired(qint64 nowMonotonicMs)
    {
        if (m_id.isEmpty() || nowMonotonicMs < m_deadlineMonotonicMs)
            return {};
        const QString expiredId = m_id;
        clear();
        return expiredId;
    }

    void clear()
    {
        m_id.clear();
        m_description.clear();
        m_deadlineEpochMs = 0;
        m_deadlineMonotonicMs = 0;
    }

private:
    QString m_id;
    QString m_description;
    qint64 m_deadlineEpochMs = 0;
    qint64 m_deadlineMonotonicMs = 0;
};
