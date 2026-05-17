#pragma once

#include <QtGlobal>

// Chains into qInstallMessageHandler to drop spurious Qt messages before
// they reach any other handler:
//   - "Missing CAP_NET_ADMIN permission..." bluetooth warning (Linux only)
//     when BleCapability::linuxMissing() is false — Qt prints it under
//     conditions that don't always mean caps are missing, misleading users
//     who have already granted the capability.
//   - "window doesn't exist." (all platforms) — QTBUG-146779, a harmless
//     critical-severity message Qt emits during startup/teardown.
//
// Install AFTER AsyncLogger, CrashHandler, and WebDebugLogger so this
// filter sits at the top of the static chain and can suppress the false
// messages before they reach any other handler.
namespace BtLogFilter {

void install();

} // namespace BtLogFilter
