## Why

Belka PORTAL measures coffee conductivity and outlet temperature, but Decenza currently requires a separate application to view them. Add both readings to the brewing timeline and retain them with the shot.

## What Changes

- Discover and connect PORTAL through the existing Scales / Sensors panel, independently of the machine, scale and refractometer.
- Show raw EC and outlet temperature during extraction and in shot history, with independent axes, advanced-mode legend toggles and explicit gaps when measurements stop.
- Preserve paired samples in the existing optional shot blob and local export/record-import paths.
- Optionally synchronize PORTAL's display graph with extraction. Acknowledged commands do not claim device-side session persistence.

## Capabilities

### New Capabilities

- `belka-portal`: continuous raw EC and outlet-temperature measurements during a shot.

### Modified Capabilities

None. The new capability owns its connection, display and optional sample-storage requirements.

## Impact

BLE discovery/transport, shot timing, live/history models, QML connection and graph views, serialization, record import/export and tests. The existing database schema and app identity remain unchanged. PORTAL does not control the machine, scale, brew profile or stop-at-weight.

The contribution is based on Decenza 2.0.5/build 3591. Private test-app packaging, signing keys, backups and machine settings are excluded. Initial Android hardware validation confirms connection, live curves, reopened history and the graph on the PORTAL display; broader acceptance remains explicitly tracked in tasks.md.
