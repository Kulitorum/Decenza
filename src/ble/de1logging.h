#pragma once

#include "core/logtags.h"

#include <QDebug>
#include <QString>

// Shared logging helpers for the DE1 subsystem — the machine itself, its BLE
// and serial transports, and USB discovery.
//
// Every line gets the [DE1] marker from the registry (core/logtags.h) plus a
// tag naming its own source:
//
//     [DE1][BLE] Connecting to DE1 at 1C:9D:C2:...
//     [DE1][USB] Probing port cu.usbmodem1234 VID:1a86 PID:55d3 sysLoc:...
//     [DE1][Serial] Port opened: cu.usbmodem1234 (115200 8N1)
//
// so one search on [DE1] returns the whole machine narrative from a submitted
// log. Before the marker existed the DE1 story was spread across "[BLE DE1]",
// a bare "[USB]" shared with nothing, and ~33 lines with no prefix at all.
//
// Read core/logtags.h for what the tiers mean; this header is the DE1
// subsystem's instance of the same shape the scales use, so the two are
// interchangeable to a reader and to the log views.
//
// ---- Choosing a helper --------------------------------------------------
//
//   DE1_LOG_TAGGED    qDebug   developer detail
//   DE1_INFO_TAGGED   qInfo    the user-facing narrative
//   DE1_WARN_TAGGED   qWarning problems
//
// Pick by AUDIENCE, not by authorship or by how important the event feels.
// `debug_get_log` with minLevel INFO shows exactly the INFO-and-above set today.
// The connections-page DE1 view does NOT yet — it is an unfiltered append of
// every line that reaches `de1LogMessage`, and `de1LogMessage(QString)` carries
// no level, so it cannot filter. Re-sourcing it from the system log at INFO+ is
// task 5.1/5.2 of the openspec change `replace-scale-log-with-system-log-filter`.
// Choose tiers for the INFO+ contract regardless: it is what the MCP query
// already honours and what the view will honour, and a line assigned by today's
// unfiltered view would have to be re-judged when 5.1 lands.
//
// So:
//
//   - Characteristic writes, MMR reads, state-machine ticks, frame uploads
//                                                  -> DEBUG.
//   - Found the machine, connecting, connected, dropped, service discovery
//     outcome, firmware/serial identity, permission results, transport chosen
//                                                  -> DE1_INFO.
//   - Connect failed, watchdog fired, write abandoned after retries, service
//     missing, controller error            -> WARN.
//
// (Only the six *_TAGGED forms below exist. There is deliberately no bare
// `DE1_LOG(msg)` the way `scalelogging.h` defines `SCALE_LOG(prefix, msg)` — the
// scale one exists to prepend "BLE " for its 13 drivers, and the DE1 has no such
// family. Each file aliases its own short name; see Mechanics.)
//
// The DE1 half is a stricter test of this than the scales were: the machine
// talks constantly (per-frame telemetry, per-write acks), so almost everything
// here is DEBUG and the INFO set has to stay short enough to read as a story.
//
// ---- Mechanics ---------------------------------------------------------
//
// `tag` is a string LITERAL naming the source. Each DE1 file aliases these
// rather than copying a body:
//   #define USB_LOG(msg)  DE1_LOG_TAGGED("USB", msg)
//   #define USB_INFO(msg) DE1_INFO_TAGGED("USB", msg)
//
// The non-_STDERR forms require `emit logMessage(QString)` to be in scope, i.e.
// use them inside a QObject carrying that signal. Never hand-roll a prefix at a
// call site.

#define DE1_LOG_TAGGED(tag, msg)  DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_DE1, tag, msg, qDebug)
#define DE1_INFO_TAGGED(tag, msg) DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_DE1, tag, msg, qInfo)
#define DE1_WARN_TAGGED(tag, msg) DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_DE1, tag, msg, qWarning)

// Stderr-only variants, for two cases:
//
//   1. DE1 code with no logMessage signal to emit — free functions, static
//      helpers, JNI shims.
//   2. A `const` member function. moc generates signal emitters as NON-const, so
//      `emit logMessage(...)` will not compile in one; this is why
//      `DE1Device::dropDeviceWriteIfFirmwareFlash` and
//      `dropIfFirmwareFlashInProgress` use DE1_WARN_STDERR_TAGGED and not the
//      emitting form. That is a hard constraint, not a preference.
//
// Same marker either way, so the line still turns up in the one search. It does
// not reach the in-app view BY ITSELF — but a class may pair one with its own
// `emit`, which is exactly what BLEManager's de1Debug/de1Info/de1Warn do: they
// send the bare text to the window and the marked text to stderr, so the window
// keeps rendering as it always has. All three tiers exist so nobody hand-rolls
// the missing one.
#define DE1_LOG_STDERR_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_DE1, tag, msg, qDebug)
#define DE1_INFO_STDERR_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_DE1, tag, msg, qInfo)
#define DE1_WARN_STDERR_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_DE1, tag, msg, qWarning)
