## Context

See proposal.md — Why. The constraints that shape the approach:

- **Qt provides nothing to build on.** No Qt USB module exists; `QSerialPortInfo` is not a `QObject` and exposes only static `availablePorts()`. Verified against `~/Qt/6.11.2/Src/qtserialport/src/serialport/` — no `udev_monitor`, `IOServiceAddMatchingNotification`, `WM_DEVICECHANGE` or `RegisterDeviceNotification` anywhere in the module. Hotplug is per-platform native code or nothing.
- **Android already has the pieces.** `AndroidUsbScale.java` uses `android.hardware.usb.UsbManager` directly, reached from C++ through `QJniObject` in `androidusbscalehelper.cpp`. `android/res/xml/device_filter.xml` already lists the CH340 (scale) and CH9102 (DE1) VID/PIDs.
- **The manifest filter is not hotplug.** `USB_DEVICE_ATTACHED` at `AndroidManifest.xml.in:86` is an activity intent-filter: it launches or foregrounds the app and grants permission. Nothing reads it at runtime, and there is no `DETACHED` counterpart. It stays as-is and is orthogonal to this change.
- **Two managers, one shape.** `UsbScaleManager` and `USBManager` (DE1) both own a `QTimer`, a `startPolling()`/`stopPolling()` pair, and a platform-split tick. Only the DE1 one is gated.
- **`stopPolling()` already tears down probe state** (`cleanupAndroidProbe`/`cleanupProbe`), so the gate has a correct off-path today; nothing new is needed for it.

## Goals / Non-Goals

**Goals:**

- Android learns of attach and detach from a broadcast, not a tick — for the DE1 as well as the scale.
- One setting governs USB scanning, and only scanning; no second toggle.
- The fallback poll survives, cheaply, and its interval is one named constant per case.
- Detach is handled at all — today nothing reports it except the scan noticing the device vanished.

**Non-Goals:**

- Desktop hotplug (udev/IOKit/`WM_DEVICECHANGE`). Three platform implementations for a rare transport is not worth it now; desktop keeps its responsive poll.
- Changing the DE1's SCANNING behavior. `USBManager` is already gated and its poll stays exactly as it is; the DE1 only gains the hotplug path.
- Removing the Android poll. That is the follow-up once the receiver is proven in the field.
- Changing the manifest's launch-time `USB_DEVICE_ATTACHED` filter or `device_filter.xml`.

## Decisions

**Broaden the existing setting rather than add a second one.**
`usb/serialEnabled` keeps its storage key and its `false` default; only the user-facing wording widens from "DE1" to "USB devices". Alternative considered: a separate `usbScaleEnabled`. Rejected — two adjacent toggles for the same cable, with the same battery rationale, is a worse settings page than one, and a second key would need its own default and its own migration reasoning. The cost is that the setting's meaning changes for users who already have it on, which the wiki entry has to state.

**No migration; the default stays `false`.**
Turning USB scale detection into an opt-in is a real regression for anyone using one, and it is accepted rather than mitigated: USB is a rare way to attach a scale, and "Scan for Devices" already runs a USB probe pass regardless of the setting (see `wifi-scale-discovery`), so a user who plugs in a scale has a working path without touching settings. Alternative considered: enable the setting for anyone with a known USB scale in `knownScales`. Rejected as machinery serving almost nobody — and it would silently turn on a setting the user never chose.

**One receiver for both device kinds, dispatching by VID/PID.**
`device_filter.xml` already lists the DE1's CH9102 and both scale CH340 PIDs, so a single `BroadcastReceiver` covers everything the app supports; it identifies the device and routes to `USBManager` or `UsbScaleManager`. Alternative considered: a receiver per manager, mirroring the two existing JNI helpers. Rejected — the two helpers already expose an identical `hasDevice()`/`deviceInfo()` surface, and a second receiver would duplicate the registration, the lifecycle handling and the VID/PID filter, which is precisely the copy-per-caller drift the centralize rule exists to stop. `UsbScaleManager` and `USBManager` each gain attach/detach entry points that do exactly what a scan tick that saw the device appear or disappear does today. Routing hotplug into the *same* handlers is what keeps the two paths from diverging — the spec's "detection paths agree" requirement is satisfied by construction rather than by a second code path that has to be kept in step.

