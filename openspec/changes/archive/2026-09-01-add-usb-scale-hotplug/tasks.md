## 1. Gate the scan behind the existing setting

- [x] 1.1 Move `usbScaleManager.startPolling()` (`src/main.cpp:2154`) inside the `settings.usbSerialEnabled()` check and add it to the existing `usbSerialEnabledChanged` handler beside `usbManager`, so both start and stop together; verify by toggling the setting at runtime and seeing `[Scale][USB Scale] Polling started` appear and no further poll activity after switching it off
- [ ] 1.2 Confirm a fresh profile logs no `[Scale][USB Scale] Polling started` at startup, and that `stopPolling()` reaching `cleanupAndroidProbe`/`cleanupProbe` leaves no probe state behind
- [ ] 1.3 Verify "Scan for Devices" still finds a USB scale with the setting OFF, via the existing `probeNow()` path — this is the recovery path the proposal relies on, so it is the one that must not regress

## 2. Reword the setting as a scanning control

- [x] 2.1 Reword `connections.usb.serialLabel`, `connections.usb.serialDesc` and `settings.connections.accessible.enableSerialUsb` in `qml/pages/settings/SettingsConnectionsTab.qml` so the control reads as governing SCANNING for USB devices (DE1 and scale), not USB support — a user must not read it as "turn USB off"; verify the Connections page and a screen reader both read the new wording
- [x] 2.2 Update the comment at `src/main.cpp:2144` that currently scopes the rationale to the DE1, and the doc comment on `Settings::usbSerialEnabled()`; storage key `usb/serialEnabled` and its `false` default are unchanged — verify by reading back an existing profile and seeing the prior value preserved

## 3. Split the poll interval per platform

- [x] 3.1 Replace `UsbScaleManager::POLL_INTERVAL_MS` with two named constants — an Android hotplug-fallback interval (60 s) and a desktop detection interval (2 s) — selected by platform at the one place the timer interval is set; verify the Android log shows ticks ~60 s apart and desktop is unchanged at 2 s
- [x] 3.2 State at the constant why the two differ (missed-broadcast recovery vs. sole detection path) and that the Android value is provisional pending field evidence — this is the note that lets it later go to zero

## 4. Android hotplug receiver (DE1 and scale)

- [x] 4.1 Add ONE `BroadcastReceiver` for `ACTION_USB_DEVICE_ATTACHED` and `ACTION_USB_DEVICE_DETACHED` covering both device kinds, registered and unregistered on the activity lifecycle; verify with `adb shell dumpsys activity broadcasts` that exactly one registration exists and none survives teardown
- [x] 4.2 Filter received broadcasts to the supported VID/PIDs without re-typing them — read `android/res/xml/device_filter.xml` or share one constant list; verify an unrelated USB device (any keyboard or stick) produces no app-side action
- [x] 4.3 Expose attach and detach across the existing `QJniObject` bridge, dispatching to `UsbScaleManager` or `USBManager` by device identifier; follow the pattern already used by `hasDevice()` in both helpers
- [x] 4.4 Route both events into the *same* manager entry points the scan tick uses when it sees a device appear or disappear — not a parallel path; verify by reading the code that each manager has one place reacting to "device present" and one to "device gone"
- [x] 4.5 Register the receiver unconditionally — NOT behind `usbSerialEnabled`; verify that plugging in a scale AND a DE1 with the setting OFF still detects and connects each, and that unplugging either is observed

## 5. Behavior verification on device

- [ ] 5.1 With the setting ON, plug in a USB scale and verify from the log that the attach is handled on the broadcast rather than on a poll tick (elapsed time well under the 60 s fallback interval)
- [ ] 5.2 Unplug a connected USB scale and verify the app reports no USB scale connected promptly, and no stale connected state remains
- [ ] 5.3 Verify no duplicate connection when a scan tick and a hotplug attach observe the same device — attach, then confirm one connect in the log, not two
- [ ] 5.3a Verify the DE1 over USB-C is detected on plug-in with the setting OFF, and that unplugging it is observed
- [ ] 5.4 Verify the missed-broadcast recovery: with hotplug suppressed (or the receiver temporarily unregistered), confirm the fallback poll still discovers an attached scale — with the setting ON, since that path is the gated one
- [ ] 5.5 Confirm the desktop build still detects attach within its 2 s interval, since it has no hotplug path

## 6. Tests

- [~] 6.1 NOT DONE, see Deviations — Add test coverage for the gate — polling does not start when the setting is off, starts on enable, stops on disable — as new slots in an existing `tst_*` file rather than a new file, per the build-cost rule in TESTING.md
- [~] 6.2 REPLACED, see Deviations — Add coverage that an attach and a poll-discovered device converge on the same state and produce no duplicate connection; state in the test comment which defect shape it catches that no existing test does
- [x] 6.3 Run the full suite via `mcp__qtcreator__run_tests` (scope `all`) and confirm no warnings, per the strict warning rules in TESTING.md

## 7. Documentation

- [~] 7.1 DROPPED (no wiki changes needed for this change) — Update the wiki manual's Connections entry: the USB toggle covers scales as well as the DE1 and controls *polling* — on Android a plugged-in scale works without it; on desktop it is needed (or a Scan). Keep it to the two or three sentences the manual rules call for
- [ ] 7.2 Add a release-note line for the widened setting: it now covers scanning for USB scales as well as the DE1, and on desktop a USB scale needs it on (Android is unaffected — hotplug covers it)
- [x] 7.3 Note in the change that desktop hotplug (udev/IOKit/`WM_DEVICECHANGE`) remains unimplemented, so the next person does not assume it exists

## Deviations from the plan

**6.1 (gate test) — not written.** The gate is a lambda in `main.cpp`; there is no
seam to test it through without either adding an `isPolling()` accessor purely for
the test (which would then assert `QTimer::isActive()` — a test that cannot
meaningfully fail) or restructuring `main.cpp` beyond what this change asks for.
Verified instead by the desktop run in group 5.

**6.2 (convergence test) — replaced with a dispatch test.** The convergence defect
it was written for no longer exists: hotplug and the timer call the SAME function
(`onHotplugEvent()` runs `onPollTimerTick()`), because that tick is driven by
`hasDevice()` rather than by what triggered it. There is no second path to diverge.
Reaching the Android tick from a test would also need a JNI fault-injection
harness, which CLAUDE.md names as a stop sign rather than a testing problem.

What replaced it is a real invariant with a real defect shape: `device_filter.xml`
is the single list of supported ids, and a scale id added there but missing from
the C++ scale list routes that scale's events to the DE1 manager — silently, since
the id is still "supported" and the receiver still forwards it.
`deviceFilterIdsRouteToTheRightManager` and
`theDeviceFilterListsExactlyTheIdsCoveredAbove` in `tst_usbdecentscale.cpp` bind
the two together.

That test's FIRST version was vacuous: it compared `usbDeviceKindForPid()` against
`kUsbScalePid1`/`kUsbScalePid2`, the same constants the function reads, so breaking
a constant moved both sides and the suite stayed green. Caught by deliberately
setting `kUsbScalePid2 = 0x9999` and watching the suite pass. The expected ids are
now literals, and the same break was re-run against the rewritten test to confirm
it goes red.

**7.1 (wiki) — dropped**, per the maintainer: no manual changes needed.

**Group 5 (on-device) and 7.2 (release note) — outstanding.** Desktop to be checked
on macOS; Android via the next beta build. Nothing in the Android path has been
compiled yet: the macOS build only sees the `#else` branch of `usbhotplug.cpp`, and
the Java, the JNI registration and the receiver are untouched by it.
