#pragma once

#include <QLatin1String>
#include <QList>
#include <QString>

// =========================================================================
// The registry of log subsystem markers — the single source of truth.
// =========================================================================
//
// Every log line in a registered subsystem starts with that subsystem's
// marker, then optionally names its own source:
//
//     [Scale][BLE AcaiaScale] tare sent
//     [DE1][USB] Probing cu.usbmodem1234
//
// So ONE search on the marker returns the whole subsystem's narrative from a
// log a user sent in. That is the point: this subsystem class is diagnosed
// after the fact from submitted logs, and a reader must not have to know four
// prefixes and then notice which one they forgot. Before the markers existed,
// the scale story needed five different patterns and one of them
// ("[DecentScaleWifi]") matched nothing anyone would think to try.
//
// ---- Adding a subsystem -------------------------------------------------
//
// Two edits, both here: a DECENZA_LOG_MARKER_<NAME> literal, and a row in
// DECENZA_LOG_SUBSYSTEMS. Everything else derives from those — the MCP
// debug_get_log description is built from the table below. Do NOT restate the
// marker list anywhere else; a copy is free to drift, and a marker the
// description does not mention is invisible to the assistant that would have
// used it.
//
// NOTE: this invariant is NOT machine-checked yet. An earlier version of this
// comment said `scripts/check_log_markers.py` parses this header; no such script
// exists — it is task 7.4 of the openspec change
// replace-scale-log-with-system-log-filter. Stating a check that does not exist
// is worse than stating none, because the next editor trusts it.
//
// A marker is a published name. Renaming one breaks every saved query, filter
// and habit built on it, so treat it as API, not an implementation detail.
//
// ---- Severity carries audience ------------------------------------------
//
// Pick a tier by WHO needs the line, not by how important it feels. This is
// what lets a subsystem's user-facing narrative be addressed as
// "marker + minLevel INFO" with no second token, and it is the same query the
// connections-page views run and the MCP tools expose:
//
//   DEBUG  developer detail — protocol frames, per-poll state, parse internals
//   INFO   the narrative a USER may need: lifecycle, discovery outcomes,
//          connect/disconnect, transport choice and fallback, scheduling
//   WARN+  problems — failures, timeouts, unreachable peers, rejected data
//
// Audience, not authorship: a low-level driver logs INFO when its event is
// part of the user-facing story, and a high-level manager logs DEBUG when the
// detail only serves a developer. Both directions of mistake are SILENT — a
// user-facing line left at DEBUG vanishes from its view, and driver chatter
// promoted to INFO puts the firehose back on screen.
//
// Apply markers and tiers only inside a logging helper, never at a call site.
// See src/ble/scales/scalelogging.h for the canonical helper set (and
// refractometerlogging.h for a second subsystem built on the same base), and
// docs/CLAUDE_MD/LOGGING.md for the full guide.

// Marker literals. Usable inside string-literal concatenation, which is how
// the logging macros stamp them, so the token itself lives here exactly once.
#define DECENZA_LOG_MARKER_SCALE         "Scale"
#define DECENZA_LOG_MARKER_DE1           "DE1"
#define DECENZA_LOG_MARKER_REFRACTOMETER "Refractometer"

// The registry. Each row: (marker literal, what the subsystem covers).
// The description is user/assistant-facing — it reaches the MCP tool
// description verbatim, so write it for someone who has never read this code.
#define DECENZA_LOG_SUBSYSTEMS(X)                                              \
    X(DECENZA_LOG_MARKER_SCALE,                                                \
      "Scales: BLE, WiFi and USB drivers, their transports, and scale "         \
      "discovery")                                                             \
    X(DECENZA_LOG_MARKER_DE1,                                                  \
      "The espresso machine: its BLE and serial transports, USB discovery, "    \
      "permissions and connection lifecycle")                                  \
    X(DECENZA_LOG_MARKER_REFRACTOMETER,                                        \
      "DiFluid R1/R2 refractometers. Separate from Scale because these are a "  \
      "different instrument answering different questions, even though they "    \
      "share the scale BLE transports and appear in the same connections view")

// ---- The one place a log line's shape is built -------------------------
//
// Per-subsystem headers (scalelogging.h, refractometerlogging.h, the DE1 set)
// define their tiers in terms of these two, so the "[marker][tag] " shape and
// the write-then-emit pairing exist once. Do not copy a body to specialize it;
// alias these. `marker` and `tag` are string LITERALS; `qFn` is qDebug, qInfo
// or qWarning.
//
// DECENZA_SUBSYS_LOG writes stderr AND emits for recording from one call, so a
// call site cannot describe its own event two different ways — the drift that
// hit 21 USB sites. It requires `emit logMessage(QString)` in scope.
#define DECENZA_SUBSYS_LOG(marker, tag, msg, qFn) do { \
    QString _msg = QString("[" marker "][" tag "] ") + (msg); \
    qFn().noquote() << _msg; \
    emit logMessage(_msg); \
} while(0)

// For code with no logMessage signal to emit — free functions, static helpers,
// JNI shims. Same marker, so the line is still found by the one search.
#define DECENZA_SUBSYS_LOG_STDERR(marker, tag, msg, qFn) \
    qFn().noquote() << (QString("[" marker "][" tag "] ") + (msg))

// ---- Canonical wording for the shared BLE device lifecycle ------------
//
// Thirteen scale drivers and two refractometers report the SAME handful of
// events, so they must report them in the same words — otherwise comparing two
// models' logs means first working
// out whether "First weight received, marking as connected" and "Scale
// confirmed working, reporting connected" are the same thing (they were), and
// whether a model that says neither is broken or just worded differently.
//
// Use these instead of typing the message. Anything genuinely model-specific
// (which characteristic, which extra notification) stays a literal at the call
// site — this is for the events every driver has.
#define DECENZA_BLE_MSG_TRANSPORT_CONNECTED \
    QStringLiteral("Transport connected, starting service discovery")
#define DECENZA_BLE_MSG_TRANSPORT_DISCONNECTED \
    QStringLiteral("Transport disconnected")
#define DECENZA_BLE_MSG_DUPLICATE_CHARACTERISTICS \
    QStringLiteral("Characteristics already set up, ignoring duplicate callback")
// The connect moment — the one INFO line a user looks for, so it reads the same
// for every model. `trigger` names what proved the scale live (a weight frame, a
// status frame), because that part legitimately differs.
#define DECENZA_BLE_MSG_CONNECTED(trigger) \
    QStringLiteral("Reporting connected (%1)").arg(trigger)
#define DECENZA_BLE_MSG_NOTIFY_SCHEDULED(ms) \
    QStringLiteral("Scheduling notification enable in %1 ms (de1app timing)").arg(ms)

namespace DecenzaLog {

struct Subsystem {
    const char* marker;       // bare token, e.g. "Scale"
    const char* description;  // what it covers, for humans and assistants
};

// The registered subsystems, in documentation order.
inline QList<Subsystem> subsystems()
{
#define DECENZA_LOG_SUBSYSTEM_ROW(marker, description) Subsystem{marker, description},
    return QList<Subsystem>{DECENZA_LOG_SUBSYSTEMS(DECENZA_LOG_SUBSYSTEM_ROW)};
#undef DECENZA_LOG_SUBSYSTEM_ROW
}

// The bracketed form to filter a log on: "Scale" -> "[Scale]".
// Callers filter with this rather than composing brackets themselves, so the
// view and the MCP tools cannot disagree about the shape of the token.
inline QString markerFilter(const char* marker)
{
    return QLatin1String("[") + QLatin1String(marker) + QLatin1String("]");
}

} // namespace DecenzaLog
