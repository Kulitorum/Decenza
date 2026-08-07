#pragma once

#include "core/logtags.h"

#include <QDebug>
#include <QString>

// Shared logging helpers for local network reachability — whether this device
// can reach the LAN at all, as distinct from any one device's link.
//
// Every line gets the [Network] marker from the registry (core/logtags.h) plus a
// tag naming its own source:
//
//     [Network][Reachability] LAN unreachable — no route to 192.168.10.0/24
//
// ---- Scope, stated because the first version of this header overstated it ---
//
// Reachability, and nothing else yet. There are three call sites, all in
// main.cpp. This header originally led with "[Network][ShotServer] Listening on
// 0.0.0.0:8080" as its first example and named a failed server bind as the
// motivating case — but ShotServer logs 117 lines under a hand-rolled
// "ShotServer: " prefix and not one of them carries this marker, so a reader
// filtering [Network] to diagnose a bind failure got nothing.
//
// That is the same defect this change documents for [Font] and [FontProbe], in
// a header written during the same change: a description promising a story the
// marker does not carry. Widen the marker to the servers before widening this
// comment back, not the other way round.
//
// ---- Why this is separate from [Scale] and [DE1] ------------------------
//
// Because it sits beside them, not inside them. "The WiFi scale is unreachable"
// and "the shot server will not bind" are the same question asked about
// different things, and neither belongs to a device: a reader who files a
// routing or permission failure under [Scale] goes looking for a fault in the
// scale driver, which is the wrong file.
//
// The case this exists for is real and recurring on macOS: an ad-hoc-signed dev
// build silently has its outbound LAN traffic dropped by the Local Network
// privacy gate while the internet still works, and it presents as a WiFi scale
// that will not connect. The evidence separating the two is network-level (no
// local port ever bound), not scale-level.
//
// ---- Choosing a helper --------------------------------------------------
//
//   NETWORK_LOG_*   qDebug   per-request detail, poll ticks
//   NETWORK_INFO_*  qInfo    listeners starting/stopping, reachability outcomes
//   NETWORK_WARN_*  qWarning bind failures, unreachable peers, refused requests
//
// The suffix is the other half of the choice, and it is NOT cosmetic: _TAGGED
// also emits logMessage and so reaches the in-app view, _STDERR does not.
//
// Both suffixes are spelled out on purpose — no bare NETWORK_LOG. The stderr
// forms here were briefly unsuffixed, which put this header in direct conflict
// with scalelogging.h, where the SHORTER name (SCALE_LOG) is the one that emits.
// Two headers where the same-shaped name means opposite things is the drift this
// whole convention exists to prevent, and it would have been invisible: moving a
// line between subsystems compiles either way and silently changes whether a
// user ever sees it.

#define NETWORK_LOG_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_NETWORK, tag, msg, qDebug)
#define NETWORK_INFO_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_NETWORK, tag, msg, qInfo)
#define NETWORK_WARN_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_NETWORK, tag, msg, qWarning)

// Stderr-only variants, for network code with no logMessage signal in scope —
// free functions, static helpers, and main.cpp's startup wiring.
#define NETWORK_LOG_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_NETWORK, tag, msg, qDebug)
#define NETWORK_INFO_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_NETWORK, tag, msg, qInfo)
#define NETWORK_WARN_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_NETWORK, tag, msg, qWarning)

// Stream variants. Same marker and shape as the forms above — the difference is
// only how the message is composed, so a reader's [Network] search returns all
// of them together. Use these where a site interleaves several values and a
// QString would obscure rather than help; prefer the statement forms otherwise.
// Being in a helper header is also what lets the .cpp using them sit in
// check_log_markers.py's COVERED_GLOBS: the bare qDebug/qWarning lives here,
// where the gate expects it, instead of in the file it serves.
#define NETWORK_DBG_STREAM(tag) \
    DECENZA_SUBSYS_STREAM(DECENZA_LOG_MARKER_NETWORK, tag, qDebug)
#define NETWORK_WARN_STREAM(tag) \
    DECENZA_SUBSYS_STREAM(DECENZA_LOG_MARKER_NETWORK, tag, qWarning)
