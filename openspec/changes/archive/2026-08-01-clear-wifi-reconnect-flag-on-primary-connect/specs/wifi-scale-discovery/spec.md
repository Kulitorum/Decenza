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

A connect to the saved WiFi primary SHALL disarm the reconnect browse, whatever route produced that connect. In particular a connect the BROWSE itself produced SHALL disarm it, even though the WiFi→BLE fallback scan the browse was racing may still be running at that moment. Deciding this from whether a fallback was in flight, rather than from what connected, leaves the browse armed for the remainder of the session after its own successful recovery — which is the same condition "Browse is not run when the direct attempt succeeds" already forbids, reached by a different route.

While the saved WiFi primary is the connected scale, a browse or probe hit matching it SHALL be a no-op: no switch-back request, and no log line asserting that a backup scale is connected. There is no backup in that state, and the switch-back is declined one layer up regardless; the log line is the part a user sees, and it reads as the app dropping their scale.

An address obtained from a browse SHALL NOT outlive the request it was obtained for. A switch-back request may be declined (mid-shot, or already on the primary) and a declined request consumes nothing, so any later reachability check SHALL discard a previously browsed address rather than let it outrank the address that check just verified.

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

#### Scenario: The browse's own success disarms the browse
- **WHEN** a direct attempt times out, the reconnect browse and the WiFi→BLE fallback scan both start, and the browse resolves the primary and reconnects it while that fallback scan is still running
- **THEN** the reconnect browse is disarmed by that connect, and no further browse runs while the primary stays connected

#### Scenario: Re-finding the primary while it is connected is silent
- **WHEN** the saved WiFi primary is the connected scale and a browse or A-record hit matches it — including on a user-initiated scan
- **THEN** no switch-back is requested and nothing is logged claiming a backup scale is connected

#### Scenario: A declined switch-back leaves no stale address behind
- **WHEN** a browse-driven switch-back request is declined, and later a reachability check verifies the primary at its cached address
- **THEN** the switch-back that follows dials the address that check verified, not the earlier browsed one

