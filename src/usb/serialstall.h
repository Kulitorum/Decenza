#pragma once

#include <QtGlobal>

// The stall decision for the USB serial link, as a self-contained detector.
//
// Split out from SerialTransport so it can be tested at all. The transport
// itself cannot be: open() needs a real port, write() early-returns unless
// m_connected, and the threshold is a minute — so the logic that decides whether
// a silent DE1 gets a warning would otherwise ship with no coverage. A
// silent-failure detector that fails silently is worse than none, because the
// absence of its warning reads as "the link is fine".
//
// A free `shouldWarn(bool, bool, qint64, qint64)` function preceded this and had
// exactly that problem: all four parameters were mutually convertible, so
// `shouldWarn(livenessValid, alreadyWarned, thresholdMs, elapsedMs)` — every
// argument transposed — compiled silently. The dangerous case: swap the two
// durations and INBOUND_STALE_MS (60000) becomes `elapsedMs`, so
// `60000 > realElapsed` is true from the moment the port opens and it warns on
// the FIRST write, every time. Encapsulating the state removes the call site
// that could get the order wrong — there is only ever one qint64 argument now,
// `nowMs`, and nothing to transpose it with.
//
// See the m_inboundLiveness block this replaced in serialtransport.h/.cpp for
// why the threshold is what it is; the reasoning didn't change, only where the
// state lives.
namespace SerialStall {

class Detector {
public:
    explicit Detector(qint64 thresholdMs) : m_thresholdMs(thresholdMs) {}

    // Port opened / became live. Arms from THIS moment, not from the last
    // inbound line (there may be none yet) — so a machine that NEVER starts
    // notifying is caught, not just one that stops. That is the case with no
    // other symptom at all: writes succeed, the port stays open, and nothing
    // ever comes back.
    void arm(qint64 nowMs) {
        m_lastInboundMs = nowMs;
        m_armed = true;
        m_warned = false;
    }

    // Port closed. A closed port has no stream to be stale; without this a
    // stale timestamp from before the close would read as an infinite stall
    // the moment the port reopens.
    void disarm() {
        m_armed = false;
        m_warned = false;
    }

    // Any complete inbound line proves the stream is alive. Returns true
    // exactly once — on the line that ends a stall the caller already warned
    // about — so the caller can log "traffic resumed" without tracking the
    // latch itself.
    bool noteInbound(qint64 nowMs) {
        m_lastInboundMs = nowMs;
        if (m_warned) {
            m_warned = false;
            return true;
        }
        return false;
    }

    // True the first time a write finds the stream stale past the threshold.
    // Latches internally — one warning per stall episode, not one per write,
    // since writes are frequent and an unlatched check is the flat-repeat
    // noise this whole change exists to remove. Call on every write; a call
    // before arm() or after disarm() never warns.
    bool shouldWarn(qint64 nowMs) {
        if (!m_armed || m_warned) return false;
        if (nowMs - m_lastInboundMs > m_thresholdMs) {
            m_warned = true;
            return true;
        }
        return false;
    }

    // How long since the last inbound line (or since arm(), if none yet). For
    // the warning message; has no effect on the latch.
    qint64 staleForMs(qint64 nowMs) const { return nowMs - m_lastInboundMs; }

private:
    qint64 m_thresholdMs;
    qint64 m_lastInboundMs = 0;
    bool m_armed = false;
    bool m_warned = false;
};

} // namespace SerialStall
