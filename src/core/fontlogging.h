#pragma once

#include "core/logtags.h"

#include <QDebug>
#include <QString>

// Shared logging helpers for font loading and text rendering.
//
// Every line gets the [Font] marker from the registry (core/logtags.h) plus a tag
// naming its own source:
//
//     [Font][Bundled] Registered Decenza Sans (4 faces)
//     [Font][Fallback] "Profile" fell back to a platform family
//
// ---- Why this earns a marker ------------------------------------------
//
// Text that renders wrongly does not arrive as "the font system is broken". It
// arrives as "the buttons are clipped in Japanese", "the ligature is missing
// from Profile" (#1537), or a screenshot of a garbled label. The reader's first
// question is which family actually resolved and whether anything fell back, and
// before this marker existed that answer was spread across 15 hand-rolled
// "[Font] …" lines that no registered marker matched — so a `[Font]` filter over
// a submitted log returned nothing while the lines sat right there.
//
// Non-Latin locales are the case that matters. The bundled family covers Latin,
// Greek and Cyrillic only; in CJK, Arabic, Hebrew, Devanagari and Thai EVERY
// glyph comes from a platform fallback, so the metric determinism the rest of
// the UI assumes does not hold there and a layout bug is the expected symptom.
//
// ---- Choosing a helper --------------------------------------------------
//
//   FONT_LOG   qDebug   which faces registered, per-family detail
//   FONT_INFO  qInfo    the resolved outcome a user's bug report turns on
//   FONT_WARN  qWarning a family that failed to load, or fell back unexpectedly
//
// stderr-only by construction: font setup runs before any object with a
// logMessage signal exists, so there is no non-STDERR variant to reach for.

#define FONT_LOG(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_FONT, tag, msg, qDebug)
#define FONT_INFO(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_FONT, tag, msg, qInfo)
#define FONT_WARN(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_FONT, tag, msg, qWarning)
