# Tasks — cold Transport starts

- [x] Compare the existing handler with Decaid revision 94501c455d81fa9a31b4ce165bb0b0777e7beec6, unified_de1.dart:339-400. Keep the shared firmware boundary at 1356 and Decenza's event-driven preparation wait.
- [x] Allow Start while connected and idle/heating/ready; retain simulation and block sleep, disconnection and other operations.
- [x] Cancel deferred AirPurge before leaving/covering Transport or notifying observers of a competing device state. Keep cancellation idempotent, preserve other pending maintenance requests, and clear deferred intent on disconnect.
- [x] Restore the selected profile on leaving/covering Transport, deferring until the device is idle. Steam warm-up reported as Heating must not permit an upload.
- [x] Treat unread GHC status conservatively for cold preparation; retain confirmed no-GHC direct starts and reset hardware confirmation on disconnect.
- [x] Test firmware/hardware eligibility, preparation ACK/release, cancellation ordering, later explicit Start, disconnect cleanup and selected-profile restoration through MockTransport.
- [x] Update main/delta specs and publish the short Maintenance manual entry (wiki b1947f0); verify rendered wording.

## Validation

The matched desktop simulator (PID 79623) showed Start, Stop returning to preparation, natural completion after 12 seconds, and Done returning to Settings. The selected profile remained D-Flow / Q. This does not establish a real preparation-profile/restore pair or prove a physical drain.

Qt Creator run 1791394637075 passed 121/121 in 28.17 s without failures, skips or test warnings. All 180 specs/changes pass strict validation; source text checks pass. Tests retain firmware/hardware coverage and representative cancellation/restoration cases without repeating the firmware-by-operation matrix.

## Scope and remaining evidence

The hardware-button espresso overlap guard and its tests/spec/manual requirements were removed at the user's request because deliberate overlap does not justify additional brew/upload state. No physical DE1 was drained; real-machine observation remains a post-deployment follow-up.
