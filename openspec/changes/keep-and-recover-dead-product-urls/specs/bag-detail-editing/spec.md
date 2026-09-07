## MODIFIED Requirements

### Requirement: A product URL can be added or corrected

The Bean details section SHALL include the product URL (`link`). When saved, the URL SHALL feed the existing bag-image resolution (`og:image` fetch and file cache) and the details popup's open-at-roaster affordance.

A write that changes `link` SHALL re-open the link check for the new URL, whatever wrote it — the
user typing one, a confirmed AI suggestion, or a restore of the Bean Base data. The marks that
describe a link's state (`linkChecked`, `linkDead`) describe ONE URL, so a write that replaces the
URL SHALL drop them; leaving them standing describes the old URL and silently exempts the new one
from ever being probed or archive-recovered. For the same reason the once-per-run guards on link
validation and on the archive lookup SHALL be keyed by URL, not by bag: their purpose is to stop
asking the same question about the same URL twice, and a bag whose URL has changed is a different
question.

A URL found dead SHALL be RETAINED on the bag, not removed. The user typed it, or the Bean Base
record supplied it, and it is the only record of where the bag came from; deleting it destroys
data on a manual bag, where no `canonical` snapshot exists to re-derive it from. Consumers SHALL
therefore gate on whether the link is USABLE rather than on whether the key is present: a dead
link SHALL NOT drive photo resolution and SHALL NOT be offered to "Get info from page".

The details popup SHALL still show a retained dead URL, marked as no longer resolving. Hiding it
would be indistinguishable from having deleted it, and the roaster's page may return. Once the URL
is recovered or replaced the marking SHALL disappear and the row SHALL render as any working link.

#### Scenario: Adding a URL to a bag without one
- **WHEN** the user enters a product URL for a bag whose blob has no `link` and saves
- **THEN** the blob SHALL carry the URL
- **AND** bag-image resolution SHALL be attempted for the bag using the new URL

#### Scenario: A dead URL the user typed is kept
- **WHEN** a manual bag's product URL is found dead and the archive has no capture of it
- **THEN** the URL SHALL remain on the bag, marked dead
- **AND** it SHALL be shown in the details popup marked as no longer resolving

#### Scenario: A dead link drives no photo fetch
- **WHEN** a bag's retained `link` is marked dead
- **THEN** no bag-image resolution SHALL be attempted for it
- **AND** "Get info from page" SHALL NOT be offered for it

#### Scenario: Restoring the Bean Base data restores a dead URL
- **WHEN** the user reverts a bag to its Bean Base data and the canonical record's URL is dead
- **THEN** the restored URL SHALL be probed
- **AND** it SHALL be replaced by an archive snapshot when one exists, or marked dead when none does

#### Scenario: A new URL is probed even though the old one was checked
- **WHEN** a bag already marked as link-checked has its `link` replaced with a different URL
- **THEN** the mark SHALL be dropped and the new URL SHALL be probed

#### Scenario: A bag whose URL changed can ask the archive again
- **WHEN** a bag's URL was already looked up in the archive this run, and the bag's `link` is then
  replaced with a different URL that turns out to be dead
- **THEN** the archive SHALL be asked about the new URL

#### Scenario: The same URL is not re-probed
- **WHEN** a bag's `link` is rewritten to the value it already held
- **THEN** no additional link check or archive lookup SHALL be issued

### Requirement: A dead product URL is replaced by its most recent working form

`link` SHALL mean "the most recent URL known to serve this bag's product page". EVERY bag holding
a URL SHALL be subject to the once-per-bag link check, whether or not it is linked to a Bean Base
record: a URL the user typed on a manual bag can be retired by its roaster exactly as a canonical
one can, and a bag that is never checked never gets a photo again and is never told why. The check
and the signals that answer it SHALL therefore be keyed by the BAG — its canonical id when it has
one, otherwise the same `bag-<rowid>` key its photo cache already uses — rather than by a canonical
id that a manual bag does not have.

