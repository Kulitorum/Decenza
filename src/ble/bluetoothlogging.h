#pragma once

#include "core/logtags.h"

#include <QDebug>
#include <QString>

// Logging helpers for the local Bluetooth RADIO, as distinct from the devices on it.
//
//     [Bluetooth][BLEManager] Adapter recovered — re-arming DE1 + scale reconnect
//     [Bluetooth][Capability] hcitool present but lacks CAP_NET_RAW
//
// Why a marker of its own rather than folding these into [Scale] or [DE1]: the
// adapter is BENEATH both. When it is powered off or wedged, neither device can
// connect, and filing that under one of them sends a reader looking for a fault in
// the wrong place — a user whose radio is off does not have a scale problem.
//
// It also earns its own grep. "Is my Bluetooth working?" is a different question
// from "did my scale connect?", and answering the first used to mean reading
// 31 lines that all began `qDebug() << "BLEManager: ..."` — a hand-rolled prefix
// that matched no registered marker, so neither device search returned them.
//
// Tiers, by the usual audience rule (see core/logtags.h):
//
//   BT_LOG   (DEBUG) host-mode changes, per-attempt recovery state, probe detail
//   BT_INFO  (INFO)  the adapter came back, a capability is present, a scan was
//                    refused because the radio is off — things that explain the
//                    app's behaviour to the person using it
//   BT_WARN  (WARN)  the adapter is off and we could not turn it on, the stack
//                    looks wedged, a platform capability is missing
//
// STDERR variants only: nothing in this subsystem has a logMessage signal, and it
// needs none — the marker is what makes these findable, in the connections views
// and in a submitted log.
#define BT_LOG_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_BLUETOOTH, tag, msg, qDebug)
#define BT_INFO_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_BLUETOOTH, tag, msg, qInfo)
#define BT_WARN_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_BLUETOOTH, tag, msg, qWarning)
