#pragma once

// Post-stop settling thresholds. One definition shared by ShotTimingController and
// tools/shot_eval's settle replays, so the offline replay cannot drift from the app.
namespace Settling {

inline constexpr int SETTLING_WINDOW_SIZE = 6;         // 6-sample circular buffer
inline constexpr double SETTLING_AVG_THRESHOLD = 0.3;  // Max avg drift to declare stable (g)
inline constexpr int SETTLING_STABLE_MS = 1000;        // How long avg must be stable (ms)
// Minimum time the stability gate must hold continuously before
// m_lastCleanSettlingAvg is captured (#1280). Filters out transient
// gate-fires during noisy/oscillating settles — a single sample whose
// window avg happens to satisfy the gate must NOT be persisted as a
// "clean" value to fall back to on cup-removal. 250 ms ≈ 3 consecutive
// samples at the typical ~100 ms scale cadence; shorter than
// SETTLING_STABLE_MS so the fallback still applies to a 700 ms plateau
// (Mark's #1280 case) without waiting for full settlement.
inline constexpr int SETTLING_CLEAN_CAPTURE_MS = 250;
// Maximum physically plausible post-stop drip (#1280 follow-up). Real
// drip is typically 0.5–3 g, even slow-flow profiles stay under ~5 g.
// A "stable" rolling avg more than this far above m_weightAtStop is
// almost certainly a scale fault (frozen reading, glitch) rather than
// a settled cup weight — corpus scan revealed one shot where the
// scale froze at ~75 g during settling on a ~40 g target. Reject the
// recovery in those cases and fall through to the m_weightAtStop floor.
inline constexpr double MAX_PLAUSIBLE_POST_STOP_DRIP_G = 5.0;
inline constexpr double SETTLING_ABOVE_AVG_MARGIN = 0.2; // Current weight must be within this of avg to declare stable (g)
inline constexpr int SETTLING_SILENCE_OVERRIDE_MS = 2000; // If weight unchanged for this long, declare stable regardless of avg margin
// A drop this far below the settling peak is a cup lift, never drip — drip only adds.
inline constexpr double CUP_REMOVED_DROP_G = 20.0;
// A post-stop change smaller than this does not restart the stillness clock.
inline constexpr double SETTLING_STILL_DELTA_G = 0.1;
// A settled average this far below the stop weight is still recovering, not settled.
inline constexpr double SETTLING_AVG_BELOW_STOP_G = 0.5;

}  // namespace Settling
