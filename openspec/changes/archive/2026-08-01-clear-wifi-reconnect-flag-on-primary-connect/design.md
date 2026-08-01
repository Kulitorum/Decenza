# Design

## Context

`m_wifiDirectAttemptFailed` was introduced by the reconnect-browse change to
answer one question: *should a reconnect tick open a browse?* Its lifecycle was
written for that question — set on a direct-dial timeout, cleared on a connect
that is not the WiFi→BLE fallback.

The clearing rule uses "was the fallback active" as a stand-in for "is the thing
that connected the backup". Those coincide for the fallback's own connect. They
come apart on the browse's success path, where the primary connects while the
fallback scan is still running, and the stand-in reports backup for the primary.

## Goals / Non-Goals

- **Goal:** the flag reflects reality after a browse-driven recovery.
- **Goal:** one definition of "on the primary", shared with `main.cpp`.
- **Non-Goal:** changing the reconnect ladder's behaviour. Only the belief is
  wrong, not the sequencing.
- **Non-Goal:** a `BLEManager` test harness. No test target links
  `blemanager.cpp` today, and adding one would pull the whole Bluetooth/scale
  graph into a test binary — the expensive direction per TESTING.md.

## Decisions

### Ask what connected, via `type()`

`ScaleDevice::type()` is virtual on the base class (`src/ble/scaledevice.h:38`)
and `DecentScaleWifi::type()` returns `"decent-wifi"`. `main.cpp:2465` already
uses exactly this to decide "already on the WiFi primary" before honouring
`wifiPrimaryReachable`. `connectedScaleIsWifiPrimary()` applies the same type
test, so the two layers agree on what the phrase means — and both now read it
from `ScaleTypeIds` rather than a hand-written literal, without which "cannot
drift apart" would have been an assertion with nothing behind it.

**The limitation is reachable, not theoretical**, and an earlier draft of this
document claimed otherwise. With a `wifi:` primary saved, any connected WiFi
scale answers true, because this compares the type and not the host. Two routes
reach it, both because the saved address is written *later* than the connection
is reported:

- A manual "Add WiFi Scale" defers `setSavedScaleAddress()` to `recognizedAsHds`,
  while `connected` is signalled at WebSocket open — `main.cpp:3030` says so
  outright. The #1281 shape (a user typing their router's address) lands here.
- Tapping a discovered WiFi scale while a dead `DecentScaleWifi` is still the
  current scale takes `main.cpp`'s type-unchanged re-wire branch, which returns
  without persisting.

Both are inert at both call sites, which is what makes the type test sufficient.
Clearing `m_wifiDirectAttemptFailed` wrongly costs at most one ladder cycle:
`onScaleConnectionTimeout()` re-arms it, and is also the only place a browse ever
starts, so the browse can be delayed but not disarmed. Returning early from
`maybeAutoConnectBrowsedScale()` produces the same outcome `main.cpp`'s own
decline already produced.

The alternative — comparing hostnames — would close the gap, but needs an
accessor `ScaleDevice` does not have, to separate cases whose wrong answers cost
nothing. Worth revisiting only if a consequence stops being inert.

**Alternative considered and rejected:** clear `m_wifiFallbackToBleActive` at the
browse's dial site, so the later connect no longer looks like a fallback. That
flag also gates whether an in-flight fallback scan may adopt a discovered BLE
scale (`onDeviceDiscovered`) and whether a second fallback is permitted
(`onScaleConnectionTimeout`). Clearing it there would quietly change which scale
the ladder ends up on, to fix a bookkeeping error. Narrower to correct the one
belief that is wrong.

### Express the clearing rule as a pure predicate

`connectClearsDirectAttemptFailed(wasWifiFallbackConnect, connectedScaleIsWifiPrimary)`
joins the two predicates already in the header. It is a two-input rule, so the
test is a four-row truth table including the row that was wrong — cheap, and it
makes the rule legible next to the flag it governs.

Its limit is stated in the test: it asserts the RULE. Whether
`connectedScaleIsWifiPrimary()` reports true on the browse path depends on live
objects and is not reachable without linking `blemanager.cpp`. That half rides on
the field log, as the surrounding browse machinery already does.

### Clear `m_browsedPrimaryIp` at the start of each probe

The member survives a declined request, and `switchToWifiPrimary()` prefers it
over the persisted cache. Clearing it where the competing evidence is produced —
`probeWifiPrimaryReachable()` — is event-based and needs no timer or timestamp:
whichever path most recently produced evidence owns the address.

## Risks / Trade-offs

- **The flag now has two clearing conditions instead of one.** Both are stated in
  one predicate with the reason attached, rather than spread across the handler.
- **`connectedScaleIsWifiPrimary()` is not a pure function**, so unlike its three
  neighbours it cannot be asserted in the existing test file. Accepted rather
  than building the harness that would make it testable — per the project's
  stance that needing new machinery to reach a branch is a signal to look at the
  branch, not to build the machinery.
