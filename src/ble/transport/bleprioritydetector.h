#pragma once

#include <cstdint>

// Pure, Qt-free decision logic for the scale connection-priority backoff
// (dual-HIGH BLE contention, #1093/#1176). Owned by QtScaleBleTransport,
// which forwards detection inputs and supplies a monotonic millisecond clock
// (the detector holds no clock, so it is fully deterministic under test).
//
// This detector instance is per-transport (per scale connection lifetime).
// The AUTHORITATIVE, app-run-wide skip-HIGH decision lives on the BLEManager
// singleton (QtScaleBleTransport seeds this detector from it on connect and
// sets it on backoff), so once any scale backs off, every scale this app run
// skips HIGH. This detector's own fire-once guarantee is the per-transport
// half of that; the BLEManager latch is what makes it app-run-scoped.
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
// transport lifetime and never re-arms (no reconnect loop); combined with
// the BLEManager latch this is at most once per app run across all scales.
class BlePriorityDetector {
public:
    static constexpr int kDe1FaultThreshold = 2;         // ≥2 DE1 faults in-window (#1176 ConnectionError pattern)
    static constexpr int64_t kDe1FaultWindowMs = 60000;  // covers #1093 ~13s, #1176 ~0.8–15s, and #1238 ~20s (write-failed → AuthorizationError) post scale-HIGH

    // Scale connected and requested HIGH: start watching. No-op (stays
    // disarmed) if a prior backoff already latched skip-HIGH (per-transport,
    // or seeded from the app-run BLEManager latch on a fresh transport).
    // `nowMs` is accepted but unused: the window anchors to the FIRST fault
    // (in onDe1Fault), not the connect instant, so there is nothing to do
    // with the clock here; the parameter is kept so callers need not change
    // if anchor-at-arm semantics are ever added.
    // Deliberately does NOT reset the fault count / window: a weak link that
    // flaps (connect → fault → reconnect → fault → …) must accumulate its
    // faults across reconnects, otherwise the per-connect reset starves the
    // ≥threshold-in-window condition exactly on the hardware this targets.
    // The window is anchored to the first fault and slides (see onDe1Fault),
    // so isolated faults far apart never accumulate; only a genuine cluster
    // does. Full reset happens in disarm() (BALANCED start / backed off).
    // `observe` (observe-mode change): when true the detector runs the
    // IDENTICAL trigger conditions but reports them via wouldFire() — no
    // latch, no disarm, keeps detecting subsequent episodes — and ignores the
    // skip-HIGH guard (observe forces HIGH at the transport, so a stale latch
    // must not suppress detection). enforce (observe=false) is byte-identical
    // to the pre-change behavior. observe is re-asserted on every arm so it
    // never leaks across an enforce re-arm.
    void armWindow(int64_t /*nowMs*/, bool observe = false) {
        m_observe = observe;
        // INVARIANT: observe MUST be evaluated before the skip-HIGH guard.
        // observe overrides (but does not erase) a persisted latch, so a
        // stale m_skipHighPriority must NOT suppress observe arming. Do not
        // reorder these two branches — the ordering is the only thing that
        // makes the legal (observe && m_skipHighPriority) state behave.
        if (observe) { m_armed = true; return; }
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
            // Window expired (or first fault). In observe mode, an expiry
            // while ≥1 fault had accumulated without reaching the threshold
            // is a "cluster subsided without escalation" — surface it for
            // observe-only logging (the transport polls takeObserveClusterSubsided()).
            if (m_observe && m_de1FaultCount >= 1) m_observeClusterSubsided = true;
            m_windowStartMs = nowMs;  // anchor / re-anchor the window
            m_de1FaultCount = 1;
            return false;
        }
        if (++m_de1FaultCount < kDe1FaultThreshold) return false;
        return m_observe ? wouldFire() : fire();
    }

    // Backstop: an in-shot scale-feed stall while armed (covers sessions with
    // no early DE1 cluster — the actual #1176 shot-1151 case).
    bool onScaleStall() {
        if (!m_armed || m_backoffTriggered) return false;
        return m_observe ? wouldFire() : fire();
    }

    // Observe-mode introspection: which path a true return took, and the
    // one-shot "cluster subsided without escalation" notice (cleared on read).
    bool observing() const { return m_observe; }
    bool takeObserveClusterSubsided() {
        const bool v = m_observeClusterSubsided;
        m_observeClusterSubsided = false;
        return v;
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

    // Observe-mode counterpart of fire(): reports the trigger WITHOUT
    // latching skip-HIGH, WITHOUT marking backed-off, and WITHOUT disarming,
    // so detection keeps running for the rest of the session. Resets the
    // cluster window so the next independent episode is detected cleanly.
    bool wouldFire() {
        m_de1FaultCount = 0;
        m_windowStartMs = 0;
        return true;
    }

    bool m_skipHighPriority = false;
    bool m_backoffTriggered = false;
    bool m_armed = false;
    bool m_observe = false;                // armed in observe (no-action) mode
    bool m_observeClusterSubsided = false; // one-shot, observe-only
    int m_de1FaultCount = 0;
    int64_t m_windowStartMs = 0;
};
