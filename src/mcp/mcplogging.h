#pragma once

#include "core/logtags.h"

#include <QDebug>
#include <QString>

// Shared logging helpers for the MCP server — sessions, the HTTP/SSE transport,
// tool dispatch, remote access and the tunnel.
//
// Every line gets the [MCP] marker from the registry (core/logtags.h) plus a tag
// naming its own source:
//
//     [MCP][Server] session 1868a90c initialized, protocol 2025-11-25
//     [MCP][RemoteAccess] rejected request from unauthorized origin
//
// so one search on [MCP] returns the whole story from a submitted log. Read
// core/logtags.h for why the marker exists and what the tiers mean; this header
// is just the MCP subsystem's instance of it.
//
// ---- Choosing a helper --------------------------------------------------
//
//   MCP_LOG_TAGGED    qDebug   developer detail
//   MCP_INFO_TAGGED   qInfo    the user-facing narrative
//   MCP_WARN_TAGGED   qWarning problems
//
// Pick by AUDIENCE, not by how important the event feels. `debug_get_log` with
// minLevel INFO shows exactly the INFO-and-above set, so:
//
//   - Per-request parsing, header extraction, keepalive ticks -> MCP_LOG (DEBUG).
//   - A session opening or being terminated, a client connecting or dropping an
//     SSE stream, a confirmation the user answered      -> MCP_INFO.
//   - A refusal or a loss: rate limit, access level, denied or superseded
//     confirmation, a response dropped because the socket went away, a write
//     that failed                                       -> MCP_WARN.
//
// The refusals matter more here than in most subsystems. When the server refuses
// a call, the ASSISTANT is told and the user usually is not — so the log line is
// the only thing that explains why the machine did nothing. A refusal left at
// DEBUG is invisible in the view that would answer the question.
//
// ---- Mechanics ---------------------------------------------------------
//
// These are the stderr-only variants: no class in src/mcp carries a
// `logMessage(QString)` signal, so there is nothing to emit to. If one ever
// gains one, switch that file to DECENZA_SUBSYS_LOG rather than hand-rolling the
// pairing.
//
// `tag` is a string LITERAL naming the source. Alias these per file rather than
// copying a body — usbscalemanager.cpp hand-rolled its prefix at 73 sites and
// drifted at 21 of them:
//
//     #define MCPSRV_WARN(msg) MCP_WARN_TAGGED("Server", msg)

#define MCP_LOG_TAGGED(tag, msg)  DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_MCP, tag, msg, qDebug)
#define MCP_INFO_TAGGED(tag, msg) DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_MCP, tag, msg, qInfo)
#define MCP_WARN_TAGGED(tag, msg) DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_MCP, tag, msg, qWarning)
