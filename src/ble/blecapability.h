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

} // namespace BleCapability
