## Why

Twice in one submitted debug log (`debug-2.log`, builds 3571 and 3572), the DE1 link went dead in
both directions while the controller still reported it connected. In both episodes the app held a
fault signal **22.5 s before** the platform reported the disconnect, and did nothing with it:

| | write abandoned | platform reported `Unconnected` | dead time |
|---|---|---|---|
| Episode 1 | 189.14 | 211.65 | 22.51 s |
| Episode 2 | 1929.62 | 1952.13 | 22.50 s |

In episode 2 there is no DE1 traffic of any kind — inbound or outbound — across that window. Every
command sent to the machine in it is discarded, so a stop press or a stop-at-weight stop does
nothing, and the log tells the user to reconnect the DE1 by hand.

The app already tracks what it needs: `BleTransport::m_notificationLiveness` is restarted by every
inbound push. But it is compared against a single 30 s threshold — longer than the 22.5 s outage —
and only inside `connectToDevice()`, where it turns an incoming reconnect into a teardown. Nothing
calls `connectToDevice()` while the link claims to be connected, so the check never runs in the
case it exists for.

## What Changes

- **The DE1's liveness is evaluated when a write is abandoned, and acted on.** If the DE1 has also
  gone quiet, the app tears the link down and the existing reconnect ladder recovers it — roughly
  10 s instead of 22.5 s of a machine that ignores every command.
- **Two signals have to agree**: the write must have been abandoned AND the DE1 must have gone
  quiet. Either alone is weak — writes fail transiently on healthy links, and silence is only
  suspicious against a cadence nobody has measured.
- **The dead-link log entry names the right remedy for each case.** Where the DE1 has also gone
  silent, the app reconnects itself; where it is still sending — the case this change deliberately
  does not cover — the entry still points at the Connections page and `devices_connect_de1`, which
  is the user's only recourse there.

Deliberately not in scope: the trigger (both episodes began on the 60 s MMR keepalive write to
`0x803854`); any change to the scale or refractometer paths; and the connect-time settings burst on
a mid-shot reconnect — a teardown produces the same sequence as a spontaneous mid-shot drop, which
already happens today, so this change neither creates nor worsens that path.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `de1-connection-health`: notification liveness is evaluated on its own rather than only at a
  reconnect attempt, and a silent link is torn down so the existing ladder recovers it. Upload
  retries are not spent on a link that is not carrying writes.
- `ble-write-retry-policy`: the dead-link log entry no longer names a manual remedy.

## Impact

- `src/ble/bletransport.cpp` / `.h` — liveness gains its own evaluation and a shorter threshold;
  the teardown; the dead-link warning text.
- `src/ble/blemanager.cpp` / `main.cpp` — the teardown's `disconnected()` reaches the existing
  ladder; it must not double-schedule.
- `src/controllers/profilemanager.cpp` — a retry falling due against a non-writing link is withheld.
- Tests — `tests/tst_bletransport*` and profile-upload retry coverage, plus a mock transport that
  can present a connected-but-silent link.
- Docs — `docs/CLAUDE_MD/BLE_PROTOCOL.md`.
- **Not touched**: the scale and refractometer paths.
