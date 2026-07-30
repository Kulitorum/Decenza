#pragma once

#include "core/logtags.h"

#include <QDebug>
#include <QString>

// Shared logging helpers for appearance: themes, colours, backgrounds and
// per-role font sizes.
//
// Every line gets the [Theme] marker from the registry (core/logtags.h) plus a
// tag naming its own source:
//
//     [Theme][Background] Preset "Espresso" applied
//     [Theme][Overrides] Clamped timerSize 100000 -> 120
//
// ---- Why this is separate from [Font] -----------------------------------
//
// Because they answer different questions that arrive as the same complaint.
// Text that looks wrong is either a SIZE the user chose (this marker: an override
// stored, clamped, or rejected) or a FAMILY the platform substituted ([Font]:
// which face actually resolved and whether anything fell back). Merging them
// would mean a reader filtering for a font-substitution bug wades through
// background-image and colour lines, and vice versa.
//
// The seam is real in the code too: settings_theme.cpp owns the stored overrides
// and validates them, while main.cpp owns registration of the bundled families.
// Those are the two files, and they are not the same subsystem.
//
// ---- Choosing a helper --------------------------------------------------
//
//   THEME_LOG_*   qDebug   per-property detail, preset bookkeeping
//   THEME_INFO_*  qInfo    the theme actually applied, a background that changed
//   THEME_WARN_*  qWarning a corrupt stored value, an unknown role, a clamp
//
// The suffix is the other half of the choice and is NOT cosmetic: _TAGGED also
// emits logMessage and so reaches the in-app view, _STDERR does not. Both are
// spelled out; there is deliberately no bare THEME_LOG — see networklogging.h for
// why an unsuffixed name is a trap across headers.

#define THEME_LOG_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_THEME, tag, msg, qDebug)
#define THEME_INFO_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_THEME, tag, msg, qInfo)
#define THEME_WARN_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_THEME, tag, msg, qWarning)

// Stderr-only variants. Settings domain objects have no logMessage signal, so
// these are the common case here.
#define THEME_LOG_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_THEME, tag, msg, qDebug)
#define THEME_INFO_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_THEME, tag, msg, qInfo)
#define THEME_WARN_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_THEME, tag, msg, qWarning)
