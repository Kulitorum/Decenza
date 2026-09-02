#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>
#include "core/hdsfirmwarecatalog.h"
#include <cstdint>

// Decent Scale binary packet protocol, shared by BLE and USB paths.
// Packet format: [PacketHeader, type, data0, data1, data2, data3, XOR] -- plus
// the 10-byte v1.2 weight frame and the 41-byte ADS debug frame below.
namespace DecentScaleProtocol {

// Every frame this protocol sends or receives opens with it, and the USB
// framer keys on it to tell a command from text.
inline constexpr char PacketHeader = 0x03;

// Frame type (byte 1) of the notified frames whose length or checksum rule
// differs from the ordinary 7-byte packet.
//
// 0x0A is the LED response, whose bytes 5-6 are the firmware version rather
// than a checksum. It is also buildHeartBeatPacket's type (openscale
// include/decent_protocol.h:76), where those bytes are 0x00 0x0A -- so if
// sendBleHeartBeat() ever gains a caller (it has none today) this driver will
// read a heartbeat as firmware "0.0.10" on a charging scale.
inline constexpr uint8_t TypeLedResponse = 0x0A;
inline constexpr uint8_t TypeAdsDebug = 0x25;  // 41 bytes, checksum in byte 40

// Weight frame types. 0xCE is what the firmware notifies; 0xCA is accepted too
// because de1app has always accepted both (binary.tcl:1438).
inline constexpr uint8_t TypeWeight = 0xCE;
inline constexpr uint8_t TypeWeightAlt = 0xCA;

inline constexpr qsizetype StandardFrameLength = 7;
inline constexpr qsizetype AdsDebugFrameLength = 41;
// Decent Scale v1.2 firmware notifies weight in TEN bytes, not seven: the
// 7-byte layout with minutes/seconds/milliseconds inserted after the weight
// short, two unused bytes, and the XOR moved to byte 9. Weight stays at bytes
// 2-3, so only the frame length and the checksum position differ.
// (de1app decent_scale_weight_read_spec_v12, binary.tcl:350-364, dispatched at
// :1424-1427.) The original scale has no USB, so this length is notified only.
inline constexpr qsizetype V12WeightFrameLength = 10;

// XOR checksum: XOR of all bytes except the last (byte 6 in a 7-byte packet).
inline uint8_t calculateXor(const QByteArray& data) {
    uint8_t result = 0;
    for (qsizetype i = 0; i < data.size() - 1; i++) {
        result ^= static_cast<uint8_t>(data[i]);
    }
    return result;
}

// How many bytes the frame for `command` needs, or 0 when fewer than that have
// arrived. It is a MINIMUM check: every type but 0x25 maps to 7, so an unknown
// type is not rejected here. For a stream framer that is the right question --
// its buffer legitimately holds more than one frame. A caller that wants
// exactness must use notifiedFrameLengthExact() and NOT compare this result
// against its own frame size: this one never returns 10, so that comparison
// rejects a v1.2 weight frame.
//
// Sourced from the firmware, not inferred: every openscale notify goes through
// bleNotifyReadPacket() (openscale include/ble.h:535) and there are exactly
// eight call sites, :558-615 -- seven at 7 bytes (weight 0xCE, button 0xAA,
// voltage, heartbeat, gyro, power-off 0x2A, LED response 0x0A) and one at 41
// (ADS debug 0x25). 0xCA is not among them; this driver accepts it as a weight
// type anyway.
//
// de1app 3abea2fb reports the scale also notifying 2/4/8/12/16-byte frames.
// Two of those are host-to-scale COMMAND lengths (decentCommandFrameLength,
// openscale include/decent_protocol_frame.h:52-126, which returns 1-7 and never
// 8, 12 or 16), and the other three are unexplained by this firmware. Undecoded
// lengths are logged rather than parsed, so do not widen this table until one
// of them has a source.
inline qsizetype notifiedFrameLength(uint8_t command, qsizetype available) {
    const qsizetype expected =
        (command == TypeAdsDebug) ? AdsDebugFrameLength : StandardFrameLength;
    return available >= expected ? expected : 0;
}

// The same question for a caller holding exactly one frame: a notification. It
// returns `len` when that is a length this command is notified at, and 0 when
// it is not -- which notifiedFrameLength() cannot express, since a stream
// framer's buffer legitimately holds more than one frame.
//
// Only weight is accepted at ten bytes. de1app unpacks any 10-byte frame with
// the v1.2 layout (binary.tcl:1424-1427) but decodes only weight from it
// (:1438-1462), and the 7-byte LED response's battery and firmware bytes have
// no known v1.2 position -- so a 10-byte frame of another type is logged, not
// guessed at.
inline qsizetype notifiedFrameLengthExact(uint8_t command, qsizetype len) {
    if (command == TypeAdsDebug)
        return len == AdsDebugFrameLength ? len : 0;
    if (len == StandardFrameLength)
        return len;
    if (len == V12WeightFrameLength && (command == TypeWeight || command == TypeWeightAlt))
        return len;
    return 0;
}

// True when the trailing XOR byte of the first `frameLen` bytes matches.
//
// frameLen is required because calculateXor() runs to data.size()-1: handed a
// buffer longer than the frame it XORs bytes the checksum never covered and
// compares the result against the wrong byte.
//
// Callers must pass a frameLen the buffer actually holds. A false return means
// the checksum FAILED, and callers spend that against the v1 auto-disable
// budget (#630) -- so it must never also mean "could not be evaluated".
inline bool checksumMatches(const QByteArray& data, qsizetype frameLen) {
    Q_ASSERT(frameLen >= 2 && data.size() >= frameLen);
    const QByteArray frame = data.left(frameLen);
    return static_cast<uint8_t>(frame[frameLen - 1]) == calculateXor(frame);
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
