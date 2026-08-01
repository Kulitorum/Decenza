# Do not switch back to a primary that is already connected

## Why

The browse-driven switch-back added in `browse-for-wifi-scale-reconnect` decides
that the connected scale is a *backup* from `m_scaleDevice->isConnected()` alone.
That test is true just as often when the connected scale IS the saved primary,
and `maybeAutoConnectBrowsedScale()` is called from the user scan's handlers as
well as the reconnect browse's. So a user scan that re-finds a perfectly healthy
primary asks to switch back to it, and `switchToWifiPrimary()` disconnects the
scale and redials the same hostname at the same IP.

Observed on the tablet, 2026-08-01 (session start 11:17:55):

```
[  4.154] WebSocket connected — peer=192.168.10.145:80
[ 22.273] Starting device scan...
[ 22.636] Reconnect browse found the saved primary hds.local at 192.168.10.145
          while a backup scale is connected — requesting switch-back
[ 22.683] (again — once for the DNS-SD hit, once for the mDNS hit)
[ 30.897] WebSocket disconnected (expected)
[ 32.494] Auto-reconnect attempt 1 → connected to the same scale
```

The user loses the scale for ~8 s each time they press Scan, and the log line
blames a "backup scale" that does not exist. Weight readings stop for that
window; a scan during a shot would take the scale away mid-pour.

## What Changes

- The switch-back is gated on evidence that the connected scale is actually a
  backup, not merely on something being connected. `m_wifiDirectAttemptFailed`
  already carries exactly that meaning: it is set only when the direct attempt
  to the saved WiFi primary times out, and cleared by any connect that is not
  the WiFi→BLE fallback.
- When the primary is the connected scale, the browse result is a no-op — no
  switch-back and no `scaleDiscovered` emission.
- The log line drops "Reconnect", which was wrong for the two scan-side callers.

No user-facing setting, no new state, no change to the reconnect ladder itself.
The genuine backup case — direct attempt timed out, WiFi→BLE fallback connected,
browse then finds the primary — is unchanged.

## Impact

- Affected specs: `wifi-scale-discovery`
- Affected code: `src/ble/blemanager.h`, `src/ble/blemanager.cpp`,
  `tests/tst_wifiscalediscovery.cpp`
