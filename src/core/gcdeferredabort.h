#pragma once

#include "../ble/protocol/de1characteristics.h"
#include <QObject>
#include <QString>
#include <functional>
#include <utility>

namespace GcDeferredAbort {
// A cached state from a previous connection (or the initial default) is not
// evidence that an operation has finished.
inline bool confirmedIdle(bool connected, bool hasCurrentState, DE1::State state)
{
    return connected && hasCurrentState
        && (state == DE1::State::Idle || state == DE1::State::SchedIdle
            || state == DE1::State::Sleep);
}

inline bool shouldRequestState(bool connected, bool hasCurrentState, DE1::State state)
{
    return connected && (!hasCurrentState || confirmedIdle(connected, hasCurrentState, state));
}

// Kept independent of the machine and the fatal action so transitions can be
// exercised without starting a real machine or crashing the test process.
class Coordinator : public QObject {
public:
    Coordinator(std::function<bool()> isIdle, std::function<void()> requestState,
                std::function<void(const QString&)> abort,
                QObject* parent = nullptr)
        : QObject(parent), m_isIdle(std::move(isIdle)), m_requestState(std::move(requestState)),
          m_abort(std::move(abort)) {}

    void mismatch(const QString& evidence)
    {
        if (m_pending.isEmpty()) {
            m_pending = evidence;
            connectionChanged();
        }
    }
    void connectionChanged()
    {
        if (!m_pending.isEmpty()) m_requestState();
    }
    // Only a state report received after the mismatch can trigger the abort.
    void stateInfoReceived()
    {
        if (m_pending.isEmpty() || !m_isIdle()) return;
        const QString evidence = std::move(m_pending);
        m_pending.clear();
        m_abort(evidence);
    }

private:
    QString m_pending;
    std::function<bool()> m_isIdle;
    std::function<void()> m_requestState;
    std::function<void(const QString&)> m_abort;
};
}
