## Context

The OpenScale HDS firmware implements signed WiFi pull OTA. Its Bluetooth and USB command protocol has a WiFi-update trigger at opcode `0x1B`, and OpenScale PR #165 — merged, carried by `v3.1.14-preview.1` — extends it with an optional three-byte version payload plus an equivalent `wifi_update` control command on the `/snapshot` WebSocket. A request that names a version installs it without the on-OLED release picker and without the hold-to-confirm gesture; a request with no version keeps the interactive flow on every transport.

Decenza already has a shared network manager and GitHub release-note retrieval logic, and scale firmware versions arrive from existing HDS transport packets.

See proposal.md and the hds-firmware-update spec for the behavioral contract.

## Goals / Non-Goals

**Goals:**

- Make an available HDS update discoverable without adding normal-state Connections UI.
- Fetch only at launch and on real application resume; reuse a successful manifest for all scale changes in the session.
- Give the user release notes and an explicit confirmation, then complete the update with no interaction at the scale.
- Support the update on every transport an HDS can be selected over: Bluetooth, USB, and WiFi.
- Preserve the HDS as the cryptographic and hardware-compatibility authority.

**Non-Goals:**

- Downloading, proxying, or validating HDS firmware assets in Decenza.
- Supplying asset URLs, sizes, or hashes to the scale. Decenza contributes a version number and nothing else.
- Adding a recurring polling timer, persisted availability state, or an update banner.
- Reporting install progress. The firmware exposes no client progress stream, by design.

## Decisions

### Use the HDS manifest for availability and the release API for prose

Read the same `releases/latest/download/manifest.json` catalog URL that the HDS reads. It supplies the eligible release versions and compatibility metadata. Fetch the selected release's GitHub release body through shared release-client code for the dialog, because the manifest supplies a release-notes URL but not the Markdown itself.

The scale independently retrieves and verifies the manifest signature and assets when it starts OTA, and resolves the requested version against its own picker selection list rather than the raw catalog. A release the scale's own eligibility rules would not have offered is refused, with no fallback to the picker. Decenza's manifest read is therefore an advisory UI input, not the authorization to install firmware — a wrong offer from Decenza cannot install anything.

### Tie requests to lifecycle edges, not an interval

Start one manifest request during app initialization and another only after a Suspended → Active lifecycle transition. Retain the last good result in memory. Scale selection and connection changes only re-evaluate that cached catalog, which avoids network traffic when the user changes scales.

The existing app updater's hourly cadence is too frequent, and the DE1 firmware updater's startup-plus-weekly cadence is too infrequent. Reuse their shared network and lifecycle infrastructure rather than either schedule.

### Add an explicit HDS update capability to the active scale surface

Expose the HDS firmware version and a capability-safe `startFirmwareUpdate(version)` operation from the active Bluetooth, USB, and WiFi HDS drivers. A controller follows the active scale and clears its state immediately on target or connection changes. This avoids treating generic scale names or raw command bytes as QML-facing behavior.

The WiFi driver is the one that needs new surface rather than a modified signature: it currently keeps `m_firmwareVersion` for diagnostics only and has no OTA path at all.

### Read the version as a plain major.minor.patch on every transport

The scale's own version is one compile-time string, `HDS_FIRMWARE_VERSION` in `config.h`, and it reaches Decenza two ways: verbatim in the WiFi `/snapshot` status JSON, and packed into two bytes in the Bluetooth and USB LED response, which carries no prerelease tag and gives minor and patch one nibble each.

Normalise both to `major.minor.patch` rather than teaching the comparator about prerelease tags. The packed read's only errors round *up* — a preview reads as its stable, `3.1.16` reads as `3.2.0` — so the catalog then holds nothing newer and Decenza simply offers no update. A missing Update button on a preview build is the whole consequence, and the scale re-resolves the version exactly before installing anything.

### Validate the target once, where the catalog is parsed

`HdsFirmwareCatalog` normalises a leading `v` away and refuses any component above 127 — the largest a payload byte can carry, and the same bound `pullOtaParseTargetVersion` enforces — at ingest. Every stored version is therefore a valid target on every transport by construction.

This is what keeps the transports from each deciding for themselves. Before it, the same question was answered three ways: the catalog admitted `v3.1.14`, the byte encoder rejected it, and the WiFi driver counted dots and accepted anything. A manifest entry could install on one transport and be refused on the others.

Refusing beats clamping for the same reason. A component clamped to 127 is a *different, installable* release, so `1.2.300` would have asked the scale for `1.2.127` — a version the user was never shown. The transports keep their own check as the last line before hardware, but it now asserts an invariant rather than deciding policy.

