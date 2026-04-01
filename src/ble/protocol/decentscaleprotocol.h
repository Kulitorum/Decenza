#pragma once

#include <QByteArray>
#include <cstdint>

// Decent Scale 7-byte binary packet protocol, shared by BLE and USB paths.
// Packet format: [0x03, type, data0, data1, data2, data3, XOR]
namespace DecentScaleProtocol {

// XOR checksum: XOR of all bytes except the last (byte 6 in a 7-byte packet).
inline uint8_t calculateXor(const QByteArray& data) {
    uint8_t result = 0;
    for (qsizetype i = 0; i < data.size() - 1; i++) {
        result ^= static_cast<uint8_t>(data[i]);
    }
    return result;
}

} // namespace DecentScaleProtocol
