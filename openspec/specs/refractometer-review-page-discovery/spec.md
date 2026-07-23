# refractometer-review-page-discovery Specification

## Purpose
Governs continuous BLE discovery of a configured refractometer while the post-shot review page is active ("hunt mode"): scans chain back-to-back from the scan-finished event so a refractometer powered on at an arbitrary moment is discovered within one scan cycle, instead of waiting out the background reconnect tick's dead window. Defines when the hunt starts, when it suspends, and how it is scoped to the page lifecycle. Discovery only — TDS reading capture is governed by `refractometer-tds-capture`.

## Requirements
### Requirement: Continuous refractometer scanning while the shot review page is active

While `PostShotReviewPage` is the active page, a refractometer is configured (saved refractometer address is non-empty), and no refractometer is connected, the system SHALL scan for BLE devices continuously: when a scan cycle finishes, the next scan SHALL be started immediately from the scan-finished event. The continuation SHALL be event-driven (no polling timer).

#### Scenario: Refractometer powered on while the review page is open

- **WHEN** the post-shot review page is active with a saved refractometer that is not connected
- **AND** the refractometer is powered on and begins advertising at an arbitrary moment
- **THEN** a scan is in progress (or starts within the turnaround of one scan-finished event)
- **AND** the refractometer is discovered during the current or immediately following scan cycle, without waiting for the 60-second background reconnect tick

#### Scenario: Page activation starts the hunt immediately

- **WHEN** the post-shot review page becomes the active page with a saved refractometer that is not connected
- **THEN** a scan is started at activation time (not deferred to the next background reconnect tick)

### Requirement: Continuous scanning suspends when it can serve no purpose

The scan-finished continuation SHALL NOT start another scan when any of the following hold: the refractometer is connected, no refractometer address is saved, BLE is disabled (simulator mode), or Bluetooth is unavailable. Scan errors SHALL NOT trigger an immediate hunt restart; recovery from a scan error is left to the existing background reconnect tick.

#### Scenario: Refractometer connects mid-hunt

- **WHEN** the hunt is active and the refractometer connects
- **THEN** the in-progress scan completes naturally and no further scan is started by the hunt
- **AND** the 60-second background reconnect cadence remains the only automatic scan driver

#### Scenario: Scan error while hunting

- **WHEN** the hunt is active and a scan ends via the scan-error path instead of the finished event
- **THEN** the hunt does not restart the scan from the error handler
- **AND** the next background reconnect tick resumes scanning as it does today

### Requirement: The hunt is scoped to the review page lifecycle

Continuous scanning SHALL end when the post-shot review page deactivates (another page is pushed on top or the page is popped) or is destroyed, and on deactivation a connected refractometer SHALL be disconnected. Off the review page the refractometer SHALL NOT be scanned for or connected: the refractometer auto-reconnect — the app-wide reconnect tick and every direct-connect path it drives — is gated on the hunt and takes no action while the review page is closed (the tick stops rather than reschedules). This scoping applies to the refractometer only; the scale is needed everywhere and keeps its own independent, always-on reconnect, which SHALL be unaffected. Manual refractometer pairing initiated from Settings SHALL NOT be gated by the hunt.

#### Scenario: Leaving the review page ends the hunt and disconnects the refractometer

- **WHEN** the hunt is active and the user navigates away from the post-shot review page
- **THEN** the in-progress scan (if any) completes and is not restarted by the hunt
- **AND** a connected refractometer is disconnected on page deactivation, not only on page destruction

#### Scenario: No refractometer connect attempts off the review page

- **WHEN** the post-shot review page is not active, a refractometer address is saved, and the refractometer is not connected
- **THEN** the app-wide reconnect tick performs no scan or connect attempt for the refractometer and stops until the review page reopens
- **AND** the scale's own reconnect continues unaffected

#### Scenario: Refractometer drops while the page is open

- **WHEN** a connected refractometer disconnects while the post-shot review page remains active
- **THEN** the hunt re-kicks the scan chain immediately, since a connected link leaves no in-flight scan for the scan-finished continuation to chain from
- **AND** from that scan's finished event onward, the hunt keeps scanning continuously until reconnection or page exit
