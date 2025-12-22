#pragma once

#include <cstdint>
#include <QByteArray>

/**
 * Binary codec for DE1 protocol fixed-point number formats.
 *
 * Naming convention:
 *   U = unsigned, S = signed
 *   8/16/24/32 = total bits
 *   P = decimal point position (fractional bits)
 *
 * Examples:
 *   U8P4  = 8-bit unsigned, 4 fractional bits, range 0-15.9375
 *   U8P1  = 8-bit unsigned, 1 fractional bit,  range 0-127.5
 *   U16P8 = 16-bit unsigned, 8 fractional bits, range 0-255.996
 *   F8_1_7 = custom float format for durations
 */
class BinaryCodec {
public:
    // U8P4: 8-bit with 4 fractional bits (range 0-15.9375)
    // Used for: pressure (bar), flow (mL/s)
    static uint8_t encodeU8P4(double value);
    static double decodeU8P4(uint8_t value);

    // U8P1: 8-bit with 1 fractional bit (range 0-127.5)
    // Used for: temperature (Celsius)
    static uint8_t encodeU8P1(double value);
    static double decodeU8P1(uint8_t value);

    // U8P0: 8-bit integer (range 0-255)
    // Used for: volume, time, counts
    static uint8_t encodeU8P0(double value);
    static double decodeU8P0(uint8_t value);

    // U16P8: 16-bit with 8 fractional bits (range 0-255.996)
    // Used for: precise temperature
    static uint16_t encodeU16P8(double value);
    static double decodeU16P8(uint16_t value);

    // S32P16: Signed 32-bit with 16 fractional bits
    // Used for: calibration values
    static int32_t encodeS32P16(double value);
    static double decodeS32P16(int32_t value);

    // F8_1_7: Custom float format for frame duration
    // If value < 12.75: uses 0.1s precision
    // If value >= 12.75: uses 1s precision with high bit set
    static uint8_t encodeF8_1_7(double value);
    static double decodeF8_1_7(uint8_t value);

    // U10P0: 10-bit integer with flag bit (bit 10 set)
    // Used for: volume limits in shot frames
    static uint16_t encodeU10P0(double value);
    static double decodeU10P0(uint16_t value);

    // U24P0: 24-bit big-endian integer
    // Used for: MMR addresses
    static QByteArray encodeU24P0(uint32_t value);
    static uint32_t decodeU24P0(const QByteArray& data);
    static uint32_t decodeU24P0(uint8_t high, uint8_t mid, uint8_t low);

    // U32P0: 32-bit big-endian integer
    static QByteArray encodeU32P0(uint32_t value);
    static uint32_t decodeU32P0(const QByteArray& data);

    // Utility: Convert 3 chars to U24P16 fixed point
    static double decode3CharToU24P16(uint8_t char1, uint8_t char2, uint8_t char3);

    // Short (16-bit) big-endian encoding/decoding
    static QByteArray encodeShortBE(uint16_t value);
    static uint16_t decodeShortBE(const QByteArray& data, int offset = 0);
    static int16_t decodeSignedShortBE(const QByteArray& data, int offset = 0);
};
