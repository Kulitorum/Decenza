#pragma once

#include "../core/logtags.h"

// Logging tiers for auto flow calibration.
//
// The subsystem answers two questions from a submitted log: "why did my flow
// calibration change" and — the one that actually gets filed as a bug — "why
// does it never change". Before this header every line in it was a bare
// qDebug() with no marker, so neither question had a retrieval handle: a reader
// had to guess the literal substring "Auto flow cal:", and `debug_get_log`,
// which enumerates the registered markers so an assistant knows what to ask
// for, did not list this subsystem at all. The story looked like it did not
// exist.
//
// Tier is chosen by AUDIENCE, not by importance (docs/CLAUDE_MD/LOGGING.md):
//
//   CAL_DETAIL  qDebug    developer detail — per-window mechanics, per-sample
//                         gates, the accumulate tick. Noise to a user.
//   CAL_INFO    qInfo     user-visible. The connections views default to
//                         minLevel INFO, so a line a USER needs must be here.
//   CAL_WARN    qWarning  something is wrong with the data or the write.
//
// The trap this subsystem walked into is the inverted shape LOGGING.md names:
// every FAULT was at WARN and every OUTCOME at DEBUG, so a reader at the
// user-visible tier saw only failures and never learned that the multiplier
// moved, held inside the deadband, or was never able to move at all. The
// outcomes are the answer to both questions above, so they are CAL_INFO:
//
//   - the multiplier changed, and from what to what           -> CAL_INFO
//   - a batch completed but landed inside the deadband        -> CAL_INFO
//   - a profile's windows are being rejected, cumulatively    -> CAL_INFO
//   - which window was picked, its samples, one shot's ideal  -> CAL_DETAIL
//
// Alias, never copy a body — see logtags.h.
#define CAL_LOG_STDERR(tag, msg)  DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_CALIBRATION, tag, msg, qDebug)
#define CAL_INFO_STDERR(tag, msg) DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_CALIBRATION, tag, msg, qInfo)
#define CAL_WARN_STDERR(tag, msg) DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_CALIBRATION, tag, msg, qWarning)

// Stream forms. computeAutoFlowCalibration()'s sites interleave five or six
// values apiece and composing a QString at each one would only make them harder
// to read, which is the case DECENZA_SUBSYS_STREAM exists for.
#define CAL_DETAIL(tag) DECENZA_SUBSYS_STREAM(DECENZA_LOG_MARKER_CALIBRATION, tag, qDebug)
#define CAL_INFO(tag)   DECENZA_SUBSYS_STREAM(DECENZA_LOG_MARKER_CALIBRATION, tag, qInfo)
#define CAL_WARN(tag)   DECENZA_SUBSYS_STREAM(DECENZA_LOG_MARKER_CALIBRATION, tag, qWarning)
