# WiFi scale mDNS: what is measured, what is refuted, what is open

Canonical account of why `hds.local` resolution is unreliable. Every code site that
needs this story points here instead of restating it — the story was hand-copied into
eight places and had already drifted (two copies said "hours", two said "five days", for
one observation) before this file existed.

**Three mechanisms have been asserted here and all three were wrong.** Read the
retractions before proposing a fourth.

## Established by measurement

Taken 2026-08-06 from a Mac (`192.168.10.183`) against two Half Decent Scales,
`hds` (`192.168.10.145`) and `hdstest` (`192.168.10.242`). Scripts are throwaway; the
method is not, and is described under "How to measure this" below.

**1. The scale's responder is conformant.** It answers an A query sent from source port
5353 with the QU bit clear — the shape every OS resolver sends — with a correct
**multicast** reply. It also answers the `_services._dns-sd._udp.local` enumeration.
There is no responder defect.

**2. mDNS multicast on a normal LAN is lossy, for everything.** One
`_services._dns-sd._udp.local` PTR query, 21 responders replying in the same windows,
12 rounds:

| host | answered | median |
|---|---|---|
| 192.168.10.20 | 12/12 (100%) | 185 ms |
| 192.168.10.244 | 12/12 (100%) | 360 ms |
| 192.168.10.238 | 9/12 (75%) | 104 ms |
| **192.168.10.145 (hds)** | **9/12 (75%)** | **330 ms** |
| 192.168.10.184 | 8/12 (66%) | 358 ms |
| **192.168.10.242 (hdstest)** | **5/12 (41%)** | **267 ms** |
| 192.168.10.194 | 5/12 (41%) | 330 ms |

Both scales sit mid-pack on rate and on latency; non-scale hosts occupy the same 41%,
66% and 75% buckets. **The scales are not outliers on anything.**

**3. Unicast replies survive where multicast replies do not.** Same scales, same
session, by query shape:

| query shape | reply travels as | rate |
|---|---|---|
| ephemeral source port (RFC 6762 §6.7 "legacy") | unicast | ~11/12 |
| source port 5353, QU bit set | unicast | 5/6 |
| source port 5353, QM — what every OS resolver sends | multicast | 12/20, 16/20 |

A unicast reply rides the 802.11 unicast path: acknowledged, retried by the AP, sent at
a negotiated rate. A multicast reply is fire-and-forget at a low basic rate, no ack, no
retry. **That is why the app queries from an ephemeral source port** — it is the only
shape whose reply is acked and retried, and it is a real reason, not a workaround.

## Retracted

**WiFi power save / modem sleep.** Taken from a comment in the openscale firmware, never
measured. The scale answers unicast queries in ~30 ms with no wake-up penalty.

**"The scale never sends a multicast reply."** Measured, and the measurement was
invalid. The probe socket bound a *specific* local address (`192.168.10.183:5353`); on
BSD such a socket is not guaranteed to receive datagrams addressed to `224.0.0.251`.
Unicast replies arrived fine, so the socket looked healthy while dropping every
multicast reply. Re-run with `INADDR_ANY` and `IP_RECVDSTADDR`, the replies were there.

**The ARP mechanism** — that a legacy query must be answered by unicast, so the scale
must ARP for the querier, and with `ETHARP_TRUST_IP_MAC` off it cannot learn our MAC
from the query frame.

Note the retraction that does NOT work, because it was published here first and is
wrong: "the scale answers multicast queries multicast, needing no ARP." True, and
irrelevant — the ARP account was only ever about the legacy/unicast path, so refuting it
with a QM-from-5353 measurement tests the wrong query shape.

What actually refutes it is below: one TCP connection to **one** scale restored
resolution for **both** scales. No mechanism keyed to a per-peer MAC cache can do that.

**"Resolution is per-peer: a scale answers a device that has dialled it and stays silent
to one that has not."** Refuted by the same experiment, for the same reason.

## The tablet is dormant, and outbound traffic wakes it

Android tablet at `192.168.10.163`, `hds` contacted, `hdstest` deliberately untouched as
a control:

| | before TCP | after one TCP connect to hds only |
|---|---|---|
| `hds.local` | 0/10 | 10/10 |
| `hdstest.local` (never contacted) | 0/10 | 9/10 |

The control flipped too, so whatever changed is on the tablet and applies to every peer
at once. Independently, the tablet was the worst mDNS responder of the 21 hosts measured
above — 1/12 at a 1253 ms median, against 41-100% for everything else — so its receive
path is degraded in both directions, which is what a station sleeping between DTIM
beacons looks like while the AP buffers for it. `dumpsys wifi` on that tablet reports
`Locks acquired: 0 full high perf, 0 full low latency`.

This is a WiFi power-save effect on the **tablet**, not on the scale. The scale-side
power-save theory in the retractions above is a different claim about a different device
and remains refuted.

It also subsumes what the per-peer story was invented to explain: a tablet that has just
dialled a scale resolves it, a tablet that has not resolves nothing at all, and a reboot
"fixes" it because a reboot is followed by traffic.

## Open

**How long the wake lasts.** Sets the shape of the fix: a wake lasting minutes means a
`WifiLock` held for the duration of a browse is enough, while one decaying in seconds
means resolution has to carry its own traffic.

**Whether a `WIFI_MODE_FULL_HIGH_PERF` lock is sufficient.** Untested. It is the obvious
candidate given that none is currently acquired, but "obvious candidate" is exactly the
status the three retracted mechanisms each had.

**Whether dialling the cached IP "repairs" mDNS.** Yes, but not for the reason long
believed — it is not per-peer repair, it is the tablet waking. Keep the cached IP
regardless: silence says nothing about whether the address is right.

## What this means for the code

- Query from an ephemeral source port. It is the only shape whose reply is acked and
  retried by the AP. This is measured, not defensive.
- Retry. A single query with a short timeout fails 25-60% of the time on an ordinary
  network, and no amount of correctness elsewhere changes that.
- Never evict a cached IP on silence.
- Do not blame the scale firmware. Two mechanisms were nearly filed as firmware bugs.
- On Android, assume the device's own receive path is asleep until it has sent
  something. A discovery pass that only listens can return empty on a LAN full of
  scales, and will do so *deterministically*, not occasionally.

Improving the AP (multicast-to-unicast conversion) lifts every host in that table, but
users' networks are not ours to fix — the code has to work at 41%.

## How to measure this

The method matters more than the scripts, because two of the three retractions above
were measurement errors rather than reasoning errors:

- **Listener on `INADDR_ANY`**, joined to the group, classifying each packet by
  `IP_RECVDSTADDR` ancillary data. A specific-address bind cannot see multicast.
- **A control host in the same window.** One `_services._dns-sd._udp.local` PTR query is
  answered by every responder on the segment, so rates and latencies are directly
  comparable and the network path is held constant. This is what exonerated the scales;
  without it, "75% and 330 ms" reads as damning instead of typical.
- **Enough rounds to distinguish a rate from an outcome.** Anything under ~10 rounds on
  a 41% path tells you nothing, which is how the per-peer claim survived as long as it
  did.
- Never `ping` to test reachability — the scale drops ICMP independently of real
  traffic, and a diagnostic ping repairs the ARP state you would be trying to observe.
