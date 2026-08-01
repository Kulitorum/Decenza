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
// This IS machine-checked: `scripts/check_log_markers.py` parses the literals below
// and runs on every PR touching src/** (.github/workflows/text-invariants.yml). It
// fails on a bare qDebug in a covered file, on a registered marker typed into a
// message, and on a helper header applying a marker this registry does not declare.
//
// (An earlier version of this comment claimed the same thing while no such script
// existed. It was corrected to say so, because a stated check that does not exist is
// worse than none — the next editor trusts it. Now it does exist.)
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
#define DECENZA_LOG_MARKER_BLUETOOTH     "Bluetooth"
#define DECENZA_LOG_MARKER_SAW           "SAW"
#define DECENZA_LOG_MARKER_FONT          "Font"
#define DECENZA_LOG_MARKER_NETWORK       "Network"
#define DECENZA_LOG_MARKER_SCREENSAVER   "Screensaver"
#define DECENZA_LOG_MARKER_THEME         "Theme"
#define DECENZA_LOG_MARKER_STORAGE       "Storage"
#define DECENZA_LOG_MARKER_EQUIPMENT     "Equipment"

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
      "share the scale BLE transports and appear in the same connections view") \
    X(DECENZA_LOG_MARKER_BLUETOOTH,                                            \
      "The local Bluetooth radio itself: adapter power state, wedge detection "  \
      "and automatic recovery, and platform capability problems. Separate from " \
      "Scale and DE1 because it is BENEATH both — when the adapter is off or "   \
      "wedged, neither device can connect, and attributing that to one of them " \
      "sends a reader looking for a fault in the wrong place")                   \
    X(DECENZA_LOG_MARKER_SAW,                                                  \
      "Stop-at-weight: deciding when to stop a shot so it lands on the target "  \
      "weight, and learning each profile+scale pair's drip and lag from past "   \
      "shots. Answers \"why did my shot stop where it did\" and \"why is it "    \
      "consistently over or under\". Separate from Scale, which answers whether " \
      "the weight readings arrived at all — a correct reading the stop logic "   \
      "then acts on wrongly is a different fault from a reading that never came") \
    X(DECENZA_LOG_MARKER_FONT,                                                 \
      "Font loading and text rendering: which bundled families registered, "     \
      "which text fell back to a platform font, and glyph-coverage problems. "   \
      "Users report these as layout or language bugs, so this is the first "     \
      "thing to check for clipped or garbled text in a non-Latin locale")       \
    X(DECENZA_LOG_MARKER_NETWORK,                                              \
      "Local network reachability — whether this device can reach the LAN at "   \
      "all — as opposed to any one device's link. Separate from Scale and DE1 "  \
      "because a routing or permission failure is not a fault in the scale "     \
      "driver, and filing it under Scale sends a reader to the wrong file. "     \
      "NOTE: currently reachability only. The app's own servers (ShotServer, "   \
      "MQTT) still log under hand-rolled prefixes and are NOT reachable through " \
      "this marker yet")                                                         \
    X(DECENZA_LOG_MARKER_SCREENSAVER,                                            \
      "The screensaver: when it engages and releases, screen dimming and "        \
      "brightness restore, the idle timer, and video playback. Users report "     \
      "these as \"the screen went dark mid-shot\" or \"it never woke up\", and "  \
      "the answer is usually which of several independent things (idle timer, "   \
      "brightness, video) did or did not fire")                                   \
    X(DECENZA_LOG_MARKER_STORAGE,                                                \
      "Whether an edit you made actually reached the database. Answers \"I "      \
      "rated that shot and it came back blank\" and \"my note vanished\": writes " \
      "are queued to a background worker per storage, and a worker destroyed "     \
      "with tasks still queued discards them. Covers the drain at quit and at "    \
      "backgrounding, that discard, and the shot-file export those writes "        \
      "trigger. Not about WHAT was stored — that is Equipment for gear identity "  \
      "— only about whether the write survived")                                   \
    X(DECENZA_LOG_MARKER_THEME,                                                  \
      "Appearance: theme selection and switching, custom colours, background "    \
      "images and presets, and per-role font-size overrides. Separate from Font, " \
      "which is about which typeface actually resolved — a size the user chose "  \
      "and a family the platform substituted are different faults with the same " \
      "symptom of text that looks wrong")                                          \
    X(DECENZA_LOG_MARKER_EQUIPMENT,                                                \
      "Equipment packages — the grinder, basket and puck prep a shot was pulled "  \
      "on. Answers \"why does the app think I have a new grinder\" and \"where "   \
      "did my grind history go\": whether an identity edit was applied in place, " \
      "forked a new package, or merged into an existing one, plus explicit "       \
      "package merges and the one-time repair of packages an older build split. "  \
      "A fork is what detaches a grinder's shot history, so the line naming that " \
      "decision is the first thing to read when history appears to have vanished")

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

// As above, but `tag` is a runtime QString instead of a literal. Only for a
// helper that logs on behalf of SEVERAL sources — one whose own class name would
// be the wrong answer. BLEManager's refractometer tiers are the case: main.cpp
// owns the Refractometer instance, so a shared forwarder hard-coding
// "BLEManager" would stamp main.cpp's lifecycle lines with a source that never
// wrote them, and a log reader has no way to tell.
//
// Prefer the literal form everywhere else: it costs no runtime concatenation and
// a literal cannot be handed the wrong value by a caller.
#define DECENZA_SUBSYS_LOG_STDERR_DYN(marker, tag, msg, qFn) \
    qFn().noquote() << (QLatin1String("[" marker "][") + (tag) + QLatin1String("] ") + (msg))

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
// Suffix for a disconnect/error callback that fires on BOTH a link that worked
// and then dropped AND a connect attempt that never got as far as usable. The
// canonical wording above cannot tell them apart and they are different
// diagnoses; append this when `ready` is false, and nothing when it is true, so
// a normal disconnect stays the plain canonical line.
//
// Centralized because it is emitted from four callbacks across two refractometer
// drivers, and the whole reason those drivers share DECENZA_BLE_MSG_* is that a
// reader comparing two devices' logs must not have to first decide whether two
// different sentences mean the same thing.
#define DECENZA_BLE_MSG_INCOMPLETE_SUFFIX(ready) \
    ((ready) ? QString() : QStringLiteral(" (connect attempt never reached ready)"))

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
