#pragma once

#include <QtGlobal>

// Chains into qInstallMessageHandler to drop Qt's "Missing CAP_NET_ADMIN
// permission..." bluetooth warning when BleCapability::linuxMissing() is
// false. Qt prints that warning under conditions where it isn't actually
// caused by missing caps (see issue #804 follow-up) which misleads users
// into running setcap despite caps already being effective.
//
// Installation order matters — install AFTER AsyncLogger so we sit above
// it in the handler chain and reach messages before they're enqueued:
//   qDebug() → ShotDebugLogger → WebDebugLogger → CrashHandler → BtLogFilter → AsyncLogger
//
// No-op on non-Linux.
namespace BtLogFilter {

void install();

} // namespace BtLogFilter
