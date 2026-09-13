## Protocol and Provenance

The wire-format reference is the MIT-licensed [variegated-rs PORTAL driver](https://github.com/variegated-coffee/variegated-rs/tree/a9cf6e324481ee70e399839bb1f8a68ef2050086/crates/variegated-belka-portal-trouble-driver), revision `a9cf6e324481ee70e399839bb1f8a68ef2050086`. No proprietary APK, decompiled code or manufacturer assets are included. Attribution is retained in the protocol header.

Service 7400 exposes measurement characteristic 7410: exactly 13 bytes containing three finite little-endian binary32 values and one raw byte. The displayed fields are raw EC and outlet temperature; the auxiliary float and status byte retain their unresolved meaning in packet diagnostics. EC is neither refractometer TDS nor hydraulic conductance. Its units and compensation remain unverified.

Characteristic 7420 accepts `40 00 01 2D` (show graph) and `40 00 01 2E` (end graph), using writes with response. Display synchronization is optional and capability-checked. Commands are sent once at extraction start/finalization. The device display curve was confirmed by the owner; persistent sessions were not checked. Acknowledgement only reports completion of submitted BLE writes. The shared transport reports terminal operation failure, including timeout, so failed commands release the pending UI without stopping measurements.

## Connection Lifecycle

`BelkaPortalDevice` owns a dedicated existing platform BLE transport and uses the shared GATT queue. It does not request HIGH connection priority or poll characteristics. Shared discovery uses the BLE `scanStarted` event; aggregate WiFi/USB progress must not clear an already discovered PORTAL. SettingsHardware owns the saved selection and display preference. PortalController binds live settings changes in both directions, including restore while connected. Pairing is saved after measurement capability discovery; PORTAL controls stay hidden until then. The existing Scales / Refractometer heading and shared scan availability are unchanged.

Automatic reconnect is limited to one attempt per scan/idle cycle, and manual disconnect disables it until explicit reconnect or app restart. Startup reconnect piggybacks on existing discovery. A reconnect waits for the previous transport disconnect callback. New connections are blocked during machine operation; an already streaming connection stays active. Failed subscription, invalid packets, disconnect and stale data cannot be presented as current readings.

PortalController owns the capture window and busy-phase policy. A shared PortalSamples constant defines the five-second silence boundary for both freshness and curve gaps. A one-second health check marks readings stale beyond that boundary. This is conservative relative to the approximately 10 Hz cadence observed on the tested device, not a validated bound for every firmware. Controlled connection-loss and sleep/wake tests remain open.

Log entries use the PORTAL helper header. Independent event keys collapse recurring stale/recovered and automatic reconnect cycles across the selected-device episode; explicit disconnect, selection changes and teardown flush repeat counts. Raw diagnostics remain in lastPacket/packetCount instead of per-notification log lines.

## Shot Data and Rendering

Notifications are timestamped at receipt against `ShotTimingController`'s first extraction-frame origin. The capture window includes the existing stop-at-weight settling period. Samples store `{time, ecRaw, temperatureC, breakBefore}` and keep their own cadence; DE1 ticks do not fabricate samples. Invalid/out-of-order observations and interrupted streams break the next segment.

The live model, optional compressed sample blob, ShotRecord and ShotProjection retain paired samples. Live notifications append primitive points to persistent FastLineRenderer instances, creating a new pair only at a gap; EC range tracking is constant-time. The full variant list is replayed once on page entry, not per measurement. Historical rendering retains the segmented snapshot path. Live, review, history and last-shot views use the shared PORTAL overlay with separate axes and visibility settings. A dedicated row below the app status bar holds the live readout and extraction-view button; the chart starts below that row. Older shots have no PORTAL curves and ignore PORTAL visibility flags in the image-cache key. Trace colors use the standard customizable palettes and theme editors. Multi-shot comparison values remain excluded until its table supports independently timed, gap-aware samples.

Local v2 JSON export includes `decenza_portal_samples`; record import and the optional Visualizer-download extension can read the same values. The existing file picker imports Tcl files and was not extended to general v2 JSON. Visualizer uploads omit the unverified extension; it remains supported for local export and record import. Database backups carry the existing sample blobs without a schema migration; tablet restore acceptance remains open.

## Validation

Decoder fixtures cover malformed lengths, non-finite values, distinct offsets and raw status values. A manually transcribed Android diagnostics packet agrees with the EC/temperature rounding shown on that screen. Driver replay covers scan progress, subscription, display writes, busy guards, bounded reconnect and stale/late packets. Persistence tests exercise timestamp/gap retention, blob/projection/export/record-import and legacy shots. Deliberate mutations must fail before restored sources pass.

On Android, two short extractions yielded 107 and 125 stored PORTAL samples with median intervals around 98 ms; history remained available after app restart. Screenshots show the independent machine/scale and PORTAL data. This is initial hardware evidence, not completion of normal stop-at-weight, controlled radio-loss or saved-device-session validation.

The full native suite, sanitizers, Android build and repository static gates are recorded in tasks.md. Test tooling uses the owner's explicitly authorized local Qt/Android environment. No automated verification operates the physical espresso machine.
