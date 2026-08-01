# Clear the WiFi reconnect flag when the primary is what connected

## Why

`m_wifiDirectAttemptFailed` gates two things: whether the reconnect browse runs
(`shouldBrowseOnReconnect`) and whether a browse hit asks for a switch-back. It
is meant to be cleared the moment a connect proves the primary is back.

It is not cleared on the browse's own success path, which is the path the browse
feature exists to produce:

1. The direct dial to the saved WiFi primary times out. `onScaleConnectionTimeout()`
   sets the flag, starts the reconnect browse, then `beginWifiFallbackToBleScan()`
   sets `m_wifiFallbackToBleActive` and starts a BLE scan.
2. The browse resolves the primary. Nothing is connected yet, so
   `maybeAutoConnectBrowsedScale()` dials it via `scaleDiscovered` — **without**
   clearing `m_wifiFallbackToBleActive`, because the BLE scan it is racing is
   still running.
3. The primary connects. `onScaleConnectedChanged()` reads
   `wasWifiFallbackConnect == true` and declines to clear the flag — even though
   the scale that just connected is the primary itself.

Nothing clears it afterwards while that scale stays connected: there is no
further connect, and `setSavedScaleAddress()` is not called because the address
did not change. So a successful recovery leaves the machinery believing the
direct attempt is still failing, for the rest of the session.

This already contradicts a requirement the spec states — "Browse is not run when
the direct attempt succeeds" — for the specific case where the attempt succeeded
*because of* the browse.

Second, smaller defect on the same path: `maybeAutoConnectBrowsedScale()` treats
"a scale is connected" as "a backup is connected". When the connected scale is
the primary, it logs `"found the saved primary … while a backup scale is
connected — requesting switch-back"` — naming a backup that does not exist and a
switch that does not happen, since `main.cpp`'s `onWifiBackupAndIdle()` declines
the request at its `decent-wifi` test. This subsystem is diagnosed from
user-submitted logs, where that line reads as the app dropping the user's scale.

## What Changes

- Clearing the flag asks what actually connected, not whether a fallback was in
  flight. A connect to the WiFi primary clears it even when the fallback scan is
  still running.
- `BLEManager::connectedScaleIsWifiPrimary()` answers that question with the same
  `type() == "decent-wifi"` test `main.cpp:2465` already applies, so the two
  layers cannot disagree about what "on the primary" means.
- A browse hit that finds the primary while the primary is already connected is a
  no-op — no switch-back request, no log line claiming a backup.
- `m_browsedPrimaryIp` is cleared at the start of `probeWifiPrimaryReachable()`.
  It is set before a request `main.cpp` may decline, and a declined request
  consumes nothing, so a stale browsed address could outrank an IP a later probe
  had just verified.

Deliberately NOT done: clearing `m_wifiFallbackToBleActive` at the browse's dial.
That flag also gates whether an in-flight fallback scan may adopt a BLE scale and
whether a second fallback is permitted, so clearing it there would change the
reconnect ladder rather than the one belief that is wrong.

## Impact

- Affected specs: `wifi-scale-discovery`
- Affected code: `src/ble/blemanager.h`, `src/ble/blemanager.cpp`,
  `tests/tst_wifiscalediscovery.cpp`
