## Why

`usbScaleManager.startPolling()` runs unconditionally at startup (`src/main.cpp:2154`) and nothing outside the destructor ever stops it. On Android each tick is a JNI call into `AndroidUsbScaleHelper::hasDevice()` every 2 s, on the main thread, for the life of the process — observed in a field session where a BLE scale was connected and no USB device was ever present.

Its DE1 twin two lines above is gated behind `settings.usbSerialEnabled()`, with the reason stated in the code: *"to avoid the 2 s polling battery drain on devices that never use a USB-C cable."* The same cost applies to the scale poll and no gate was ever added.

Polling is also the *only* runtime detection path today. The `USB_DEVICE_ATTACHED` filter in `AndroidManifest.xml.in:86` is an activity launch hook, not a runtime signal — nothing in `src/usb/` reads that intent — and `USB_DEVICE_DETACHED` is not wired at all. Qt offers no alternative: there is no Qt USB module, and `QSerialPortInfo` is not a `QObject` and exposes only the static snapshot `availablePorts()` (verified in `~/Qt/6.11.2/Src/qtserialport`; the module contains no `udev_monitor`, `IOServiceAddMatchingNotification` or `WM_DEVICECHANGE` code). So hotplug has to be written per platform, and Android — the primary platform — is the one where it is cheap, because the JNI bridge and the `UsbManager` Java class already exist.

## What Changes

- **Android USB hotplug for BOTH device kinds, always on.** One runtime `BroadcastReceiver` for `ACTION_USB_DEVICE_ATTACHED` and `ACTION_USB_DEVICE_DETACHED`, filtered to the VID/PIDs already declared in `android/res/xml/device_filter.xml` — which already lists the DE1's CH9102 alongside both scale CH340 PIDs — dispatching to `USBManager` (DE1) or `UsbScaleManager` (scale) by identifier. One receiver, not one per device: `AndroidUsbHelper` and `AndroidUsbScaleHelper` already expose the same `hasDevice()`/`deviceInfo()` surface, and two near-identical receivers is the drift the centralize rule exists to stop. **The receiver is NOT gated by the USB setting** — an idle broadcast subscription costs nothing, so on Android a plugged-in device works whether the setting is on or off.
- **The Android poll stays as a fallback but slows from 2 s to 60 s**, not removed — a broadcast can be missed while backgrounded or on OEM-modified builds, and the poll is the only recovery. Once the receiver carries detection, 2 s buys nothing: it becomes a safety net whose job is to notice a missed broadcast eventually, not to be the mechanism. The interval stays a named constant so it can go to zero once hotplug is proven in the field. Desktop keeps its 2 s tick, where polling is still the only detection path.
- **The existing `usbSerialEnabled` setting is broadened from "scan for the DE1" to "scan for USB devices"**, and now gates `usbScaleManager.startPolling()` as well. The switch is about SCANNING and nothing else — it does not gate hotplug, the on-demand probe behind "Scan for Devices", or an already-connected scale. Its label, description and accessibility name change to say so; the storage key `usb/serialEnabled` is unchanged.
- **USB scale scanning becomes opt-in on desktop only.** `usb/serialEnabled` defaults to `false` and stays that way. On **Android** nothing regresses — hotplug is ungated, so plugging a device in still works with the setting off. On **desktop**, which has no hotplug, a USB scale now needs either the toggle or a press of "Scan for Devices" (which already probes USB on demand). The affected population is narrow: a desktop user, with a USB scale, who has the setting off. **No migration** — the setting is not silently enabled for anyone.

## Capabilities

### New Capabilities
- `usb-device-discovery`: How the app detects a supported USB device — DE1 or scale — being attached and detached: the single Android hotplug receiver covering both kinds, the scanning fallback and its interval, and the user-facing setting that governs periodic scanning only.

### Modified Capabilities

None. `wifi-scale-discovery` already requires that "Scan for Devices" runs a USB probe pass concurrently; that requirement is unchanged and becomes the manual recovery path when the gate is off.

## Impact

- `src/main.cpp:2154` — gate `usbScaleManager.startPolling()` and wire it to `usbSerialEnabledChanged`, matching the DE1 block above it.
- `src/usb/usbscalemanager.{h,cpp}` — accept hotplug attach/detach signals; keep `startPolling`/`stopPolling` as the fallback.
- `src/usb/androidusbscalehelper.{h,cpp}` and `src/usb/androidusbhelper.{h,cpp}` — JNI surface for receiver registration and the attach/detach callback, dispatched to the right manager by device identifier.
- `src/usb/usbmanager.{h,cpp}` — accept hotplug attach/detach for the DE1, alongside its existing gated poll.
- `android/src/io/github/kulitorum/decenza_de1/` — the shared `BroadcastReceiver`, registered and unregistered with the activity lifecycle, covering both device kinds.
- `src/core/settings.{h,cpp}` — the broadened setting's documentation; storage key and `false` default both unchanged.
- `qml/pages/settings/SettingsConnectionsTab.qml` — label, description and accessible name; translation keys `connections.usb.serialLabel`, `connections.usb.serialDesc`, `settings.connections.accessible.enableSerialUsb`.
- Wiki manual — the toggle's meaning changes for users who have one; the Connections page entry needs updating.
- Desktop (macOS/Windows/Linux) gains no hotplug. Polling remains the only mechanism there, now gated.
