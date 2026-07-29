#pragma once

#include "core/logtags.h"

#include <QDebug>
#include <QString>

// Shared logging helpers for the scale subsystem — drivers, transports and USB.
// (Refractometers share these transports but are a different instrument, so they
// have their own marker: see refractometers/refractometerlogging.h.)
//
// Every line gets the [Scale] marker from the registry (core/logtags.h) plus a
// tag naming its own source:
//
//     [Scale][BLE AcaiaScale] tare sent
//     [Scale][USB Scale] Probing cu.usbmodem1234
//
// so one search on [Scale] returns the whole scale narrative from a submitted
// log. Read core/logtags.h for why the marker exists and what the tiers mean;
// this header is just the scale subsystem's instance of it.
//
// ---- Choosing a helper --------------------------------------------------
//
//   SCALE_LOG   / SCALE_LOG_TAGGED    qDebug   developer detail
//   SCALE_INFO  / SCALE_INFO_TAGGED   qInfo    the user-facing narrative
//   SCALE_WARN  / SCALE_WARN_TAGGED   qWarning problems
//
// Pick by AUDIENCE, not by authorship or by how important the event feels. The
// connections-page scale view and `debug_get_log` with minLevel INFO both show
// exactly the INFO-and-above set, so:
//
//   - A frame decode, a poll tick, a parse detail  -> SCALE_LOG (DEBUG).
//     These would bury the view; they stay in the log, one minLevel away.
//   - Found a scale, connecting, connected, dropped, switched transport,
//     scheduled a reconnect                        -> SCALE_INFO.
//   - Open failed, timed out, unreachable, rejected data -> SCALE_WARN.
//
// A driver may legitimately use SCALE_INFO: "connected" is the user's story
// even though a driver emits it. Equally BLEManager uses SCALE_LOG for detail
// only a developer wants. Getting this wrong is silent in both directions — a
// narrative line left at DEBUG disappears from the view, and chatter promoted
// to INFO puts the firehose back on screen.
//
// ---- Mechanics ---------------------------------------------------------
//
// *_TAGGED is the base; `tag` is a string LITERAL naming the source.
// SCALE_LOG/SCALE_INFO/SCALE_WARN are the BLE-driver shorthand, tagging
// "BLE <x>". Each scale file aliases them rather than copying a body:
//   #define ACAIA_LOG(msg)  SCALE_LOG("AcaiaScale", msg)
//   #define ACAIA_INFO(msg) SCALE_INFO("AcaiaScale", msg)
//
// These require `emit logMessage(QString)` to be in scope, i.e. use them inside
// a QObject carrying that signal. Never hand-roll a prefix at a call site:
// usbscalemanager.cpp did that at 73 sites and drifted into qDebug saying one
// thing while logMessage said another, at 21 of them.

#define SCALE_LOG_TAGGED(tag, msg)  DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_SCALE, tag, msg, qDebug)
#define SCALE_INFO_TAGGED(tag, msg) DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_SCALE, tag, msg, qInfo)
#define SCALE_WARN_TAGGED(tag, msg) DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_SCALE, tag, msg, qWarning)

// Stderr-only variants for scale code with no logMessage signal to emit —
// free functions, static helpers, JNI shims. Same marker, so the line still
// turns up in the one search; it just doesn't reach the in-app view. All three
// tiers exist so nobody hand-rolls the missing one.
#define SCALE_LOG_STDERR_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_SCALE, tag, msg, qDebug)
#define SCALE_INFO_STDERR_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_SCALE, tag, msg, qInfo)
#define SCALE_WARN_STDERR_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_SCALE, tag, msg, qWarning)

// Runtime-tag variants, for a forwarder logging on behalf of more than one source
// (see DECENZA_SUBSYS_LOG_STDERR_DYN). BLEManager's scale tiers use these because
// main.cpp drives the scale reconnect ladder and its lines must not be stamped
// "BLEManager".
#define SCALE_LOG_STDERR_DYN(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR_DYN(DECENZA_LOG_MARKER_SCALE, tag, msg, qDebug)
#define SCALE_INFO_STDERR_DYN(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR_DYN(DECENZA_LOG_MARKER_SCALE, tag, msg, qInfo)
#define SCALE_WARN_STDERR_DYN(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR_DYN(DECENZA_LOG_MARKER_SCALE, tag, msg, qWarning)

#define SCALE_LOG(prefix, msg)  SCALE_LOG_TAGGED("BLE " prefix, msg)
#define SCALE_INFO(prefix, msg) SCALE_INFO_TAGGED("BLE " prefix, msg)
#define SCALE_WARN(prefix, msg) SCALE_WARN_TAGGED("BLE " prefix, msg)
