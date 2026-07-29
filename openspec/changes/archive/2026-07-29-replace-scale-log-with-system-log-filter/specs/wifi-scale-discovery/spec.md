## MODIFIED Requirements

### Requirement: Only resolved browse instances are shown

A DNS-SD browse returns service instance names, some of which are stale registrations that never resolve — on a network with two live scales, four instances were observed, of which two never resolved within 10 seconds and did so persistently across repeated browses.

An instance SHALL therefore appear in the discovered-scales list only after its SRV and address records resolve successfully within a named per-instance resolve deadline. An instance that fails to resolve SHALL be dropped, not rendered as an unselectable or failing row. The resolve deadline is independent of the A-record fallback's ~5 second window.

Within a scan cycle the discovered-devices list SHALL be add-only: a row that has been shown to the user SHALL NOT be removed before the scan ends. Keeping ghosts out is the job of the resolve gate above, not of retracting a row the user has already seen. Withdrawal callbacks arriving mid-scan SHALL be logged and otherwise ignored; the list is rebuilt on the next scan, which is where a departed device disappears.

This does not apply to a USB scale being physically unplugged, which removes its row immediately as it does today — that is a device going away, not discovery churn.

#### Scenario: Stale instance that never resolves
- **WHEN** the browse returns an instance name whose SRV/address resolution does not complete within the resolve deadline
- **THEN** no row appears for that instance, and the scale narrative in the system log records the instance name and that it was dropped at resolve

#### Scenario: Instance withdrawn mid-scan
- **WHEN** an instance is listed and the system resolver subsequently withdraws it because nobody answered a re-query
- **THEN** its row remains in the list for the rest of the scan cycle, and the withdrawal is recorded in the scale narrative only

#### Scenario: Departed device disappears on the next scan
- **WHEN** a scale that was listed in the previous scan is no longer present and the user scans again
- **THEN** the list is rebuilt without it

#### Scenario: USB scale unplugged
- **WHEN** a connected USB scale is physically unplugged
- **THEN** its row is removed immediately, whether or not a scan is in progress

#### Scenario: Live instances alongside stale ones
- **WHEN** a browse returns four instances of which two resolve to live scales
- **THEN** exactly two rows appear in the discovered-scales list

### Requirement: Discovery diagnostics cover the browse

The scale narrative in the system log SHALL record what the browse did, at the same level of detail the existing A-record probe records, so a user-shared log explains a discovery failure without needing a console. These records are part of the user-facing narrative — they are what a discovery complaint is diagnosed from — so they SHALL be logged at INFO or above and therefore appear in the connections page's scale view as well as in the shared log.

#### Scenario: Browse produces results
- **WHEN** a browse completes and returns one or more services
- **THEN** the scale narrative records the service type queried, the number of results, and each result's instance name, host, address and firmware version

#### Scenario: Browse returns nothing
- **WHEN** a browse completes with no results
- **THEN** the scale narrative records that the browse ran and found nothing, distinguishably from the browse not having been attempted

#### Scenario: Fallback names are logged individually
- **WHEN** the multi-name A-record fallback runs
- **THEN** the scale narrative records the outcome for each of `hds.local`, `hds-2.local` and `hds-3.local` separately, so a partial result is diagnosable

#### Scenario: A lookup could not be performed at all
- **WHEN** the query socket cannot be opened, or every query send fails
- **THEN** the log says so specifically rather than reporting "no responder", because those have opposite fixes — a local permission or multicast problem versus a sleeping or absent scale

#### Scenario: A diagnostic reads an empty result set
- **WHEN** a diagnostic surface reports the results of the most recent discovery and there are none
- **THEN** it also reports whether each transport actually ran, so "permission denied" is not indistinguishable from "no scales on this network"
