#pragma once

#include <cstdint>

// Pure, Qt-free decision logic for the scale connection-priority backoff
// (dual-HIGH BLE contention, #1093/#1176). Owned by QtScaleBleTransport,
// which forwards detection inputs and supplies a monotonic millisecond clock
// (the detector holds no clock, so it is fully deterministic under test).
//
// Lifecycle within one transport object (which persists across the
// backoff-induced reconnect):
//   armWindow()  — scale connected and requested HIGH; start watching.
//   disarm()     — scale started at BALANCED, or backed off; not watching.
//   onDe1Fault() — primary signal; ≥ kDe1FaultThreshold within
//                   kDe1FaultWindowMs of arming ⇒ trigger backoff.
//   onScaleStall() — backstop; any in-shot stall while armed ⇒ trigger.
// fire() latches skip-HIGH + backed-off so it triggers at most once per
// session and never re-arms (no reconnect loop).
class BlePriorityDetector {
public:
    static constexpr int kDe1FaultThreshold = 2;         // ≥2 DE1 faults in-window (#1176 ConnectionError pattern)
    static constexpr int64_t kDe1FaultWindowMs = 20000;  // covers #1093 ~13s and #1176 ~0.8–15s post scale-HIGH

    // Scale connected and requested HIGH: arm the watch window. No-op (stays
    // disarmed) if a prior backoff already latched skip-HIGH this session.
    void armWindow(int64_t nowMs) {
        if (m_skipHighPriority) { m_armed = false; return; }
        m_armed = true;
        m_windowStartMs = nowMs;
        m_de1FaultCount = 0;
    }

    // Scale started at BALANCED (skip flag set) or backed off — not watching.
    void disarm() { m_armed = false; m_de1FaultCount = 0; }

    // Primary signal. Returns true exactly once, when the fault count crosses
    // the threshold inside the window — the caller should then back off.
    bool onDe1Fault(int64_t nowMs) {
        if (!m_armed || m_backoffTriggered) return false;
        if (nowMs - m_windowStartMs > kDe1FaultWindowMs) {
            m_armed = false;  // window elapsed cleanly — device looks capable
            return false;
        }
        if (++m_de1FaultCount < kDe1FaultThreshold) return false;
        return fire();
    }

    // Backstop: an in-shot scale-feed stall while armed (covers sessions with
    // no early DE1 cluster — the actual #1176 shot-1151 case).
    bool onScaleStall() {
        if (!m_armed || m_backoffTriggered) return false;
        return fire();
    }

    // Session skip-HIGH flag. Persisted in-memory on the (long-lived)
    // transport so it survives the backoff-induced reconnect.
    void setSkipHighPriority(bool skip) {
        m_skipHighPriority = skip;
        if (skip) disarm();
    }

    bool skipHighPriority() const { return m_skipHighPriority; }
    bool backoffTriggered() const { return m_backoffTriggered; }
    bool armed() const { return m_armed; }
    int de1FaultCount() const { return m_de1FaultCount; }

private:
    bool fire() {
        m_backoffTriggered = true;
        m_skipHighPriority = true;  // next (re)connect skips HIGH ⇒ BALANCED
        m_armed = false;            // never re-arms ⇒ no reconnect loop
        return true;
    }

    bool m_skipHighPriority = false;
    bool m_backoffTriggered = false;
    bool m_armed = false;
    int m_de1FaultCount = 0;
    int64_t m_windowStartMs = 0;
};
