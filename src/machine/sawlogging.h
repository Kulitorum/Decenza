#pragma once

#include "core/logtags.h"

#include <QDebug>
#include <QString>

// Shared logging helpers for stop-at-weight — the stop decision, the settling
// pass that follows it, the per-(profile, scale) learning store, and the weight
// worker that feeds all three.
//
// Every line gets the [SAW] marker from the registry (core/logtags.h) plus a tag
// naming its own source:
//
//     [SAW][Timing] Stop triggered: weight=35.8 flow=1.21 target=36.0
//     [SAW][Worker] Scale oscillation detected (weight=-12.97 g)
//     [SAW][Learning] Outlier rejected: drip=8.2 g
//
// so one search on [SAW] returns the whole stop-at-weight story from a submitted
// log. Read core/logtags.h for why the marker exists and what the tiers mean;
// this header is just this subsystem's instance of it.
//
// ---- Why SAW has its own marker rather than living under [Scale] ---------
//
// It answers a DIFFERENT QUESTION. [Scale] answers "did the weight readings
// arrive, and were they right" — connection, transport, frames, disconnects.
// [SAW] answers "given readings that arrived, did the app stop the shot in the
// right place, and is it learning the right correction". A correct reading that
// the stop logic then acts on wrongly is a different fault, in different code,
// from a reading that never came, and filing them together sends a reader
// hunting in the wrong subsystem.
//
// This was left open by #1707 ("shot logic, not a device") and resolved here on
// that test rather than on whether the subsystem owns hardware. The registry is
// not restricted to devices.
//
// ---- Choosing a helper --------------------------------------------------
//
//   SAW_LOG_TAGGED  / SAW_LOG_STDERR    qDebug   developer detail
//   SAW_INFO_TAGGED / SAW_INFO_STDERR   qInfo    the user-facing narrative
//   SAW_WARN_TAGGED / SAW_WARN_STDERR   qWarning problems
//
// Those six are the whole set — there is deliberately no bare SAW_LOG /
// SAW_INFO / SAW_WARN. Every caller must say which of the two forms it means,
// because the choice is not cosmetic: _TAGGED also emits logMessage and reaches
// the in-app view, _STDERR does not. A bare name would have to silently pick
// one. (This table listed those three names for a while and the header never
// defined them, which is the failure mode CLAUDE.md names directly — a stated
// fact gets believed, and here it would have been believed by someone reaching
// for a macro that does not exist.)
//
// Pick by AUDIENCE, not by how important the event feels:
//
//   - Per-sample settling ticks, de-jitter internals, model inputs, batch
//     bookkeeping                                   -> SAW_LOG_* (DEBUG).
//     These fire many times per shot and would bury everything else.
//   - The stop itself, and what the shot actually settled at -> SAW_INFO_*.
//     Once per shot, and it is the answer to "why did it stop there".
//   - Learning skipped, cup removed, drip out of range, a corrupt store
//                                                    -> SAW_WARN_*.
//
// Note the shape of the mistake this subsystem started with: 29 WARN, 36 DEBUG
// and ZERO INFO, so a user-level read of a log showed SAW only when something
// went wrong and never showed a shot stopping normally. A subsystem that is
// silent when it is working is the hardest kind of gap to notice, because
// nothing is there to catch your eye.
//
// ---- Mechanics ---------------------------------------------------------
//
// `tag` is a string LITERAL naming the source. Alias these per file rather than
// copying a body:
//   #define SAWW_LOG(msg)  SAW_LOG_STDERR("Worker", msg)
//
// The plain (non-STDERR) forms require `emit logMessage(QString)` in scope. Most
// SAW code has no such signal — it lives in controllers, a settings store and a
// worker thread — so the STDERR forms are the common case here, unlike in the
// scale subsystem where the drivers all carry the signal.

#define SAW_LOG_TAGGED(tag, msg)  DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_SAW, tag, msg, qDebug)
#define SAW_INFO_TAGGED(tag, msg) DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_SAW, tag, msg, qInfo)
#define SAW_WARN_TAGGED(tag, msg) DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_SAW, tag, msg, qWarning)

// Stderr-only variants, for SAW code with no logMessage signal to emit — the
// timing controller, the learning store in Settings, the weight worker. Same
// marker, so the line still turns up in the one search.
#define SAW_LOG_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_SAW, tag, msg, qDebug)
#define SAW_INFO_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_SAW, tag, msg, qInfo)
#define SAW_WARN_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_SAW, tag, msg, qWarning)
