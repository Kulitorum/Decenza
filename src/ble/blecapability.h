#pragma once

#include <QString>

// Detection of the Linux CAP_NET_ADMIN capability required by BlueZ to
// distinguish random from public BLE addresses. Without it, connects to
// random-address peripherals like the DE1 fail with UnknownRemoteDeviceError.
// Lives in its own TU so lightweight test binaries can link against it
// without pulling in all of BLEManager's scale/refractometer dependencies.
namespace BleCapability {

// Returns true only on Linux (non-Android) when the current process lacks
// effective CAP_NET_ADMIN. Cached after the first call.
bool linuxMissing();

// The exact `sudo setcap 'cap_net_admin+eip' <binary>` command that grants
// the capability. Empty string on non-Linux or when the capability is
// already present.
QString linuxSetcapCommand();

// Dump a one-shot block of Linux BT diagnostics (CapEff/CapBnd, getcap,
// BlueZ version, hciconfig -a) to the debug log via qInfo(). Safe to call
// from any BLE error path; fires at most once per process and is a no-op
// on non-Linux. Designed to flow into the debug log that the issue
// template attaches to bug reports.
void logLinuxBtDiagnosticsOnce();

// One-shot latch for the BlueZ-cache recovery hint. Returns true the
// first time it is called in a process, false thereafter. Used by
// transport error paths to emit the hint signal only once per session.
bool takeBluezCacheHintToken();

} // namespace BleCapability
