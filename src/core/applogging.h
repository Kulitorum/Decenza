#pragma once

#include "core/logtags.h"

#include <QDebug>
#include <QString>

// Shared logging helpers for the app's own housekeeping — the things Decenza
// does for itself rather than to the espresso machine.
//
// Every line gets the [App] marker from the registry (core/logtags.h) plus a tag
// naming its own source:
//
//     [App][Update]  current= 2.0.4 build= 3566 latest= 2.0.4 newer= false
//     [App][Weather] Fetching weather for 39.6 -104.9 using NWS
//
// ---- Why one marker and not two -----------------------------------------
//
// These were briefly registered as separate [Weather] and [Update] markers.
// Folded because neither is a SUBSYSTEM in the sense the rest of the registry
// uses the word: every other marker names something a user diagnoses about their
// machine or its devices, and a reader reaching for either of these is asking a
// different kind of question — is the APP misbehaving, or the coffee equipment.
// Splitting them also cost two registry rows to carry four log lines between
// them. The tag keeps the distinction where it is cheap.
//
// Add a new tag here rather than a new marker for anything of the same kind
// (licence checks, telemetry, migration housekeeping). Promote to its own marker
// only when its lines are retrieved as a group often enough that filtering [App]
// and reading tags stops being good enough.
//
// ---- Why these are registered at all ------------------------------------
//
// They were not, and that is the point. Both files carried a hand-typed
// "WeatherManager: " / "UpdateChecker: " prefix that no marker matched, so they
// were absent from every per-marker count and from debug_get_log's filter. That
// is how four periodic lines reached 125, 125, 132 and 132 byte-identical
// repeats in one submitted log without appearing in an analysis looking for
// exactly that: the analysis ranked BY MARKER, and an unmarked subsystem cannot
// appear in a per-marker ranking.

#define APP_LOG_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_APP, tag, msg, qDebug)
#define APP_INFO_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_APP, tag, msg, qInfo)
#define APP_WARN_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_APP, tag, msg, qWarning)

// Stream variants, for the sites that interleave several values. Same marker and
// shape, so an [App] search returns them alongside the statement forms.
#define APP_DBG_STREAM(tag) \
    DECENZA_SUBSYS_STREAM(DECENZA_LOG_MARKER_APP, tag, qDebug)
#define APP_WARN_STREAM(tag) \
    DECENZA_SUBSYS_STREAM(DECENZA_LOG_MARKER_APP, tag, qWarning)
