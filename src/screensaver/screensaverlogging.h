#pragma once

#include "core/logtags.h"

#include <QDebug>
#include <QString>

// Shared logging helpers for the screensaver.
//
// Every line gets the [Screensaver] marker from the registry (core/logtags.h)
// plus a tag naming its own source:
//
//     [Screensaver][Brightness] Saved original brightness 0.72
//     [Screensaver][Video] Playlist exhausted, restarting from first item
//
// ---- Why this earns a marker -------------------------------------------
//
// Because the screensaver is not one mechanism, it is three that fail
// independently, and the user's report never says which. "The screen went dark
// mid-shot" and "it never woke up" both arrive as one sentence, while the cause
// is the idle timer, or the brightness save/restore, or video playback holding
// the screen — each with a different fix and different files.
//
// The brightness pair is the one worth calling out: the original value is saved
// on the first dim and restored on wake, and it is ALSO persisted to
// NSUserDefaults so a crash while dimmed can be recovered from on next launch.
// A reader diagnosing "my screen is stuck dim" needs to see which of those three
// moments happened, and they are spread across the platform file and the manager.
//
// ---- Choosing a helper --------------------------------------------------
//
//   SCREENSAVER_LOG_*   qDebug   per-transition detail, playlist bookkeeping
//   SCREENSAVER_INFO_*  qInfo    engage/release, brightness saved and restored
//   SCREENSAVER_WARN_*  qWarning a video that will not load, a restore that failed
//
// The suffix is the other half of the choice and is NOT cosmetic: _TAGGED also
// emits logMessage and so reaches the in-app view, _STDERR does not. Both are
// spelled out; there is deliberately no bare SCREENSAVER_LOG, for the reason
// networklogging.h gives at length — an unsuffixed name means the opposite thing
// in scalelogging.h, and a line moved between subsystems would compile either way
// while silently changing whether a user ever sees it.

#define SCREENSAVER_LOG_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_SCREENSAVER, tag, msg, qDebug)
#define SCREENSAVER_INFO_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_SCREENSAVER, tag, msg, qInfo)
#define SCREENSAVER_WARN_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_SCREENSAVER, tag, msg, qWarning)

// Stderr-only variants, for screensaver code with no logMessage signal in scope —
// the platform shims (iosbrightness.mm) especially, which are free functions.
#define SCREENSAVER_LOG_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_SCREENSAVER, tag, msg, qDebug)
#define SCREENSAVER_INFO_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_SCREENSAVER, tag, msg, qInfo)
#define SCREENSAVER_WARN_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_SCREENSAVER, tag, msg, qWarning)