When the check finds the stored URL dead, it SHALL query the Internet Archive for a snapshot of
that URL before treating the link as lost. When a snapshot exists, the snapshot URL
SHALL replace `link` and the bag SHALL NOT be marked dead. No additional blob key SHALL be
introduced for the archived form — every consumer of `link` (photo resolution, "Get info from
page", the open-at-roaster affordance) SHALL use the recovered URL exactly as it used the original.

**Replacing a dead URL with its archived copy is the outcome to reach whenever it is reachable at
all.** A dead verdict is therefore not final: the retained URL is itself the retry source, so the
archive SHALL be asked again for a bag holding a link marked dead — a MANUAL bag included, which
today can never recover because the retry reads `canonical.link` and a manual bag has none. A retry
that succeeds replaces the link and clears the mark.

The retry SHALL run when the bag is USED — when it is the active bag — and NOT when its card is
merely drawn. A retryable link has no persisted marker to settle it the way `linkChecked` settles
the once-per-bag link check, so a retry on card construction would query the archive for every dead
bag every time the inventory is shown. The URL matters when the user reaches for that bag, and that
is when the cost belongs.

**The link check's own result decides the mark; the archive only ever upgrades it.** A 404 or 410
from the roaster is proof the URL no longer serves the page, so it SHALL set the dead mark on its
own. The archive is then asked one question — can this be replaced with a capture — and only a
capture changes anything. Every other outcome, an empty envelope included, SHALL mean "no
replacement yet" and SHALL leave the bag marked dead and retryable.

The availability API SHALL NOT be asked to decide absence, because it cannot express it: it answers
a URL it never archived and a URL it cannot look up right now with the same empty `archived_snapshots`
envelope, and was observed returning that envelope with HTTP 200 for a URL whose capture fetched
successfully in the same session. Making the dead mark depend on telling those apart let a degraded
service settle a bag's fate.

Reading a page for extraction SHALL reach the archive on its own, not only through the link check.
When the page fetch for extraction finds the URL gone, it SHALL query the Internet Archive for a
snapshot of that URL and, when one exists, extract from the snapshot instead of failing. This holds
however the URL reached the field — restored from the Bean Base record, typed by the user, or
suggested by the AI — because a URL that never passed the once-per-bag link check would otherwise
have no route to the archive at all.

A link already pointing at an archive snapshot SHALL NOT be re-probed or re-recovered: it is
terminal, and a snapshot that later becomes unreachable leaves the bag as it would have been.

#### Scenario: A manual bag's URL is checked like any other
- **WHEN** a bag with no `canonical` snapshot holds a product URL
- **THEN** that URL SHALL be link-checked once, as a Bean-Base-linked bag's URL is
- **AND** a dead result SHALL mark it and attempt archive recovery

#### Scenario: Delisted product recovers its page
- **WHEN** a bag's stored product URL returns 404 and the Internet Archive holds a successful
  snapshot of it
- **THEN** the bag's `link` SHALL become the snapshot URL
- **AND** the bag SHALL NOT be marked as having a dead link

#### Scenario: No snapshot exists
- **WHEN** a bag's stored product URL is dead and the Internet Archive has no successful snapshot
- **THEN** the bag SHALL be marked dead
- **AND** the URL SHALL be retained so a later run can ask again

#### Scenario: A manual bag recovers when it is next used
- **WHEN** a bag with no `canonical` snapshot holds a link marked dead, is selected as the active
  bag, and the archive answers with a capture of it
- **THEN** the link SHALL become the snapshot URL and the dead mark SHALL be cleared

#### Scenario: Scrolling past a dead bag costs nothing
- **WHEN** the bag inventory is shown and it contains bags holding links marked dead
- **THEN** no archive lookup SHALL be issued for a bag that is not the active one

#### Scenario: A degraded availability answer marks nothing dead
- **WHEN** the availability API answers with an empty `archived_snapshots` envelope
- **THEN** the bag SHALL NOT be marked dead on the strength of that answer
- **AND** a bag already marked dead SHALL remain retryable rather than being treated as settled

#### Scenario: A recovered link is not probed again
- **WHEN** a bag whose `link` is already an archive snapshot is displayed
- **THEN** no further link check or archive lookup SHALL be issued for it

#### Scenario: Picking a Bean Base entry whose URL is stale
- **WHEN** the user picks a Bean Base entry whose product URL is dead, so the photo attempt made at
  pick time fails
- **THEN** the resulting bag SHALL still recover through the archive
- **AND** photo resolution SHALL be re-attempted against the recovered URL, rather than being
  suppressed by the failed attempt already made for that bag this session

#### Scenario: Extraction works from the recovered page
- **WHEN** a bag whose `link` is an archive snapshot and an AI provider is configured
- **THEN** "Get info from page" SHALL be offered and SHALL extract from the snapshot's page text
  under the same apply rules as a live page

#### Scenario: Get info recovers a URL the link check never saw
- **WHEN** the user presses Get info on a URL that is not stored on the bag and that returns 404,
  and the Internet Archive holds a successful snapshot of it
- **THEN** the extraction SHALL proceed from that snapshot
- **AND** the user SHALL NOT be told the page could not be read

#### Scenario: Get info on a dead URL with no snapshot
- **WHEN** the page fetch for extraction finds the URL gone and the archive has no capture of it
- **THEN** the failure SHALL surface as an inline status message naming the page as unreadable
