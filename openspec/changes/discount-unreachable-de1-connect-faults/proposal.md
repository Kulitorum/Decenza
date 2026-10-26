# Don't count a switched-off DE1 as radio contention

## Why

On 2026-10-26 the developer's SM-X210, a capable tablet, met the DE1-fault-cluster threshold four times while the DE1 was power-cycled. Each failed reconnect attempt reported `ConnectionError`, and every one counted as a link fault. The tablet runs in observe mode, so nothing latched. In enforce mode it would have persisted the BALANCED latch for a device that never had contention.

The fault feed cannot drop failed attempts outright. In the #1176 reporter's five logs, failed connect attempts are 18 to 57 of the DE1 controller errors per log against one dropped link. In the sessions that show the contention signature, the DE1 was seen advertising during the failed attempts. A switched-off DE1 never advertises.

## What Changes

- The DE1 transport reports a connection-class controller error on an attempt that never reached Connected as `connect-failed`, distinct from `controller-error` on a link that had connected.
- The scale connection-priority detector counts a `connect-failed` fault only if a DE1 was seen advertising since the previous connect attempt ended. Dropped links, write-failure cascades and zombie links count as before.
- The BLE-stack-wedge detector (#1309) still counts every kind. Its fingerprint also requires the scale to be unreachable, which a machine that is merely off does not cause.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `ble-connection-priority`: which DE1 connect failures count toward the fault cluster.

## Impact

`src/ble/bletransport.{h,cpp}`, `src/ble/blemanager.{h,cpp}`, `src/ble/transport/qtscalebletransport.cpp`, `src/main.cpp` wiring, `tests/tst_bletransporterror.cpp`. No settings, UI or persisted-state change. A latch already persisted on a device is untouched.
