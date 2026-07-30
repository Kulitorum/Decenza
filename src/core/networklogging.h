#pragma once

#include "core/logtags.h"

#include <QDebug>
#include <QString>

// Shared logging helpers for local network reachability and the app's own
// servers — as distinct from any one device's link.
//
// Every line gets the [Network] marker from the registry (core/logtags.h) plus a
// tag naming its own source:
//
//     [Network][ShotServer] Listening on 0.0.0.0:8080
//     [Network][Reachability] LAN unreachable — no route to 192.168.10.0/24
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
//   NETWORK_LOG   qDebug   per-request detail, poll ticks
//   NETWORK_INFO  qInfo    listeners starting/stopping, reachability outcomes
//   NETWORK_WARN  qWarning bind failures, unreachable peers, refused requests

#define NETWORK_LOG_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_NETWORK, tag, msg, qDebug)
#define NETWORK_INFO_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_NETWORK, tag, msg, qInfo)
#define NETWORK_WARN_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_NETWORK, tag, msg, qWarning)

// Stderr-only variants, for network code with no logMessage signal in scope —
// free functions, static helpers, and main.cpp's startup wiring.
#define NETWORK_LOG(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_NETWORK, tag, msg, qDebug)
#define NETWORK_INFO(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_NETWORK, tag, msg, qInfo)
#define NETWORK_WARN(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_NETWORK, tag, msg, qWarning)
