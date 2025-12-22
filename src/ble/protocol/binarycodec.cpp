#include "binarycodec.h"
#include <algorithm>
#include <cmath>

// U8P4: 8-bit with 4 fractional bits (range 0-15.9375)
uint8_t BinaryCodec::encodeU8P4(double value) {
    value = std::clamp(value, 0.0, 16.0);
    return static_cast<uint8_t>(std::round(value * 16.0));
}

double BinaryCodec::decodeU8P4(uint8_t value) {
    return value / 16.0;
}

// U8P1: 8-bit with 1 fractional bit (range 0-127.5)
uint8_t BinaryCodec::encodeU8P1(double value) {
    value = std::clamp(value, 0.0, 128.0);
    return static_cast<uint8_t>(std::round(value * 2.0));
}

double BinaryCodec::decodeU8P1(uint8_t value) {
    return value / 2.0;
}

// U8P0: 8-bit integer (range 0-255)
uint8_t BinaryCodec::encodeU8P0(double value) {
    value = std::clamp(value, 0.0, 256.0);
    return static_cast<uint8_t>(std::round(value));
}

double BinaryCodec::decodeU8P0(uint8_t value) {
    return static_cast<double>(value);
}

// U16P8: 16-bit with 8 fractional bits (range 0-255.996)
uint16_t BinaryCodec::encodeU16P8(double value) {
    value = std::clamp(value, 0.0, 256.0);
    return static_cast<uint16_t>(std::round(value * 256.0));
}

double BinaryCodec::decodeU16P8(uint16_t value) {
    return value / 256.0;
}

// S32P16: Signed 32-bit with 16 fractional bits
int32_t BinaryCodec::encodeS32P16(double value) {
    value = std::clamp(value, -65536.0, 65536.0);
    return static_cast<int32_t>(std::round(value * 65536.0));
}

double BinaryCodec::decodeS32P16(int32_t value) {
    return value / 65536.0;
}

// F8_1_7: Custom float format for frame duration
// If value < 12.75: encode as round(value * 10), 0.1s precision
// If value >= 12.75: encode as round(value) | 0x80, 1s precision
uint8_t BinaryCodec::encodeF8_1_7(double value) {
    if (value < 12.75) {
        return static_cast<uint8_t>(std::round(value * 10.0));
    } else {
        value = std::clamp(value, 0.0, 127.0);
        return static_cast<uint8_t>(std::round(value)) | 0x80;
    }
}

double BinaryCodec::decodeF8_1_7(uint8_t value) {
    if ((value & 0x80) == 0) {
        // Low precision mode: value / 10.0
        return value / 10.0;
    } else {
        // High precision mode: strip high bit
        return static_cast<double>(value & 0x7F);
    }
}

// U10P0: 10-bit integer with flag bit (bit 10 always set)
uint16_t BinaryCodec::encodeU10P0(double value) {
    uint16_t intVal = static_cast<uint16_t>(std::round(std::clamp(value, 0.0, 1023.0)));
    return intVal | 0x0400;  // Set bit 10 as flag
}

double BinaryCodec::decodeU10P0(uint16_t value) {
    return static_cast<double>(value & 0x03FF);  // Mask to 10 bits
}

// U24P0: 24-bit big-endian integer
QByteArray BinaryCodec::encodeU24P0(uint32_t value) {
    QByteArray result(3, 0);
    result[0] = static_cast<char>((value >> 16) & 0xFF);
    result[1] = static_cast<char>((value >> 8) & 0xFF);
    result[2] = static_cast<char>(value & 0xFF);
    return result;
}

uint32_t BinaryCodec::decodeU24P0(const QByteArray& data) {
    if (data.size() < 3) return 0;
    return decodeU24P0(
        static_cast<uint8_t>(data[0]),
        static_cast<uint8_t>(data[1]),
        static_cast<uint8_t>(data[2])
    );
}

uint32_t BinaryCodec::decodeU24P0(uint8_t high, uint8_t mid, uint8_t low) {
    return (static_cast<uint32_t>(high) << 16) |
           (static_cast<uint32_t>(mid) << 8) |
           static_cast<uint32_t>(low);
}

// U32P0: 32-bit big-endian integer
QByteArray BinaryCodec::encodeU32P0(uint32_t value) {
    QByteArray result(4, 0);
    result[0] = static_cast<char>((value >> 24) & 0xFF);
    result[1] = static_cast<char>((value >> 16) & 0xFF);
    result[2] = static_cast<char>((value >> 8) & 0xFF);
    result[3] = static_cast<char>(value & 0xFF);
    return result;
}

uint32_t BinaryCodec::decodeU32P0(const QByteArray& data) {
    if (data.size() < 4) return 0;
    return (static_cast<uint32_t>(static_cast<uint8_t>(data[0])) << 24) |
           (static_cast<uint32_t>(static_cast<uint8_t>(data[1])) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(data[2])) << 8) |
           static_cast<uint32_t>(static_cast<uint8_t>(data[3]));
}

// Utility: Convert 3 chars to U24P16 fixed point
double BinaryCodec::decode3CharToU24P16(uint8_t char1, uint8_t char2, uint8_t char3) {
    return static_cast<double>(char1) +
           (static_cast<double>(char2) / 256.0) +
           (static_cast<double>(char3) / 65536.0);
}

// Short (16-bit) big-endian encoding
QByteArray BinaryCodec::encodeShortBE(uint16_t value) {
    QByteArray result(2, 0);
    result[0] = static_cast<char>((value >> 8) & 0xFF);
    result[1] = static_cast<char>(value & 0xFF);
    return result;
}

uint16_t BinaryCodec::decodeShortBE(const QByteArray& data, int offset) {
    if (data.size() < offset + 2) return 0;
    return (static_cast<uint16_t>(static_cast<uint8_t>(data[offset])) << 8) |
           static_cast<uint16_t>(static_cast<uint8_t>(data[offset + 1]));
}

int16_t BinaryCodec::decodeSignedShortBE(const QByteArray& data, int offset) {
    return static_cast<int16_t>(decodeShortBE(data, offset));
}
