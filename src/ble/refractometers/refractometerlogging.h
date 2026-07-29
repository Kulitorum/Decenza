#pragma once

#include "core/logtags.h"

#include <QDebug>
#include <QString>

// Logging helpers for the DiFluid R1/R2 refractometers.
//
//     [Refractometer][BLE DiFluidR2] measurement 8.42 %TDS
//
// Why a marker of their own, when they run on the scale BLE transports and are
// listed in the same connections view: they answer a different question. A
// refractometer problem ("my TDS readings are wrong") and a scale problem ("my
// weight is wrong") are diagnosed from different lines, and mixing them meant a
// [Scale] search returned both and neither could be read on its own. The
// transports keep their own [Scale] marker — they are scale plumbing that the
// refractometers borrow — so a refractometer investigation reads
// [Refractometer] for the instrument and [Scale] for the link beneath it.
//
// Tier choice is the same rule as everywhere: audience, not authorship. See
// core/logtags.h. In practice for these devices:
//
//   R_LOG   (DEBUG) packet framing, checksum detail, raw payloads
//   R_INFO  (INFO)  connected, disconnected, a completed measurement
//   R_WARN  (WARN)  no liquid detected, beyond range, decode rejected
//
// INFO and above is what the connections view shows and what
// `debug_get_log` returns for minLevel INFO.
//
// These require `emit logMessage(QString)` in scope, i.e. use them inside the
// RefractometerDevice subclass. Alias them per driver rather than copying a
// body — the shape lives in core/logtags.h.

#define REFRACTOMETER_LOG_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_REFRACTOMETER, tag, msg, qDebug)
#define REFRACTOMETER_INFO_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_REFRACTOMETER, tag, msg, qInfo)
#define REFRACTOMETER_WARN_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_REFRACTOMETER, tag, msg, qWarning)

// Stderr-only variants, for refractometer code with no logMessage to emit.
#define REFRACTOMETER_LOG_STDERR_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_REFRACTOMETER, tag, msg, qDebug)
#define REFRACTOMETER_INFO_STDERR_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_REFRACTOMETER, tag, msg, qInfo)
#define REFRACTOMETER_WARN_STDERR_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_REFRACTOMETER, tag, msg, qWarning)

// Runtime-tag variants, for a forwarder that logs on behalf of more than one
// source (see DECENZA_SUBSYS_LOG_STDERR_DYN). BLEManager's refractometer tiers
// use these so a line main.cpp wrote is not stamped "BLEManager".
#define REFRACTOMETER_LOG_STDERR_DYN(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR_DYN(DECENZA_LOG_MARKER_REFRACTOMETER, tag, msg, qDebug)
#define REFRACTOMETER_INFO_STDERR_DYN(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR_DYN(DECENZA_LOG_MARKER_REFRACTOMETER, tag, msg, qInfo)

// BLE-driver shorthand, matching SCALE_LOG's "BLE <x>" tagging.
#define REFRACTOMETER_LOG(prefix, msg)  REFRACTOMETER_LOG_TAGGED("BLE " prefix, msg)
#define REFRACTOMETER_INFO(prefix, msg) REFRACTOMETER_INFO_TAGGED("BLE " prefix, msg)
#define REFRACTOMETER_WARN(prefix, msg) REFRACTOMETER_WARN_TAGGED("BLE " prefix, msg)
