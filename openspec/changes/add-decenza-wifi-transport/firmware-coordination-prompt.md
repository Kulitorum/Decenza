# Firmware-Side Coordination Prompt

This is the prompt to hand to the AI working on `c:\code\decenzascale` (the
ESP32-NimBLE firmware). The Decenza-side has reached a point where three
firmware-side decisions are needed before the corresponding Decenza features
can ship.

---

You're working on the DecenzaScale firmware (ESP32 / NimBLE). The
companion Decenza tablet app at `c:\code\decenza` has just shipped its
side of Wi-Fi-transport support and is now blocked on a few firmware-side
decisions before three follow-on features can land. Read this brief in
full; there are no behavioral changes the firmware MUST make, only
decisions to lock in so the app can target the right wire format.

## Context

You already shipped:
- The Wi-Fi runtime transport: TCP server on port 8765, streams 7-byte
  Decent Scale frames at 10 Hz to the connected client.
- The BLE provisioning service (`0000feed-decc-...`) with fee1/fee2/fee3
  write characteristics and fee4 status notify+read.

The Decenza app side now has:
- A `WifiScaleTransport` that consumes those frames (BLE-shaped adapter,
  unit-tested with localhost framing/resync/timeout coverage).
- A `DecenzaProvisioningClient` that drives fee1→fee2→fee3 writes and
  parses fee4 STATUS notifications.
- A `Settings.connections.scaleWifiPairings` map persisting `(mac → ip,
  port, lastSeen)` so subsequent app launches connect Wi-Fi-first without
  user interaction.
- A factory branch that builds the Wi-Fi transport when a pairing exists
  and falls back to BLE on connect-failure.

What's NOT shipped yet on the Decenza side: the GUI provisioning dialog
(Phase 4), and the calibration GUI (Phase 5). Both can use the existing
firmware as-is; calibration specifically needs a firmware decision.

## Decision 1 — Calibration command wire format (REQUIRED for Decenza Phase 5)

The Decenza calibration dialog wants to send a "calibrate to this known
weight" command to the scale. The user enters their reference weight in
grams (e.g. 100.0 g, 132.0 g — anything they own), places it on the
scale, and taps Calibrate. The Decenza app emits one frame on the active
transport (BLE or Wi-Fi); the firmware reads its puck-cell ADC and
persists a new scale factor `adc_now / known_weight_decigrams` to NVS.

Tentative format proposed by the app side, matching the Decent Scale
7-byte protocol convention:

```
Byte 0: 0x03                  // model header (existing convention)
Byte 1: 0x10                  // command byte — currently unused in the
                              //   Decent Scale protocol (existing
                              //   commands: 0x0A LCD/LED, 0x0B timer,
                              //   0x0F tare; responses: 0xCE/0xCA
                              //   weight, 0xAA button)
Byte 2: weight_high           // big-endian int16 of known weight in
Byte 3: weight_low            //   decigrams (matching the existing
                              //   weight encoding — 100.0 g → 0x03E8)
Byte 4: 0x00
Byte 5: 0x00
Byte 6: xor_checksum          // XOR of bytes 0..5 (same algorithm
                              //   used by every other command)
```

Question for firmware: **is `0x10` acceptable as the calibration command
byte, and is `int16BE decigrams` the right weight encoding?** If not,
propose your alternative — what matters is that we lock the format
before the Decenza-side calibration UI ships.

If you accept the format, please add the receiver in firmware. The
Decenza side can then wire a `DecentScale::calibrateToKnownWeight(double
grams)` method that emits this command on whichever transport is active
(BLE today, Wi-Fi too once Decision 2 lands).

## Decision 2 — Tare command receive over TCP (REQUIRED for Decenza tare-over-Wi-Fi)

The Decenza app already wires its `WifiScaleTransport::writeCharacteristic`
to send arbitrary bytes over the TCP socket, and `DecentScale::tare()`
goes through that path unconditionally. So when the user is connected
over Wi-Fi and taps Tare, the bytes `03 0F 01 00 00 00 [xor]` arrive on
your TCP socket — but firmware silently discards incoming TCP bytes
today.

Action: add a TCP-side receive handler that parses the same Decent Scale
write frames as the BLE write characteristic does (tare `0x0F`, timer
`0x0B`, LCD/LED `0x0A`, sleep, calibration once Decision 1 is in). Same
XOR check, same dispatch table. The simplest implementation is to share
the existing BLE-write parser between the two transports.

This unblocks tare-over-Wi-Fi end-to-end and means the calibration
command (Decision 1) works over Wi-Fi for free.

## Decision 3 — fee4 STATUS error code mapping (NICE-TO-HAVE)

The Decenza provisioning client parses fee4 status as `[state, rssi,
ip[0..3], err]` and emits state transitions to the UI. When state == 3
(Failed), it reports either "Wi-Fi connection failed" (if err == 0) or
"Wi-Fi connection failed (err=N)" (if err is non-zero).

Question for firmware: **what error codes does the firmware actually
emit, and what do they mean?** I'm guessing things like `1=wrong
password`, `2=AP not found`, `3=timeout`, but the Decenza side can show
much more useful UI messages if firmware documents the mapping.

If firmware already has this mapping somewhere, point me at it. If not,
propose a small enum (3–6 codes is plenty) and I'll match the Decenza
side. Cosmetic, can ship after the rest.

## Verification on the firmware side

A quick way to confirm everything works, in order:

1. **Decenza Phase 4 lands** (provisioning UI). User opens Decenza, goes
   to Settings → Connections → "Set up Decenza Wi-Fi", picks the scale,
   enters SSID and passphrase, taps Connect. Decenza writes
   fee1/fee2/fee3 in sequence and subscribes to fee4. Firmware should
   see all three writes, kick off STA association, and notify fee4 with
   state=1 (Connecting) → state=2 (Connected, IP filled).
2. **Decenza app restart.** Decenza should connect Wi-Fi-first to the
   stored IP, no BLE pairing dance. Live weight appears in <1 s.
3. **Decision 2 verification.** With Decenza connected over Wi-Fi, tap
   the Tare button. Firmware should receive `03 0F 01 00 00 00 [xor]`
   on the TCP socket and tare. Without Decision 2, the bytes arrive but
   are discarded — Decenza tare-over-Wi-Fi just appears to do nothing.
4. **Decision 1 verification.** Once the calibration UI lands on the
   Decenza side, the wizard sends `03 10 [weight] 00 00 [xor]`.
   Firmware should read the puck-cell ADC, compute the new scale
   factor, persist to NVS, and (optionally) emit a status response.

## What NOT to do

- Don't change the existing Wi-Fi runtime frame format (still 7-byte
  Decent Scale frames at 10 Hz, no length prefix, no framing wrapper).
  The Decenza side's frame parser is locked-in and unit-tested.
- Don't change the provisioning service / characteristic UUIDs or the
  fee4 STATUS byte layout. Decenza's parser is unit-tested against the
  documented `[state, rssi_int8, ip[0..3], err]` shape.
- Don't add BLE pairing / WRITE_ENC requirement to the provisioning
  characteristics for now. Decenza side accepts cleartext writes; we'll
  revisit if there's a real threat model that requires it.

## Reply format

When you have an answer, please reply with:

1. **Decision 1**: accept format / propose alternative (with bytes).
2. **Decision 2**: estimate of when the TCP receive handler lands.
3. **Decision 3**: error code mapping (or "TBD, will land later").

Then the Decenza-side AI can finish Phases 4–5 and ship the user-visible
UI.
