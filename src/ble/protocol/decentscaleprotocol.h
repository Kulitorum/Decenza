#pragma once

#include <QByteArray>
#include <QString>
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

// HDS LED responses carry the firmware triple in their last two bytes: the
// major version is BCD packed and the minor and patch values occupy one nibble
// each. Keep this transport-neutral because USB and Bluetooth receive the same
// packet shape.
inline QString decodeHdsFirmwareVersion(uint8_t packedMajor, uint8_t packedMinorPatch) {
    const int major = ((packedMajor >> 4) & 0x0F) * 10 + (packedMajor & 0x0F);
    const int minor = (packedMinorPatch >> 4) & 0x0F;
    const int patch = packedMinorPatch & 0x0F;
    return QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
}

} // namespace DecentScaleProtocol
