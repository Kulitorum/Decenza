#pragma once

#include <QByteArray>
#include <QString>
#include "core/hdsfirmwarecatalog.h"
#include <cstdint>

// Decent Scale 7-byte binary packet protocol, shared by BLE and USB paths.
// Packet format: [PacketHeader, type, data0, data1, data2, data3, XOR]
namespace DecentScaleProtocol {

// Every frame this protocol sends or receives opens with it, and the USB
// framer keys on it to tell a command from text.
inline constexpr char PacketHeader = 0x03;

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

inline QString firmwareUpdateNotConnectedMessage() {
    return QStringLiteral("Firmware update command dropped - scale not connected");
}

inline QString firmwareUpdateStartingMessage(const QString& targetVersion) {
    return QStringLiteral("Starting firmware update to %1").arg(targetVersion);
}

// A targeted WiFi-update request (opcode 0x1B) carries the release to install
// as three payload bytes, each biased with 0x80. The bias is what lets one
// request form be correct against every scale: firmware carrying openscale
// PR #165 disambiguates on `data[2] >= 0x80` (include/decent_protocol.h, the
// 0x1B arm), and older firmware ignores the payload — on Bluetooth because
// that path has no framer and never reads past data[1] (include/ble.h), and on
// USB because the frame ends at two bytes and the rest falls to the text path
// (include/usbcomm.h, bufferedTextLength).
//
// On USB the bias is a safety property rather than a framing convenience.
// bufferedTextLength splits a text run at the first 0x03, so an UNBIASED
// payload would be re-entered as a command once the frame timeout expired: a
// 3.10.2 target leaves `03 0A 02`, which older firmware reads as power-off
// (include/decent_protocol.h). Biasing puts every payload byte in 0x80..0xFF,
// which no frame start can match.
//
// Verified against openscale PR #165 (bf425cf), re-checked at 910deb9.
inline constexpr uint8_t OtaTargetByteBias = 0x80;
inline constexpr int OtaTargetPayloadBytes = 3;

// "3.1.14" -> 1B 83 81 8E. Empty for any version HdsFirmwareCatalog::parseVersion
// rejects.
//
// Empty rather than a bare 0x1B, and every caller must honour that: a bare 0x1B
// is a VALID command that drops the scale into its own on-display picker. Sending
// one because a target failed to resolve would silently downgrade the release the
// user confirmed in the dialog into a second choice they must make on the
// hardware — the exact interaction this feature removes.
inline QByteArray buildTargetedFirmwareUpdateCommand(const QString& version) {
    const auto components = HdsFirmwareCatalog::parseVersion(version);
    if (!components)
        return {};

    QByteArray command(1, char(0x1B));
    for (const int component : *components)
        command.append(char(OtaTargetByteBias | uint8_t(component)));
    return command;
}

} // namespace DecentScaleProtocol
