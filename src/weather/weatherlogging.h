#pragma once

#include "core/logtags.h"

#include <QDebug>
#include <QString>

// Shared logging helpers for the weather and sun-time fetches behind the idle
// screen and the screensaver.
//
// Every line gets the [Weather] marker from the registry (core/logtags.h) plus a
// tag naming its own source:
//
//     [Weather][Manager] Fetching weather for 39.6 -104.9 using NWS
//
// ---- Why this is registered at all --------------------------------------
//
// It was not, and that is the point. These lines carried a hand-typed
// "WeatherManager: " prefix that no marker matched, so they were invisible to
// every per-subsystem count and to debug_get_log's marker filter. That is how
// the periodic fetch reached 125 byte-identical repeats in one submitted log
// without anyone noticing: the analysis that found the log's other repeaters
// ranked them BY MARKER, and an unmarked subsystem cannot appear in a per-marker
// ranking. The blind spot was shaped exactly like this file.
//
// ---- Scope --------------------------------------------------------------
//
// The provider request and what came back, and nothing else. A reader filtering
// [Weather] is asking "why is the weather wrong or stale", which separates a
// provider outage from a location the app resolved incorrectly. Location
// resolution itself belongs to whoever resolved it.

#define WEATHER_LOG_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_WEATHER, tag, msg, qDebug)
#define WEATHER_INFO_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_WEATHER, tag, msg, qInfo)
#define WEATHER_WARN_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_WEATHER, tag, msg, qWarning)

// Stream variants, for the sites that interleave several values. Same marker and
// shape, so a [Weather] search returns them alongside the statement forms.
#define WEATHER_DBG_STREAM(tag) \
    DECENZA_SUBSYS_STREAM(DECENZA_LOG_MARKER_WEATHER, tag, qDebug)
#define WEATHER_WARN_STREAM(tag) \
    DECENZA_SUBSYS_STREAM(DECENZA_LOG_MARKER_WEATHER, tag, qWarning)
