#pragma once

#include <QtGlobal>

// The stall decision for the USB serial link, as a pure predicate.
//
// Split out from SerialTransport so it can be tested at all. The transport
// itself cannot be: open() needs a real port, write() early-returns unless
// m_connected, and the threshold is a minute — so the logic that decides whether
// a silent DE1 gets a warning would otherwise ship with no coverage. A
// silent-failure detector that fails silently is worse than none, because the
// absence of its warning reads as "the link is fine".
//
// See the m_inboundLiveness block in serialtransport.h for what feeds this and
// why the threshold is what it is.
namespace SerialStall {

// True when a write should emit the stall warning.
//
//   alreadyWarned  the per-episode latch. One warning per stall, not one per
//                  write — writes are frequent, and an unlatched check is the
//                  flat-repeat noise this whole change exists to remove.
//   livenessValid  false before the port is open and after it closes. A closed
//                  port has no stream to be stale; without this gate a
//                  default-constructed timer would read as an infinite stall.
//   elapsedMs      since the last inbound line.
//   thresholdMs    INBOUND_STALE_MS in production; small in tests.
inline bool shouldWarn(bool alreadyWarned, bool livenessValid,
                       qint64 elapsedMs, qint64 thresholdMs)
{
    return !alreadyWarned && livenessValid && elapsedMs > thresholdMs;
}

} // namespace SerialStall
