#pragma once

#include <QHash>
#include <QVariantMap>
#include <optional>

// Joins two independently completing events by espresso operation, never recency.
class ShotWaterJoin {
public:
    struct Write { qint64 shotId; QString uuid; QVariantMap result; };
    void expect(qint64 operation) { if (operation > 0) m_pending.insert(operation, {}); }
    std::optional<Write> saved(qint64 operation, qint64 shotId, const QString& uuid) {
        auto it = m_pending.find(operation);
        if (it == m_pending.end()) return std::nullopt;
        if (shotId <= 0) { m_pending.erase(it); return std::nullopt; }
        it->shotId = shotId;
        it->uuid = uuid;
        return takeReady(operation);
    }
    std::optional<Write> result(qint64 operation, const QVariantMap& result) {
        auto it = m_pending.find(operation);
        if (it == m_pending.end()) return std::nullopt;
        it->result = result;
        return takeReady(operation);
    }
    QVariantMap result(qint64 operation) const { return m_pending.value(operation).result; }
    void discard(qint64 operation) { m_pending.remove(operation); }
private:
    struct Pending { qint64 shotId = 0; QString uuid; QVariantMap result; };
    QHash<qint64, Pending> m_pending;
    std::optional<Write> takeReady(qint64 operation) {
        const auto it = m_pending.find(operation);
        if (it == m_pending.end() || it->shotId <= 0 || it->uuid.isEmpty() || it->result.isEmpty())
            return std::nullopt;
        Write write{it->shotId, it->uuid, it->result};
        m_pending.erase(it);
        return write;
    }
};
