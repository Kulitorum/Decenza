#pragma once

#include <QtGlobal>

// Chains into qInstallMessageHandler to drop Qt's "Missing CAP_NET_ADMIN
// permission..." bluetooth warning when BleCapability::linuxMissing() is
// false — Qt prints the warning under conditions that don't always mean
// caps are missing, and the false alarm misleads users who have already
// granted the capability.
//
// Install AFTER AsyncLogger, CrashHandler, and WebDebugLogger so this
// filter sits at the top of the static chain and can suppress the false
// warning before it reaches any other handler.
//
// No-op on non-Linux.
namespace BtLogFilter {

void install();

} // namespace BtLogFilter