### Send the version unconditionally, with no capability negotiation

Each version byte is encoded `0x80 | value`, capped at 127. The bias is what makes one request form correct against every scale:

- Firmware predating PR #165 frames `03 1B` as two bytes, starts its picker, and discards the trailing payload through the text path. That is the correct graceful fallback.
- Firmware carrying PR #165 disambiguates on `data[2] >= 0x80`. A following command always begins `0x03`, and a biased byte never can, so the two forms are distinguished with no checksum and no reliance on the frame timeout.

So Decenza sends the versioned form to every HDS and gates nothing on the reported firmware version. Gating would be worse than useless here: the reported version is exactly the field the upstream nibble-packing bug corrupts (see Risks).

An unbiased payload would be unsafe rather than merely unrecognised — on older firmware a `3.10.2` target decodes to `03 0A 02`, the power-off command. Build the payload in one shared encoder so neither transport driver hand-rolls the bias.

### Write the exact frame the transport expects

Both Decenza HDS drivers currently pad every command into a fixed seven-byte packet. That stays correct for Bluetooth: the firmware's length check is a minimum, so `03 1B 83 81 8E 00 XOR` passes and the trailing bytes are ignored.

USB is framed rather than packetised, and `0x1B` with a biased payload is a five-byte frame. A padded write leaves two trailing bytes to the text path. They are harmless — the XOR over a biased payload always lands in `0x80..0xFF`, so it can never be misread as a `0x03` frame start — but that is luck, not design. Write the exact five-byte frame on USB.

WiFi carries no framing question: send the `wifi_update <version>` control command over the existing `/snapshot` WebSocket.

### Claim only what a transport can support, rather than building a channel to prove more

Bluetooth and USB carry no acknowledgement of an update request at all. So "the scale accepted this" is not knowable there — not with a return value, which only says bytes reached a socket, and not with any amount of added machinery. Over WiFi the scale answers, but only at accept time: `ota_version_invalid` and `ota_busy`. Everything it checks afterwards — catalog eligibility, signature, rollback — happens once it has closed its WebSocket clients, and reaches its own display alone.

The dialog therefore says the update was *requested*, and that the scale restarts if it accepts and shows any problem on its display. That is true on all three transports and needs nothing built.

This is deliberately not a failure-reporting channel. The paths that could fail are unreachable from the UI: the controller already gates on `supportsFirmwareUpdate()` and `isConnected()` before offering the action, and the catalog guarantees the version parses. A `bool` return and a failure signal would exist to describe states the UI cannot enter.

What carries the story instead is the log, which already exists: the start line sits at INFO so the connections view shows it, a dropped command warns, and a WiFi `error` frame is warned unconditionally rather than through the once-per-connect frame-shape sampler that would otherwise swallow a refusal behind an earlier unrelated error.

## Risks / Trade-offs

- [Bluetooth and USB report a packed version] → It carries no prerelease tag and caps minor and patch at 15, and its errors round up, so the failure mode is that Decenza offers no update rather than a wrong one. PR #165 records the upstream `buildLedResponsePacket()` fix as its own change; the packed read is exact through `3.1.15`.
- [Manifest and release notes can be stale for the session] → The HDS rechecks its signed catalog at installation time and resolves the request against its own selection list; a stale Decenza offer cannot bypass device validation.
- [A refused install is invisible to the app] → Accept-time refusals are reported; catalog-level ones are not, by firmware design. Decenza states that the update has started and relies on reconnection to confirm the outcome, which is the same evidence it would have had anyway.
- [Network errors leave no visible result] → Intentional: the normal Connections surface stays quiet. Log diagnostics and try again on the next launch or resume.
- [Future hardware metadata may outgrow the host's eligibility view] → Treat HDS validation as final and keep the host's presentation constrained to manifest candidates it can identify.

## Migration Plan

1. Ship the availability control and the versioned start command on all three transports. Older HDS firmware degrades to its own picker with no client change.
2. Verify the quiet path on hardware, which is reachable today: a scale already at or above the newest catalog release must leave Settings → Connections unchanged.
3. Verify the install once upstream publishes a stable release newer than the scale under test. This cannot be arranged locally — the app offers only a NEWER release, so the signed downgrade openscale#165 used for its own testing is not a route available here, and a pre-release does not appear in the catalog at all (`releases/latest` skips it). Then confirm on Bluetooth, USB, and WiFi that no picker appears, that a failed update leaves the installed firmware running, and that pre-#165 firmware still reaches its picker.
4. Track the upstream `buildLedResponsePacket()` nibble fix and re-check the eligibility comparison once a release past `3.1.15` exists.
