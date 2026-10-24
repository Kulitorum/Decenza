#pragma once

#include <cstdint>

namespace DE1 {

// Profile frame encoding is shared by the editor and the native BLE transport.
enum FrameFlag : uint8_t {
    CtrlF       = 0x01,  // Flow control mode (else pressure control)
    DoCompare   = 0x02,  // Enable exit condition checking
    DC_GT       = 0x04,  // Exit if > threshold (else <)
    DC_CompF    = 0x08,  // Compare flow (else pressure)
    TMixTemp    = 0x10,  // Target mix temperature (else basket temp)
    Interpolate = 0x20,  // Ramp smoothly (else instant jump)
    IgnoreLimit = 0x40   // Ignore min pressure/max flow limits
};

} // namespace DE1
