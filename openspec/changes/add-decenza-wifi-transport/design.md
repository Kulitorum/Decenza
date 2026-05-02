# Design: Decenza Scale Wi-Fi Transport, Provisioning, and Calibration GUI

## Why a design.md

This change is cross-cutting in three independent ways: it adds a new BLE service consumer (provisioning), it adds a new transport class to a hierarchy whose interface was BLE-shaped, and it expands the canonical 12-domain Settings architecture to 13. It also reaches into a sister repository (the firmware) for the calibration command's wire format. Any one of these in isolation would not warrant a design doc; together, locking in the decisions before any code lands is cheap insurance.

## Decision 1 — Adapter, not refactor

Two structural options were considered for getting Wi-Fi frames into the existing protocol consumer:

- **Option A — Adapter.** `WifiScaleTransport` implements `ScaleBleTransport`. TCP bytes become synthetic `characteristicChanged` emissions on `Scale::Decent::READ`. BLE-shaped methods (`discoverServices`, `enableNotifications`) become trivial signal emissions or no-ops.
- **Option B — Parallel.** Refactor `DecentScale` to talk through a smaller transport interface (`open()`, `close()`, `send(bytes)`, `bytesReceived(bytes)`) that both BLE and Wi-Fi could implement cleanly.

**Decision: Option A.** Justification:

- The wake sequence in `decentscale.cpp:onCharacteristicsDiscoveryFinished` runs five timed singleShots (heartbeat, LCD enable @ 200 ms, NOTIFY enable @ 300 ms, NOTIFY enable @ 400 ms, LCD enable again @ 500 ms, heartbeat @ 2 s). The 300/400 ms double-NOTIFY-enable is a deliberate reliability dance for finicky BLE peripherals. Refactoring this away into a smaller interface would either move the dance up to the transport (BLE-specific code in a transport-neutral interface — wrong) or delete the dance (regresses real BLE devices that need it).
- The watchdog in `decentscale.cpp:onWatchdogFired` re-calls `enableNotifications` up to 10 times when weight data goes stale. Same shape as above — this is a BLE recovery mechanism that has no Wi-Fi analogue.
- The protocol consumer's data flow is *already* opaque: `characteristicChanged(uuid, bytes)` doesn't care where the bytes came from. The interface leaks BLE concepts in its method names but not in its data contract. The adapter exploits this; the refactor would re-prove a property the code already has.
- User has explicitly asked for small, surgical changes.

**Cost of the adapter:** the no-op methods read awkwardly. Mitigation: a comment block at the top of `wifiscaletransport.cpp` calling out the synthetic-emissions pattern, and a one-line `WIFI_LOG` per synthetic emission so debugging is greppable.

## Decision 2 — Dedicated short-lived QLowEnergyController for provisioning

The provisioning service (`0000feed-decc-...`) is on the same NimBLE server as the Decent Scale service (`0xFFF0` family). Two ways to write to it:

- **Reuse the runtime BLE transport's controller.** Save one BLE connect cycle.
- **Spin up a fresh, dedicated `QLowEnergyController` for provisioning.** One extra connect cycle; cleaner separation.

**Decision: dedicated short-lived controller.** Justification:

- The user explicitly asked for this in the planning conversation.
- Provisioning happens once per scale, ever (or once per Wi-Fi-config change — rare). A few extra hundred ms of BLE setup is invisible in that flow.
- The runtime transport's hot path (wake sequence, watchdog, heartbeat) has no business including provisioning code. Mixing them would also force `WifiScaleTransport` to grow a BLE backdoor for the IP-refresh case, defeating the abstraction.
- The user can provision a scale that is *not currently the runtime scale* — e.g. a second Decenza scale on the bench, or re-provisioning a paired scale after they changed Wi-Fi networks. Reusing the runtime controller would force the user to disconnect first.
- Self-deleting (`deleteLater()` on success/failure) is trivially correct because the lifetime is bounded.

