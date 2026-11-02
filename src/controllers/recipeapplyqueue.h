#pragma once

#include <QObject>
#include <functional>

// Dose/yield writes and terminal replies share an activation generation.
// A superseded activation still completes, but cannot alter its successor.
class RecipeApplyQueue {
public:
    quint64 begin() { m_pending = true; return ++m_generation; }
    bool pending() const { return m_pending; }

    void writeLater(QObject* owner, std::function<void()> write) {
        const auto generation = m_generation;
        QMetaObject::invokeMethod(owner, [this, generation, operation = std::move(write)] {
            if (generation == m_generation) operation();
        }, Qt::QueuedConnection);
    }

    void finishLater(QObject* owner, quint64 generation, std::function<void(bool)> complete) {
        QMetaObject::invokeMethod(owner, [this, generation, completion = std::move(complete)] {
            const bool current = generation == m_generation;
            if (current) m_pending = false;
            completion(current);
        }, Qt::QueuedConnection);
    }

private:
    quint64 m_generation = 0;
    bool m_pending = false;
};
