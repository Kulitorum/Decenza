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
// Pick by AUDIENCE, not by authorship or by how important the event feels. Both
// consumers now honour the same contract: `debug_get_log` with minLevel INFO, and
// the connections-page DE1 view, which reads the system log for [DE1] at INFO and
// above. So a tier chosen here decides both, and DEBUG genuinely means "not on
// screen".
//
// (This note used to say the view could not filter, because it was an unfiltered
// append of a level-less `de1LogMessage` signal. That signal no longer exists.)
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
//   2. A `const` member function. Our `logMessage` signals are declared non-const
//      (`de1device.h:361`, `de1transport.h:163`), so `emit logMessage(...)` will
//      not compile in one; this is why
//      `DE1Device::dropDeviceWriteIfFirmwareFlash` and
//      `dropIfFirmwareFlashInProgress` use DE1_WARN_STDERR_TAGGED and not the
//      emitting form.
//
//      This used to read "moc generates signal emitters as NON-const … a hard
//      constraint, not a preference." That is FALSE, and it was written with no
//      citation: moc honours a const signal and emits a const emitter that
//      const_casts `this` (qtbase/src/tools/moc/generator.cpp:1297-1300, and
//      moc.cpp:604 parses the qualifier without complaint). So declaring the
//      signal const IS available if the emitting form is ever wanted in a const
//      member — the limit is our declaration, not the toolchain. Recorded because
//      an un-sourced claim about Qt gets believed and then closes off a real
//      option; see CLAUDE.md.
//
// Same marker either way, so the line still turns up in the one search. These do
// NOT reach the in-app views, and nothing pairs them with an emit any more: the
// views are now filtered reads of the system log, so the marker alone is what
// puts a line on screen. All three tiers exist so nobody hand-rolls the missing
// one.
#define DE1_LOG_STDERR_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_DE1, tag, msg, qDebug)
#define DE1_INFO_STDERR_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_DE1, tag, msg, qInfo)
#define DE1_WARN_STDERR_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_DE1, tag, msg, qWarning)

// As above, but `tag` is a runtime QString rather than a literal — for a helper
// that logs on behalf of SEVERAL sources, where a hard-coded tag would be the
// wrong answer for at least one of them. BLEManager::de1Debug/de1Info/de1Warn use
// this: main.cpp drives the DE1 reconnect ladder through those forwarders, and a
// hard-coded "BLEManager" tag would stamp main.cpp's own lines with a source that
// never wrote them. Mirrors scalelogging.h's *_STDERR_DYN, added for the same
// reason on the scale side.
#define DE1_LOG_STDERR_DYN(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR_DYN(DECENZA_LOG_MARKER_DE1, tag, msg, qDebug)
#define DE1_INFO_STDERR_DYN(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR_DYN(DECENZA_LOG_MARKER_DE1, tag, msg, qInfo)
#define DE1_WARN_STDERR_DYN(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR_DYN(DECENZA_LOG_MARKER_DE1, tag, msg, qWarning)