**Exception — the opportunistic IP refresh after BLE connect.** This *does* reuse the runtime controller, because at that moment we already have a working BLE connection and we're just adding one fee4 read. Spinning up a parallel controller for one read would be silly. This is the only "shared controller" path; it's encapsulated as `DecenzaProvisioningClient::readWifiStatusOnce(controller, callback)` so the runtime transport doesn't grow a `readFee4()` method.

## Decision 3 — `Settings.connections` as a new 13th domain

Per `CLAUDE.md`'s settings architecture rules: "Add new properties to the matching `Settings<Domain>` class, or create a new sub-object if none fits."

The existing 12 domains: MQTT, AutoWake, Hardware, AI, Theme, Visualizer, MCP, Brew, Dye, Network, App, Calibration. The closest match is `SettingsNetwork` — but inspecting `settings_network.h` shows it is "shot server / saved searches / layout / discuss-shot URLs", a UI/web-config grab bag rather than a transport-state domain. Putting BLE-paired-scale Wi-Fi credentials there would be a stretch and would compound the very recompile-blast problem the split was designed to avoid.

**Decision: new `SettingsConnections` domain.** It can hold scale Wi-Fi pairings now and will absorb other device-pairing state later (e.g. DE1 last-known-MAC, scale model preferences) if we choose to migrate them.

This requires updating the `settings-architecture` capability spec — captured in the spec deltas.

## Decision 4 — Frame-resync on `0x03` header, not framing wrapper

TCP is in-order and reliable; under normal conditions the firmware writes one full 7-byte frame per `send()` call and the client receives them aligned. Two failure modes can produce misalignment:

1. **Initial connect mid-frame.** If a previous client disconnected mid-frame, the firmware *should* reset its frame writer, but defensive parsing is free.
2. **Partial reads.** `QTcpSocket::readyRead` may fire with fewer than 7 bytes; we already buffer for that.

Adding a length prefix or a magic delimiter on the firmware side would be cleaner *if* we were designing the protocol from scratch. We're not — we are reusing the Decent Scale 7-byte frame as-is so that `DecentScale::parseWeightData` is bit-identical between BLE and Wi-Fi. Adding a TCP-only framing wrapper would make the transport non-trivial to test against the existing protocol parser.

**Decision: defensive header-byte resync.** If `m_rxBuffer[0] != 0x03`, drop one byte and retry. Per-byte scan is fine — at 70 B/s peak (10 frames × 7 bytes) it's not a hot path. Log the resync once per occurrence (not per dropped byte) so a misbehaving firmware version is visible without spamming the log.

## Decision 5 — Calibration command wire format

The Decent Scale protocol as documented in `decentscaleprotocol.h` and used in `decentscale.cpp` defines these command bytes (byte index 1 of the 7-byte packet, after the `0x03` header):

- `0x0A` — LCD / LED
- `0x0B` — Timer (start/stop/reset variants in byte 2)
- `0x0F` — Tare
- (responses: `0xCE` / `0xCA` weight, `0xAA` button, `0x0A` LED response)

There is no defined calibration command. The firmware repo currently does not parse incoming TCP commands at all — see proposal item 7.

**Tentative format (subject to firmware approval):**

```
[0x03] [0x10] [weightHi] [weightLo] [0x00] [0x00] [xor]
```

- `0x10` chosen because it does not collide with any existing command in the Decent Scale protocol or with any response byte the firmware emits.
- `weightHi/weightLo` are a big-endian `int16` of the known weight in *decigrams* (matching the existing weight encoding — 100.0 g is `0x03E8`).
- Trailing zeros pad to 7 bytes.
- XOR checksum at byte 6 — same algorithm as every other Decent Scale command (`DecentScaleProtocol::calculateXor`).

**Coordination point:** before Phase 5 lands, the format must be approved in the DecenzaScale firmware repo (likely as a NimBLE/TCP write handler that, on receipt of `[0x03 0x10 ...]`, reads the puck-cell ADC, computes the new scale factor as `adc_now / weight_decigrams`, and persists it to NVS). If firmware proposes a different format, this design doc gets updated and the Decenza-side implementation tracks it.

