#pragma once

#include <QtGlobal>

#include <algorithm>

// Consecutive-sample confirmation, the scale subsystem's standard way of saying
// "this reading has held long enough to act on".
//
// The idiom — increment while a condition holds, reset the instant it doesn't, act
// once the count crosses a threshold — was hand-rolled independently four times
// before this header existed (weightprocessor.cpp's tare-landed, oscillation-recovery
// and untared-cup streaks, then the auto-tare gates in machinestate.cpp). The third
// copy's own comment said it was "mirroring the oscillation-recovery debounce above",
// which is the point at which it should have been extracted rather than repeated.
//
// It is a sample COUNT and never a wall-clock duration. That is a project rule
// (CLAUDE.md: "Never use timers as guards/workarounds"), and it is also the only
// thing that works here: supported scales report anywhere from 2 Hz to 10 Hz, so a
// fixed millisecond window means a different number of samples on different
// hardware, while a sample count means the same amount of evidence everywhere.
//
// Every site is on it: the auto-tare settle and zero-drift gates in machinestate.cpp,
// and weightprocessor.cpp's tare-landed, spike-rejection, oscillation-recovery and
// untared-cup streaks. That migration is a pure mechanical substitution — `++x >= N`
// became `x.update(true) >= N` and `x = 0` became `x.reset()`, both preserving the
// value semantics exactly — which is the test of whether the abstraction was the
// right one. An abstraction only a new call site uses would not have been.
namespace SampleStreak {

// Counts consecutive samples for which a caller-evaluated condition held.
//
//     m_streak.update(weight > 50.0);
//     if (m_streak.reached(4)) { ... }
class Counter {
public:
    // Returns the new count so a caller can act on the value inline.
    int update(bool conditionHeld) {
        m_count = conditionHeld ? m_count + 1 : 0;
        return m_count;
    }

    bool reached(int samples) const { return m_count >= samples; }
    int count() const { return m_count; }
    bool justStarted() const { return m_count == 0; }
    void reset() { m_count = 0; }

private:
    int m_count = 0;
};

// Tracks whether the last N samples all sat inside a band, measured as the spread
// across the whole window (max - min), NOT as a per-sample delta.
//
// The distinction is the whole reason this class exists rather than a Counter.
// A per-sample check calls a slow monotonic ramp "still": the drift that motivated
// the auto-tare settle gate moved about 0.47 g per sample, so every individual step
// sat inside a 1 g band while the reading travelled 20 g. Bounding the SPREAD
// catches the ramp; bounding each step does not.
//
// Window size is fixed at construction and capped — callers need three or four
// samples, not a history buffer.
class Window {
public:
    static constexpr int MaxSamples = 8;

    explicit Window(int samples)
        : m_size(qBound(2, samples, MaxSamples)) {}

    void add(double value) {
        if (m_filled < m_size) {
            m_samples[m_filled++] = value;
        } else {
            std::rotate(m_samples, m_samples + 1, m_samples + m_size);
            m_samples[m_size - 1] = value;
        }
    }

    // False until the window has filled — an unproven reading is never "still".
    bool withinBand(double band) const {
        if (m_filled < m_size)
            return false;
        const auto [lo, hi] = std::minmax_element(m_samples, m_samples + m_filled);
        return (*hi - *lo) <= band;
    }

    bool isEmpty() const { return m_filled == 0; }
    int size() const { return m_size; }
    void reset() { m_filled = 0; }

private:
    int m_size;
    int m_filled = 0;
    double m_samples[MaxSamples] = {};
};

}  // namespace SampleStreak
