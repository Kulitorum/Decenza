#pragma once

#include <QByteArray>

#include <cmath>
#include <cstdint>
#include <optional>

// Atomax Skale II weight notifications (characteristic EF81).
//
// Frame: [flags, mantissa LE 24-bit signed, exponent int8], weight = mantissa * 10^exponent.
namespace SkaleScaleProtocol {

inline constexpr qsizetype WeightFrameLength = 5;

// The exponent range this driver will act on. The SDK format permits any int8,
// but nothing between a microgram and a tonne needs more, and the value reached
// here feeds stop-at-weight: 10^127 would arrive as a weight, not as an error.
inline constexpr int MinExponent = -6;
inline constexpr int MaxExponent = 6;

// Grams, or nullopt when the frame is not one this decoder can read.
//
// The exponent is the point. Decenza, de1app (bluetooth.tcl, `binary scan ...
// cus1cu` then `t1 / 10.0`) and decaid all read bytes 1-2 as a 16-bit mantissa
// and divided by ten, which is correct ONLY for exponent -1 -- the one value
// every observed scale happens to send. A frame carrying any other exponent
// decoded to a weight wrong by a factor of ten or a hundred:
//
//   00 D2 04 00 FE   1234 x 10^-2 = 12.34 g,  read as 123.4 g
//   00 7B 00 00 01    123 x 10^1  = 1230 g,   read as 12.3 g
//
// Sourced from decentespresso/decaid#722 and its issue #712, which carry the
// Atomax SDK's own decode and those examples. No Skale has been observed
// sending another exponent, so this is a latent misread rather than a live
// fault -- worth correcting because the consumer is stop-at-weight.
inline std::optional<double> decodeWeightGrams(const QByteArray& frame) {
    if (frame.size() != WeightFrameLength)
        return std::nullopt;

    const uint8_t* d = reinterpret_cast<const uint8_t*>(frame.constData());

    // Sign-extend from bit 23. Reading only bytes 1-2 as int16 got small
    // negatives right by accident -- their low 16 bits match -- and everything
    // past +-32.767 g at exponent -1 wrong.
    int32_t mantissa = int32_t(d[1]) | (int32_t(d[2]) << 8) | (int32_t(d[3]) << 16);
    if (mantissa & 0x800000)
        mantissa -= 0x1000000;

    const int exponent = static_cast<int8_t>(d[4]);
    if (exponent < MinExponent || exponent > MaxExponent)
        return std::nullopt;

    // Divide rather than multiply by a negative power: std::pow(10, n) for a
    // small non-negative n is an exact integer, so the result is one correctly
    // rounded operation away from the true value.
    const double scale = std::pow(10.0, std::abs(exponent));
    return exponent < 0 ? mantissa / scale : mantissa * scale;
}

} // namespace SkaleScaleProtocol