**Punt:** if firmware coordination slips, ship Phases 0–4 + 6–8 with the calibration dialog *disabled* and a "Requires DecenzaScale firmware ≥ X.Y" message. The rest of the change is fully independent of calibration.

## Decision 6 — Persistence schema

`Settings.connections.scaleWifiPairings` is a `QVariantMap` keyed by lowercase scale BLE MAC address (e.g. `"a0:b1:c2:d3:e4:f5"`). Each value is a nested `QVariantMap`:

```
{
  "ip":          "192.168.1.42",       // QString, IPv4 dotted-decimal
  "port":        8765,                 // int — fixed today, persisted to allow future override
  "lastSeenIso": "2026-05-02T11:20:41-06:00"  // QString, ISO 8601 with timezone (per MCP conventions)
}
```

**Why MAC-keyed, not name-keyed.** Multiple scales can share the same advertised name ("Decenza Scale V1.0"). MAC is unique. The `isDecentScale()` name match still drives discovery; the MAC drives pairing identity.

**Why persist the port.** Today firmware exposes 8765 unconditionally. Persisting it lets us ship a future firmware that lets the user pick a port (e.g. for multi-scale conflict resolution) without a Decenza schema migration.

**Why ISO 8601 with timezone for `lastSeenIso`.** Aligns with the MCP-tool data conventions in `CLAUDE.md` ("Never return Unix timestamps. Use ISO 8601 with timezone"). Also human-readable in log output.

## Decision 7 — Transport selection failure semantics

If a stored Wi-Fi pairing fails to connect within 2 s (timeout, refused, host unreachable, network down), the factory falls back to BLE. Three sub-decisions:

- **Don't delete the pairing on failure.** A stale pairing is recoverable via the post-BLE-connect IP refresh; an aggressive delete would force re-provisioning every time the user takes the tablet on the road. The refresh path repairs the pairing without user intervention.
- **Don't retry Wi-Fi within the same session.** Once we've fallen back to BLE, stay on BLE for that session even if the IP refresh succeeds — switching mid-session would tear down the protocol state machine. Refresh is for *next launch*.
- **Log the fallback.** A single "Wi-Fi connect failed for <mac> at <ip>:<port>, falling back to BLE" line at warn level. Helps diagnose user reports of "the scale connection feels slow today".

## Decision 8 — Tare-over-Wi-Fi without firmware support

Tare is a Decent Scale command frame (`0x03 0x0F 0x01 0x00 ... + xor`). Two options for the Decenza side while waiting on firmware:

- **Wire the write path now.** `DecentScale::tare()` always calls `m_transport->writeCharacteristic(...)`. Over BLE it works today; over Wi-Fi the bytes are sent and silently dropped by current firmware. When firmware lands the receive handler, no Decenza-side change is needed.
- **Gate the write path on a "transport supports writes" flag.** Adds a code path that has no value once firmware ships.

**Decision: wire it now.** The Wi-Fi side becomes silently functional the day firmware adds the receive path. No deprecation, no flag, no follow-up Decenza commit. This is the smallest viable choice and matches the user's "small surgical changes" preference.

## Open questions

- Does Qt's BLE stack on Android initiate pairing automatically when writing to a characteristic with `WRITE_ENC` properties? Firmware is currently `WRITE` (cleartext). If we ever upgrade firmware to `WRITE_ENC`, this becomes load-bearing. Out of scope for this change — flagged for future.
- Should the IP-refresh path also refresh on Wi-Fi *connect success* (not just BLE connect success)? If we connected to the stored IP, the IP is by definition still good — no refresh needed. Not addressed because it would only change behavior if the *port* changed, which the firmware doesn't do today.
- Multi-scale UI ergonomics — left for a future change if and when a user reports it.
