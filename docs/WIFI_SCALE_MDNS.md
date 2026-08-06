# WiFi scale mDNS: what is measured, what is refuted, what is open

Canonical account of why `hds.local` resolution is unreliable. Every code site that
needs this story points here instead of restating it — the story was hand-copied into
eight places and had already drifted (two copies said "hours", two said "five days", for
one observation) before this file existed.

**Four accounts have been asserted in this file. Two were wrong, one was retracted
twice and then restored by controlled measurement.** Read the retractions, and the two
controls under "Established", before proposing a fifth.

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

**"Tablet-side WiFi power save; outbound traffic wakes the receive path for all peers."**
Asserted here briefly on the strength of one run in which a TCP connect to `hds` lifted
an untouched `hdstest` from 0/10 to 9/10. Refuted by the control that run lacked — see
below. That `hdstest` reading is best explained by a still-valid ARP entry on that scale
from earlier in the session, which later aged out.

## Established: resolution is per-peer, and outbound IP traffic to THAT scale enables it

Android tablet `192.168.10.163`, cold, with a gateway control and a scale control:

| step | `hds.local` | `hdstest.local` |
|---|---|---|
| cold | 0/6 | 0/6 |
| after TCP to the **gateway** (`192.168.10.1`, connect succeeded) | 0/6 | 0/6 |
| after TCP to **hds** (connect *failed*, rc=1) | 4/6 | 0/6 |
| after TCP to **hdstest** (connect *failed*, rc=1) | 6/6 unchanged | 5/6 |

Read the two controls, because each excludes a different explanation:

- **The gateway step excludes anything device-wide.** Successful outbound unicast, and
  resolution did not move. Whatever changes is not "the tablet's radio woke up".
- **The other scale, in both directions, excludes anything network-wide or temporal.**
  Touching `hds` left `hdstest` at 0/6; touching `hdstest` left `hds` at 6/6.
- **A FAILED connect is sufficient** (`rc=1` both times). Nothing at the TCP layer
  completed, so the payload cannot be what matters — only the packets sent trying.

## Mechanism: strongly supported, still an inference

An outbound IP packet to the scale makes the *tablet* ARP for it; the scale answers that
request and caches the tablet's MAC. Until then the scale cannot address the tablet, and
a legacy (ephemeral source port) query — which is what Android's resolver sends, verified
on the wire at source ports 57320, 42193, 12244 with QU clear — obliges it to answer by
**unicast**. So the reply is undeliverable, deterministically, rather than lost at the
rates in the table above.

This is the `ETHARP_TRUST_IP_MAC` account, and it was retracted twice in this file before
the controls above were run. Both retractions were wrong, for instructive reasons: the
first refuted it with a QM-from-5353 measurement, which tests the multicast path the
account never described; the second used an experiment with no gateway control, so a
per-peer effect and a device-wide one were indistinguishable.

It remains an inference. No ARP frame was captured and the scale's ARP table was never
read. What is measured is the table above; the ARP story is the mechanism that fits it
and survives both controls.

Corollary worth knowing: lwIP ages entries out (`ARP_MAXAGE`, 300 s by default), which is
why a scale that resolved reliably a few minutes ago goes silent again with nothing
having changed, and why results differ between two runs of the same script.

## Open

**Whether the multicast path avoids this entirely.** It should: a query from source port
5353 with QU clear is answered multicast, which needs no ARP at all, and the scale does
answer that shape (75% / 41% above). On Android that also requires a held
`MulticastLock`, which the app now takes. The two branch commits that do this — query
from 5353, hold our own lock — are therefore aimed correctly, but the combination has not
been measured end-to-end on the tablet.

**Whether dialling the cached IP "repairs" mDNS.** Yes, per-peer, and that is why. Keep
the cached IP regardless: silence says nothing about whether the address is right.

## What this means for the code

- The two query shapes trade off, and the trade differs by platform. An ephemeral
  source port gets a UNICAST reply, which the AP acks and retries (~11/12 on a Mac) but
  which the scale can only send once it holds this device's MAC — the Android failure
  above. Source port 5353 gets a MULTICAST reply, which needs no ARP and reaches a cold
  peer, but is fire-and-forget (12/20) and on Android needs a held `MulticastLock`.
  Neither dominates; the branch sends from 5353 and holds the lock, which is the pairing
  that addresses the deterministic failure rather than the lossy one.
- Retry. A single query with a short timeout fails 25-60% of the time on an ordinary
  network, and no amount of correctness elsewhere changes that.
- Never evict a cached IP on silence.
- Do not blame the scale firmware. Two mechanisms were nearly filed as firmware bugs.
- On Android, a scale the device has never sent IP traffic to may be unresolvable
  outright, not merely slow, and will stay so until something addresses it. A
  discovery pass can therefore return empty on a LAN full of scales, and do so
  *deterministically*. The multicast path should sidestep this; see Open.

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
