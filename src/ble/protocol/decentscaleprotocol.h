#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
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

// The three firmware-update diagnostics, written once. All three HDS drivers
// emit them through their own subsystem marker, and this subsystem is
// diagnosed from user-submitted logs — where a reader cannot know that the
// same event is worded differently depending on which transport produced it.
inline QString firmwareUpdateUnknownVersionMessage() {
    return QStringLiteral("Firmware update command dropped - HDS firmware version is unknown");
}

inline QString firmwareUpdateBadTargetMessage(const QString& targetVersion) {
    return QStringLiteral("Firmware update command dropped - unparsable target version '%1'")
        .arg(targetVersion);
}

inline QString firmwareUpdateStartingMessage(const QString& targetVersion) {
    return QStringLiteral("Starting firmware update to %1").arg(targetVersion);
}

// A targeted WiFi-update request (opcode 0x1B) carries the release to install
// as three payload bytes, each biased with 0x80. The bias is what makes one
// request form correct against every scale: firmware predating openscale
// PR #165 frames `03 1B` as two bytes and discards the payload through its
// text path, while firmware carrying it disambiguates on `data[2] >= 0x80`.
// A following command always begins 0x03, and a biased byte never can.
//
// The bias is also a safety property rather than a framing convenience. An
// unbiased payload would collide with real commands on older firmware: a
// 3.10.2 target would decode as `03 0A 02`, the power-off command.
//
// Encode in one place. Neither transport driver may hand-roll this.
inline constexpr uint8_t OtaTargetByteBias = 0x80;
inline constexpr uint8_t OtaTargetMaxComponent = 127;

inline uint8_t encodeOtaTargetByte(int value) {
    const int clamped = value < 0 ? 0 : (value > OtaTargetMaxComponent ? OtaTargetMaxComponent : value);
    return static_cast<uint8_t>(OtaTargetByteBias | static_cast<uint8_t>(clamped));
}

// "3.1.14" -> 1B 83 81 8E. Returns an empty array for anything that does not
// parse as a major.minor.patch triple, so a caller cannot accidentally send a
// bare 0x1B — which would start the scale's own interactive picker — by
// passing a version it failed to resolve.
inline QByteArray buildTargetedFirmwareUpdateCommand(const QString& version) {
    const QStringList parts = version.split(QLatin1Char('.'));
    if (parts.size() != 3)
        return {};

    QByteArray command;
    command.append(static_cast<char>(0x1B));
    for (const QString& part : parts) {
        bool ok = false;
        const int component = part.toInt(&ok);
        if (!ok || component < 0)
            return {};
        command.append(static_cast<char>(encodeOtaTargetByte(component)));
    }
    return command;
}

} // namespace DecentScaleProtocol
