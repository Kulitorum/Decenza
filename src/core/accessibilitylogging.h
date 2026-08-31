#pragma once

#include "core/logtags.h"

#include <QDebug>
#include <QString>

// Shared logging helpers for screen-reader announcements and TTS setup.
//
// Every line gets the [Accessibility] marker from the registry (core/logtags.h)
// plus a tag naming its own source:
//
//     [Accessibility][Route] path=platform isActive=true len=42
//     [Accessibility][Tts] engine ready, locale en_US
//
// ---- Why this earns a marker ------------------------------------------
//
// "TalkBack says nothing on this screen" and "it speaks everything twice" are
// the two reports this subsystem generates, and neither is answerable from any
// other marker. An announcement either reached the platform, went to the app's
// own TTS, or was dropped — and only these lines say which. The route matters
// more than any assumption about the reader: #1300's reporter ran TalkMan under
// TalkBack's class name, so what the code DID is the only reliable evidence.
//
// Before this marker the file carried two hand-rolled conventions that had
// drifted apart — eight "[a11y] …" lines on bare qInfo and six
// "AccessibilityManager: …" lines on bare qDebug/qWarning/qInfo/qCritical.
// Neither was a registered marker, so a reader pulling the accessibility story
// out of a submitted log with a [Accessibility] filter got nothing at all, and
// a `grep a11y` got only the announcement half without the TTS setup that
// usually explains it. That is the same split logtags.h records for
// "[DecentScaleWifi]" and "[MulticastLock]".
//
// ---- Choosing a helper --------------------------------------------------
//
//   A11Y_LOG_*   qDebug    setup steps, migration detail
//   A11Y_INFO_*  qInfo     the route an announcement actually took
//   A11Y_WARN_*  qWarning  an unreadable store, an invariant broken
//
// INFO for the routes is deliberate and is the audience rule from LOGGING.md,
// not a judgement about importance: the connections views default to minLevel
// INFO, so a route left at DEBUG is absent from the view a user is asked to
// screenshot — which is precisely the half of a "says nothing" report that
// carries the answer.
//
// The suffix is the other half of the choice and is NOT cosmetic: _TAGGED also
// emits logMessage and so reaches the in-app view, _STDERR does not. Both are
// spelled out; there is deliberately no bare A11Y_LOG — see networklogging.h
// for why an unsuffixed name is a trap across headers.

#define A11Y_LOG_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_ACCESSIBILITY, tag, msg, qDebug)
#define A11Y_INFO_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_ACCESSIBILITY, tag, msg, qInfo)
#define A11Y_WARN_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_ACCESSIBILITY, tag, msg, qWarning)

// Stderr-only variants. AccessibilityManager is constructed before anything is
// wired to its logMessage signal, so setup lines use these.
#define A11Y_LOG_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_ACCESSIBILITY, tag, msg, qDebug)
#define A11Y_INFO_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_ACCESSIBILITY, tag, msg, qInfo)
#define A11Y_WARN_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_ACCESSIBILITY, tag, msg, qWarning)
