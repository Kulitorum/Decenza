#pragma once

#include <cstdint>

// Pure, Qt-free decision logic for the scale connection-priority backoff
// (dual-HIGH BLE contention, #1093/#1176). Owned by QtScaleBleTransport,
// which forwards detection inputs and supplies a monotonic millisecond clock
// (the detector holds no clock, so it is fully deterministic under test).
//
// Lifecycle within one transport object (which persists across the
// backoff-induced reconnect):
//   armWindow()  — scale connected and requested HIGH; start watching
//                   (does NOT reset the cluster — faults accumulate across
//                   reconnects so a flapping weak link is not starved).
//   disarm()     — scale started at BALANCED, or backed off; not watching;
//                   clears the accumulated cluster.
//   onDe1Fault() — primary signal; ≥ kDe1FaultThreshold within
//                   kDe1FaultWindowMs of the first fault (sliding) ⇒ trigger.
//   onScaleStall() — backstop; any in-shot stall while armed ⇒ trigger.
// fire() latches skip-HIGH + backed-off so it triggers at most once per
// session and never re-arms (no reconnect loop).
class BlePriorityDetector {
public:
    static constexpr int kDe1FaultThreshold = 2;         // ≥2 DE1 faults in-window (#1176 ConnectionError pattern)
    static constexpr int64_t kDe1FaultWindowMs = 20000;  // covers #1093 ~13s and #1176 ~0.8–15s post scale-HIGH

    // Scale connected and requested HIGH: start watching. No-op (stays
    // disarmed) if a prior backoff already latched skip-HIGH this session.
    // Deliberately does NOT reset the fault count / window: a weak link that
    // flaps (connect → fault → reconnect → fault → …) must accumulate its
    // faults across reconnects, otherwise the per-connect reset starves the
    // ≥threshold-in-window condition exactly on the hardware this targets.
    // The window is anchored to the first fault and slides (see onDe1Fault),
    // so isolated faults far apart never accumulate; only a genuine cluster
    // does. Full reset happens in disarm() (BALANCED start / backed off).
    void armWindow(int64_t /*nowMs*/) {
        if (m_skipHighPriority) { m_armed = false; return; }
        m_armed = true;
    }

    // Scale started at BALANCED (skip flag set) or backed off — not watching.
    // Clears the accumulated cluster so a later session starts clean.
    void disarm() { m_armed = false; m_de1FaultCount = 0; m_windowStartMs = 0; }

    // Primary signal. Returns true exactly once, when ≥ kDe1FaultThreshold
    // faults occur within kDe1FaultWindowMs of the first fault. A fault that
    // arrives after the window elapsed starts a fresh window from itself
    // (sliding) rather than disarming — so a flapping weak device whose
    // faults straddle reconnects still trips, while genuinely isolated faults
    // never reach the threshold.
    bool onDe1Fault(int64_t nowMs) {
        if (!m_armed || m_backoffTriggered) return false;
        if (m_de1FaultCount == 0 || nowMs - m_windowStartMs > kDe1FaultWindowMs) {
            m_windowStartMs = nowMs;  // anchor / re-anchor the window
            m_de1FaultCount = 1;
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