**Filter on VID/PID in the receiver, not after the fact.**
The broadcast carries the `UsbDevice`. Matching the identifiers already in `device_filter.xml` at the receiver means an unrelated device never reaches C++ at all. The values are declared in the XML and must not be re-typed in Java — read them from the device filter resource, or keep one shared constant, so the two lists cannot drift (CLAUDE.md's centralize rule).

**Poll interval becomes a per-platform named constant.**
`POLL_INTERVAL_MS = 2000` is currently one value used everywhere. It splits: Android's fallback (60 s) and the desktop detection interval (2 s, unchanged). Named separately because they answer different questions — "how long until a missed broadcast is noticed" versus "how long until an attach is noticed". Alternative considered: a single value chosen for the worst case. Rejected — it forces desktop to be slow or Android to be wasteful, and it hides that the Android number is provisional.

**The switch is about scanning; nothing else is gated by it.**
The setting's purpose, stated in the code it came from, is avoiding "the 2 s polling battery drain". A registered `BroadcastReceiver` that receives nothing costs nothing, so gating it would trade no saving for a plugged-in scale that does not work. Splitting it this way also collapses the upgrade regression on Android to zero — the platform that matters most here — leaving it only on desktop, where there is no hotplug to fall back on. Alternative considered: gate both, so "off" means one simple thing. Rejected: the simplicity is only in the sentence, and it is paid for by a user plugging in a scale and getting nothing.

**Register the receiver on the activity lifecycle, not at manager construction.**
Android requires unregistration; a receiver leaked across a teardown is a warning at best and a crash at worst. `UsbScaleManager`'s destructor already calls `stopPolling()` and `cleanupAndroidProbe()`, so it is the natural owner of the unregister call too.

## Risks / Trade-offs

**Desktop USB scale users lose automatic detection on upgrade** → Accepted, per proposal, and desktop-only: Android keeps working via hotplug. Mitigation is discovery, not code: the wiki entry states that desktop needs the toggle, and "Scan for Devices" finds the scale without it.

**A broadcast is missed while the app is backgrounded** → The 60 s fallback poll recovers it. This is precisely why the poll is kept rather than deleted, and why the fallback interval is a constant rather than a deletion.

**OEM-modified Android builds may not deliver `DETACHED`** → The existing `connectedChanged` watchdog in `connectToScale()` already tears down a scale whose serial link dies while still enumerated, and the fallback poll still sees the device disappear. Detach is therefore covered three ways.

**"Off" no longer means "no USB activity"** → It means no periodic *scanning*. A user who turns the setting off and later plugs in a scale on Android will still get a connection. Judged correct rather than surprising: the control is a battery setting, and a scale that connects when plugged in is what a user expects. The wording change in task 2.1 must say "scan" rather than implying USB is disabled outright, or the control will read as a lie.

**Widening the setting's meaning changes behavior for existing users who have it ON** → They gain USB scale polling they did not previously have. Harmless (it is the current unconditional behavior) but it belongs in the release note.

## Migration Plan

No data migration. The storage key and default are unchanged; only the setting's scope and wording change.

Rollback is reverting the change: the setting reverts to DE1-only meaning and the unconditional scale poll returns. No persisted state is written that an older build would misread.

## Open Questions

- **What the Android fallback interval should settle at.** 60 s is a starting point chosen to be clearly a safety net rather than a mechanism. Whether it can go to zero depends on field evidence that the broadcast is reliable across the OEM builds in use — deliberately deferred, and it changes only a constant.
