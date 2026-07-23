## MODIFIED Requirements

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
