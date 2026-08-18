#pragma once

#include "bluetoothlogging.h"

/**
 * The GATT queue's log helpers, defined once.
 *
 * [Bluetooth][GattQueue], not [DE1] or [Scale], for the reason
 * bluetoothlogging.h already gives about the adapter: the queue sits BENEATH
 * every device. An ordering decision that delayed the DE1 because a
 * refractometer held the slot is not a DE1 fault and not a refractometer fault,
 * and filing it under either sends a reader looking in the wrong file.
 *
 * Here rather than in blegattqueue.cpp because two files emit these lines — the
 * queue itself and the scale/refractometer transport base that submits to it —
 * and they were briefly two copies of the same three-line macro under different
 * names. That is the difluidr1/difluidr2 shape CLAUDE.md names: a one-line fix
 * to the shared form has to be found in every copy, and the copies are only
 * still identical by luck.
 */
#define GQ_LOG(msg)  BT_LOG_TAGGED("GattQueue", msg)
#define GQ_INFO(msg) BT_INFO_TAGGED("GattQueue", msg)
#define GQ_WARN(msg) BT_WARN_TAGGED("GattQueue", msg)
