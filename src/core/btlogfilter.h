#pragma once

// Chains into qInstallMessageHandler to drop Qt Bluetooth's "Missing CAP_NET_ADMIN
// permission..." warning (Linux only) when BleCapability::linuxMissing() is false:
// Qt prints it under conditions that don't always mean caps are missing,
// misleading users who have already granted the capability.
//
// Install AFTER AsyncLogger, CrashHandler, and WebDebugLogger so this
// filter sits at the top of the static chain.
namespace BtLogFilter {

void install();

} // namespace BtLogFilter
