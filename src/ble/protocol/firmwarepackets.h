#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include <QByteArray>

// DE1 firmware-update packet builders and notification parser.
//
// Three wire formats. The chunk format below was corrected against Kal
// Freese's working Python updater (kalfreese/de1-firmware-updater) after
// the original byte order (sourced from a misread of de1app's binary.tcl)
// caused a real-DE1 flash to land at wrong addresses, trigger pumps, and
// be rejected by the bootloader.
//
//   FWMapRequest (A009, write/notify) — 7 bytes
//     0-1: WindowIncrement (u16, big-endian; always 0 on write)
//     2:   FWToErase (1 = erase phase, 0 = verify phase)
//     3:   FWToMap   (1 = enable new firmware)
//     4-6: FirstError (0,0,0 on erase write; 0xFF,0xFF,0xFF on verify write;
//          DE1 fills with first-error-address or 0xFF,0xFF,0xFD for success)
//
//   Firmware chunk (A006, write, same characteristic as MMR writes) — 20 bytes
//     0:    Length (number of payload bytes — 16 for firmware chunks,
//                   matching the same-characteristic MMR-write "0x04")
//     1:    Address high byte (bits 16..23)
//     2-3:  Address low 16 bits (BIG-endian)
//     4-19: Payload (exactly 16 bytes)
//
// Unit-tested in tests/tst_firmwarepackets.cpp. No Qt dependency beyond
// QByteArray, so the tests run without the BLE stack, network, or GUI.

namespace DE1::Firmware {

// Marker the DE1 writes into FirstError after a successful verify.
inline constexpr std::array<uint8_t, 3> VERIFY_SUCCESS = {0xFF, 0xFF, 0xFD};

inline QByteArray buildFWMapRequest(uint8_t fwToErase, uint8_t fwToMap,
                                    std::array<uint8_t, 3> firstError = {0, 0, 0}) {
    QByteArray packet(7, char(0));
    // Bytes 0-1 (WindowIncrement, big-endian) stay zero.
    packet[2] = static_cast<char>(fwToErase);
    packet[3] = static_cast<char>(fwToMap);
    packet[4] = static_cast<char>(firstError[0]);
    packet[5] = static_cast<char>(firstError[1]);
    packet[6] = static_cast<char>(firstError[2]);
    return packet;
}

inline QByteArray buildChunk(uint32_t address, const QByteArray& payload16) {
    if (payload16.size() != 16) {
        return QByteArray();
    }
    QByteArray packet(4, char(0));
    packet[0] = char(16);                                       // length = 16 bytes
    packet[1] = static_cast<char>((address >> 16) & 0xFF);      // BE byte 0 (high)
    packet[2] = static_cast<char>((address >> 8) & 0xFF);       // BE byte 1 (mid)
    packet[3] = static_cast<char>(address & 0xFF);              // BE byte 2 (low)
    packet.append(payload16);
    return packet;
}

struct FWMapNotification {
    uint16_t windowIncrement;
    uint8_t  fwToErase;
    uint8_t  fwToMap;
    std::array<uint8_t, 3> firstError;
};

inline std::optional<FWMapNotification> parseFWMapNotification(const QByteArray& data) {
    if (data.size() < 7) {
        return std::nullopt;
    }
    FWMapNotification out;
    out.windowIncrement =
        (static_cast<uint16_t>(static_cast<uint8_t>(data[0])) << 8) |
         static_cast<uint16_t>(static_cast<uint8_t>(data[1]));
    out.fwToErase     = static_cast<uint8_t>(data[2]);
    out.fwToMap       = static_cast<uint8_t>(data[3]);
    out.firstError[0] = static_cast<uint8_t>(data[4]);
    out.firstError[1] = static_cast<uint8_t>(data[5]);
    out.firstError[2] = static_cast<uint8_t>(data[6]);
    return out;
}

}  // namespace DE1::Firmware
