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
// question is which family actually resolved and whether anything fell back.
//
// Before this marker existed that answer sat in 15 hand-rolled "[Font] …" lines
// in main.cpp. Three things were wrong with them, and it is worth being exact
// about which, because an earlier version of this comment was not:
//
//   - The marker was UNDECLARED, so nothing told a reader the family existed or
//     what it covered. debug_get_log's description is built from the registry;
//     a subsystem absent from it is one nobody knows to search for.
//   - The lines carried no source tag, so "which part of font setup" had to be
//     inferred from the wording.
//   - [FontProbe] — four more lines, in iosbrightness.mm — is genuinely
//     invisible to a `[Font]` filter, because the filter is a case-insensitive
//     SUBSTRING (mcplogfilter.h) and "[Font]" is not a substring of
//     "[FontProbe]". Those four were the real unreachable ones.
//
// What was NOT wrong: a `[Font]` filter did find the 15. They began "[Font] "
// literally, and substring matching does not care whether a marker is
// registered. This comment used to claim that filter "returned nothing while
// the lines sat right there", which is the opposite of what the matcher does —
// a wrong justification for a correct change, and exactly the kind of assertion
// that gets believed and then reasoned from.
//
// Non-Latin locales are the case that matters. The bundled family covers Latin,
// Greek and Cyrillic only; in CJK, Arabic, Hebrew, Devanagari and Thai EVERY
// glyph comes from a platform fallback, so the metric determinism the rest of
// the UI assumes does not hold there and a layout bug is the expected symptom.
//
// ---- Choosing a helper --------------------------------------------------
//
//   FONT_LOG_STDERR   qDebug   which faces registered, per-family detail
//   FONT_INFO_STDERR  qInfo    the resolved outcome a user's bug report turns on
//   FONT_WARN_STDERR  qWarning a family that failed to load, or fell back unexpectedly
//
// stderr-only by construction: font setup runs before any object with a
// logMessage signal exists, so there is no non-STDERR variant to reach for.

#define FONT_LOG_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_FONT, tag, msg, qDebug)
#define FONT_INFO_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_FONT, tag, msg, qInfo)
#define FONT_WARN_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_FONT, tag, msg, qWarning)
