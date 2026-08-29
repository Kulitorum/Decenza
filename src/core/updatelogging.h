#pragma once

#include "core/logtags.h"

#include <QDebug>
#include <QString>

// Shared logging helpers for the app's own update check.
//
// Every line gets the [Update] marker from the registry (core/logtags.h) plus a
// tag naming its own source:
//
//     [Update][Checker] current= "2.0.4" build= 3566 latest= "2.0.4" newer= false
//
// ---- Why this is registered at all --------------------------------------
//
// Same reason as weatherlogging.h, found in the same pass: these lines carried a
// hand-typed "UpdateChecker: " prefix that no marker matched, so they were
// absent from every per-marker count and from debug_get_log's filter. The
// periodic check reached 132 byte-identical repeats of its own two lines in one
// submitted log while being invisible to the analysis looking for exactly that.
//
// ---- Scope --------------------------------------------------------------
//
// When the check ran, the versions it compared, and whether a newer build was
// offered. The question is "why am I not being offered the update", and the
// usual answer is a release with no asset for this platform rather than a check
// that failed — which is why the no-asset line is a WARN and not a debug detail.

#define UPDATE_LOG_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_UPDATE, tag, msg, qDebug)
#define UPDATE_INFO_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_UPDATE, tag, msg, qInfo)
#define UPDATE_WARN_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_UPDATE, tag, msg, qWarning)

#define UPDATE_DBG_STREAM(tag) \
    DECENZA_SUBSYS_STREAM(DECENZA_LOG_MARKER_UPDATE, tag, qDebug)
#define UPDATE_WARN_STREAM(tag) \
    DECENZA_SUBSYS_STREAM(DECENZA_LOG_MARKER_UPDATE, tag, qWarning)
