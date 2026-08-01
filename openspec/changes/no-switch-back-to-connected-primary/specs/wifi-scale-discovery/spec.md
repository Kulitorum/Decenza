## MODIFIED Requirements

### Requirement: Saved WiFi scale reconnect resolves by service browse

When a WiFi scale is the saved primary and a reconnect attempt fails to establish a connection, the app SHALL run a DNS-SD service browse for `_decentscale._tcp.local` as part of the reconnect ladder, and SHALL auto-connect when a resolved instance matches the saved primary address.

This exists because the direct A-record query for the saved hostname has been observed to enter a state where it returns nothing at all, for hours, while the scale is plainly reachable. On Android: 82 consecutive reconnect attempts over 7.5 h each received **zero** mDNS records, while the scale was awake, mains-powered and serving WebSocket traffic on its IP; a DNS-SD browse resolved the same hostname in 362 ms, and raising the A-query deadline from 2 s to 5 s did not change the outcome.

That state is NOT a property of the responder, and this requirement must not be read as saying so. A later run on the same tablet and the same scale resolved `hds.local` by direct A-query in **357 ms — one query, one record** — and connected without any browse. The distinguishing factor was a tablet reboot between the two sessions, matching an earlier finding that mDNS discovery is reliable after a reboot and that pre-reboot failures were tablet-side stale state. Treat the failure as a host-side condition the app cannot detect or clear, not as scale or protocol behaviour.

The browse is justified by that being unrecoverable from inside the app: when the direct query returns nothing there is no signal to distinguish "stale resolver state" from "scale genuinely absent", and the browse succeeds in both readings of the first case.

The browse SHALL run on every platform. The same SYMPTOM has been seen off Android — macOS once logged `QHostInfo resolution failed for hdstest.local: Host not found` on the reconnect path while a browse resolved that host in 1.32 s — but one occurrence through a different resolver is not evidence of a shared root cause, and that macOS state did not recur. Running everywhere is a cheap hedge against a failure whose cause is not established, not a claim that every platform has the same defect. `MdnsResolver::browseService()` selects the system Bonjour backend on Apple platforms and the mjansson backend elsewhere.

The reconnect browse SHALL be isolated from user-initiated scanning:

- It SHALL NOT cancel, supersede or shorten a browse belonging to a user-initiated scan.
- It SHALL NOT set the scanning indicator, and SHALL NOT make the "Scan for Devices" control read "Scanning…".
- It SHALL NOT clear the discovered-devices list that a user scan populated.

The reconnect browse SHALL use a deadline shorter than the user-scan browse, because it repeats on the reconnect ladder rather than running once per user action.

The existing anti-substitution guarantee is unchanged: a browsed instance is auto-connected only when its address matches the saved primary.

Matching the saved primary while a scale is already connected SHALL NOT by itself cause a switch-back. A switch-back SHALL be requested only when the connected scale is a backup — that is, when the direct attempt to the saved WiFi primary has already failed and has not since been superseded by a connect to the primary. When the saved primary IS the connected scale, the browse result SHALL be a no-op: the app SHALL NOT disconnect it, SHALL NOT redial it, and SHALL NOT log that a backup scale is connected.

This is not a hypothetical ordering. Deciding "a backup is connected" from connectedness alone made every user scan disconnect and redial the healthy primary, because the same auto-connect path serves the user scan's browse and A-record probe as well as the reconnect browse. Measured on the tablet: connected at t=4.2 s, scan at t=22.3 s, two switch-back requests, torn down at t=30.9 s, reconnected to the same hostname and IP at t=32.6 s — roughly 8 s with no weight readings, repeated on every press of Scan.

#### Scenario: Stale cached IP is recovered without user action
- **WHEN** a WiFi scale is the saved primary, its cached IP no longer reaches it, and a direct hostname lookup returns no records
- **THEN** the reconnect ladder runs a service browse, the scale's current address is resolved from the browse, and the app connects to it without the user opening the Connections page

#### Scenario: Reconnect browse does not disturb a user scan
- **WHEN** the user has a scan in progress and a reconnect tick fires
- **THEN** the user's browse continues to its own deadline, its discovered rows are not cleared, and the scanning indicator reflects only the user's scan

#### Scenario: Reconnect browse is invisible in the UI
- **WHEN** a reconnect browse is running and no user scan is in progress
- **THEN** the "Scan for Devices" control remains idle and enabled, and no scanning indicator is shown

#### Scenario: A different scale on the LAN is not substituted
- **WHEN** a reconnect browse resolves a scale whose address does not match the saved primary
- **THEN** the app does not auto-connect to it

#### Scenario: No browse when no WiFi scale is saved
- **WHEN** the saved primary scale address does not begin with `"wifi:"`
- **THEN** no reconnect browse is started, and no WiFi discovery traffic is generated outside a user-initiated scan

#### Scenario: Browse is not run when the direct attempt succeeds
- **WHEN** a reconnect attempt connects successfully via the cached IP
- **THEN** no reconnect browse is started for that attempt

#### Scenario: Scanning while the saved WiFi primary is connected does not drop it
- **WHEN** the saved WiFi primary is connected and the user taps "Scan for devices", and the scan's browse and A-record probe both resolve that same primary
- **THEN** the scale stays connected throughout — no disconnect, no redial, no gap in weight readings — and no switch-back is requested by either hit

#### Scenario: Switch-back still runs when a backup is the connected scale
- **WHEN** the direct attempt to the saved WiFi primary has failed, the WiFi→BLE fallback has connected a backup scale, and a browse then resolves the saved primary
- **THEN** the app requests the switch-back, drops the backup, and connects the primary at the freshly browsed address

