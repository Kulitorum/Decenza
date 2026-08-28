#pragma once

#include <QBluetoothUuid>
#include <QByteArray>
#include <QString>

#include <cstdint>
#include <optional>

#include "binarycodec.h"

namespace DE1 {

// Primary Service UUID
const QBluetoothUuid SERVICE_UUID(QString("0000A000-0000-1000-8000-00805F9B34FB"));

// Characteristic UUIDs
namespace Characteristic {
    // Version - Read: Get firmware and BLE API version
    const QBluetoothUuid VERSION(QString("0000A001-0000-1000-8000-00805F9B34FB"));

    // RequestedState - Write: Command machine state changes
    const QBluetoothUuid REQUESTED_STATE(QString("0000A002-0000-1000-8000-00805F9B34FB"));

    // ReadFromMMR - Read/Notify: Read memory-mapped registers
    const QBluetoothUuid READ_FROM_MMR(QString("0000A005-0000-1000-8000-00805F9B34FB"));

    // WriteToMMR - Write: Write memory-mapped registers
    const QBluetoothUuid WRITE_TO_MMR(QString("0000A006-0000-1000-8000-00805F9B34FB"));

    // FWMapRequest - Write/Notify: Firmware update
    const QBluetoothUuid FW_MAP_REQUEST(QString("0000A009-0000-1000-8000-00805F9B34FB"));

    // Temperatures - Read/Notify: Temperature readings
    const QBluetoothUuid TEMPERATURES(QString("0000A00A-0000-1000-8000-00805F9B34FB"));

    // ShotSettings - Read/Write: Steam, hot water, and flush settings
    const QBluetoothUuid SHOT_SETTINGS(QString("0000A00B-0000-1000-8000-00805F9B34FB"));

    // ShotSample - Notify: Real-time shot data (~5Hz during extraction)
    const QBluetoothUuid SHOT_SAMPLE(QString("0000A00D-0000-1000-8000-00805F9B34FB"));

    // StateInfo - Read/Notify: Machine state change notifications
    const QBluetoothUuid STATE_INFO(QString("0000A00E-0000-1000-8000-00805F9B34FB"));

    // HeaderWrite - Write: Upload espresso profile header
    const QBluetoothUuid HEADER_WRITE(QString("0000A00F-0000-1000-8000-00805F9B34FB"));

    // FrameWrite - Write: Upload espresso profile frames
    const QBluetoothUuid FRAME_WRITE(QString("0000A010-0000-1000-8000-00805F9B34FB"));

    // WaterLevels - Read/Notify: Water tank level
    const QBluetoothUuid WATER_LEVELS(QString("0000A011-0000-1000-8000-00805F9B34FB"));

    // Calibration - Read/Write: Calibration data
    const QBluetoothUuid CALIBRATION(QString("0000A012-0000-1000-8000-00805F9B34FB"));
}

// Sensor calibration, carried on the CALIBRATION characteristic above.
//
// The record is 14 bytes, big-endian:
//   WriteKey       u32     see the keys below
//   CalCommand     u8      Command
//   CalTarget      u8      Target
//   DE1ReportedVal 32-bit P16  what the machine's own sensor read. de1app reads
//                              this field UNSIGNED; Decenza decodes it signed,
//                              which is harmless because it is only ever
//                              RECEIVED in an echo, whose payload is discarded.
//   MeasuredVal    S32P16      what an external instrument read — and, on a
//                              reply, the machine's stored offset. de1app's spec
//                              marks this one signed, which matters: a machine
//                              reading above the instrument stores a negative.
//
// Source: de1app's `calibrate_spec` (de1plus/binary.tcl:414) and
// de1_send_calibration / de1_read_calibration (de1plus/de1_comms.tcl:1632, :1665).
// There is no DE1 firmware source in this tree, so the arithmetic the machine
// applies to the pair is NOT confirmed here — only that both values are sent and
// that the machine stores a signed offset it reports back. Do not write a comment
// asserting the formula without opening the firmware.
namespace Calibration {
    enum class Target : uint8_t {
        Flow        = 0,
        Pressure    = 1,
        Temperature = 2
    };

    enum class Command : uint8_t {
        ReadCurrent  = 0,
        Write        = 1,
        // Named for completeness of the wire vocabulary; Decenza never sends it.
        // No working implementation of it exists to copy — see the note above
        // DE1Device::readCalibration.
        ResetFactory = 2,
        ReadFactory  = 3
    };

    // The firmware accepts a write only with this key; de1app sends 1 for reads
    // (de1_comms.tcl:1651 for the write key, :1686 for the read key).
    constexpr uint32_t WRITE_KEY = 0xCAFEF00D;
    constexpr uint32_t READ_KEY  = 0x00000001;

    // A REPLY carrying a real stored value has WriteKey == 0; any other value
    // marks it as an echo of a read or write request and it carries no data
    // (de1app's calibration_ble_received, de1plus/bluetooth.tcl:3344).
    constexpr uint32_t REPLY_VALUE_KEY = 0x00000000;

    // Wire size of the packed record.
    constexpr int RECORD_BYTES = 14;

    // One record, parsed. `writeKey` is kept because it is the only thing that
    // distinguishes a reply carrying a real value from an echo — see
    // REPLY_VALUE_KEY above.
    struct Record {
        uint32_t writeKey = 0;
        Command  command  = Command::ReadCurrent;
        Target   target   = Target::Pressure;
        double   reported = 0.0;
        double   measured = 0.0;
    };

    // Pack/parse live here rather than inside DE1Device so the wire format can be
    // tested directly (tests/tst_binarycodec.cpp) without friend access. They are
    // pure functions over the record; nothing about the transport belongs in them.
    inline QByteArray packRecord(const Record& r) {
        const int32_t reported = BinaryCodec::encodeS32P16(r.reported);
        const int32_t measured = BinaryCodec::encodeS32P16(r.measured);

        QByteArray out;
        out.reserve(RECORD_BYTES);
        // Big-endian throughout. de1app gets this from the capital `Int` in
        // calibrate_spec rather than from an endian argument — make_packed_calibration
        // (binary.tcl:245) passes none, and fields::endianness is never set. The
        // `bigeendian` literal (the typo is de1app's) appears only on the unpack
        // side, parse_binary_calibration (binary.tcl:1326).
        for (int shift : {24, 16, 8, 0})
            out.append(static_cast<char>((r.writeKey >> shift) & 0xFF));
        out.append(static_cast<char>(r.command));
        out.append(static_cast<char>(r.target));
        for (int shift : {24, 16, 8, 0})
            out.append(static_cast<char>((static_cast<uint32_t>(reported) >> shift) & 0xFF));
        for (int shift : {24, 16, 8, 0})
            out.append(static_cast<char>((static_cast<uint32_t>(measured) >> shift) & 0xFF));
        return out;
    }

    // Returns nothing on a short or malformed record rather than a
    // default-constructed one: a zero offset reads as "no correction", which is
    // the one wrong answer that looks plausible.
    inline std::optional<Record> parseRecord(const QByteArray& data) {
        if (data.size() < RECORD_BYTES)
            return std::nullopt;

        const auto byteAt = [&data](int i) {
            return static_cast<uint32_t>(static_cast<uint8_t>(data[i]));
        };
        const auto u32At = [&byteAt](int i) {
            return (byteAt(i) << 24) | (byteAt(i + 1) << 16) | (byteAt(i + 2) << 8) | byteAt(i + 3);
        };

        const uint8_t rawCommand = static_cast<uint8_t>(data[4]);
        const uint8_t rawTarget  = static_cast<uint8_t>(data[5]);
        if (rawCommand > static_cast<uint8_t>(Command::ReadFactory))
            return std::nullopt;
        if (rawTarget > static_cast<uint8_t>(Target::Temperature))
            return std::nullopt;

        Record r;
        r.writeKey = u32At(0);
        r.command  = static_cast<Command>(rawCommand);
        r.target   = static_cast<Target>(rawTarget);
        r.reported = BinaryCodec::decodeS32P16(static_cast<int32_t>(u32At(6)));
        r.measured = BinaryCodec::decodeS32P16(static_cast<int32_t>(u32At(10)));
        return r;
    }

    // A reply carries a stored value only when its key says so. Everything else
    // on this characteristic is an echo of a request.
    inline bool replyCarriesValue(const Record& r) {
        return r.writeKey == REPLY_VALUE_KEY;
    }
}

// Machine States (written to REQUESTED_STATE characteristic)
enum class State : uint8_t {
    Sleep           = 0x00,
    GoingToSleep    = 0x01,
    Idle            = 0x02,
    Busy            = 0x03,
    Espresso        = 0x04,
    Steam           = 0x05,
    HotWater        = 0x06,
    ShortCal        = 0x07,
    SelfTest        = 0x08,
    LongCal         = 0x09,
    Descale         = 0x0A,
    FatalError      = 0x0B,
    Init            = 0x0C,
    NoRequest       = 0x0D,
    SkipToNext      = 0x0E,
    HotWaterRinse   = 0x0F,  // Flush
    SteamRinse      = 0x10,
    Refill          = 0x11,
    Clean           = 0x12,
    InBootLoader    = 0x13,
    AirPurge        = 0x14,
    SchedIdle       = 0x15
};

// Machine Substates (received in STATE_INFO notifications)
enum class SubState : uint8_t {
    Ready           = 0,
    Heating         = 1,
    FinalHeating    = 2,
    Stabilising     = 3,
    Preinfusion     = 4,
    Pouring         = 5,
    Ending          = 6,
    Steaming        = 7,
    DescaleInit     = 8,
    DescaleFillGroup= 9,
    DescaleReturn   = 10,
    DescaleGroup    = 11,
    DescaleSteam    = 12,
    CleanInit       = 13,
    CleanFillGroup  = 14,
    CleanSoak       = 15,
    CleanGroup      = 16,
    Refill          = 17,
    PausedSteam     = 18,
    UserNotPresent  = 19,
    Puffing         = 20,
    // Front standby switch is cutting AC power to the machine (no power, cannot make
    // coffee). Firmware < 1337 reports this unreliably — gate any use of it on
    // firmwareBuildNumber() >= 1337, matching de1app.
    Error_NoAC      = 217
};

// Shot frame flags (bit field)
enum FrameFlag : uint8_t {
    CtrlF       = 0x01,  // Flow control mode (else pressure control)
    DoCompare   = 0x02,  // Enable exit condition checking
    DC_GT       = 0x04,  // Exit if > threshold (else <)
    DC_CompF    = 0x08,  // Compare flow (else pressure)
    TMixTemp    = 0x10,  // Target mix temperature (else basket temp)
    Interpolate = 0x20,  // Ramp smoothly (else instant jump)
    IgnoreLimit = 0x40   // Ignore min pressure/max flow limits
};

// Machine models (from MMR 0x80000C)
enum class MachineModel : uint8_t {
    DE1       = 1,
    DE1Plus   = 2,
    DE1Pro    = 3,
    DE1XL     = 4,
    DE1Cafe   = 5
};

// MMR Addresses (Memory-Mapped Registers)
namespace MMR {
    constexpr uint32_t CPU_BOARD_MODEL      = 0x800008;
    constexpr uint32_t MACHINE_MODEL        = 0x80000C;
    constexpr uint32_t FIRMWARE_VERSION     = 0x800010;
    constexpr uint32_t FAN_THRESHOLD        = 0x803808;
    constexpr uint32_t TANK_TEMP_THRESHOLD  = 0x80380C;  // Tank temperature threshold (de1app default: 0 = off)
    constexpr uint32_t PHASE1_FLOW_RATE     = 0x803810;  // Heater warmup flow rate in tenths mL/s (de1app default: 20 = 2.0 mL/s)
    constexpr uint32_t PHASE2_FLOW_RATE     = 0x803814;  // Heater test flow rate in tenths mL/s (de1app default: 40 = 4.0 mL/s)
    constexpr uint32_t HOT_WATER_IDLE_TEMP  = 0x803818;  // Heater idle temperature in tenths °C (de1app default: 990 = 99.0°C)
    constexpr uint32_t GHC_INFO             = 0x80381C;
    constexpr uint32_t GHC_MODE             = 0x803820;
    constexpr uint32_t STEAM_FLOW           = 0x803828;
    constexpr uint32_t STEAM_HIGHFLOW_START = 0x80382C;  // Steam high-flow start (de1app default: 70, no UI)
    constexpr uint32_t SERIAL_NUMBER        = 0x803830;
    constexpr uint32_t HEATER_VOLTAGE       = 0x803834;
    constexpr uint32_t ESPRESSO_WARMUP_TIMEOUT = 0x803838;  // Warmup timeout in tenths of seconds (de1app default: 10 = 1.0s)
    constexpr uint32_t FLOW_CALIBRATION     = 0x80383C;  // Flow calibration multiplier (value = int(1000 * multiplier))
    constexpr uint32_t FLUSH_FLOW_RATE      = 0x803840;  // Flush flow rate in tenths mL/s (de1app: set_flush_flow_rate)
    constexpr uint32_t FLUSH_TIMEOUT        = 0x803848;  // Flush timeout in tenths of seconds (de1app: set_flush_timeout)
    constexpr uint32_t HOT_WATER_FLOW_RATE  = 0x80384C;  // Hot water flow rate in tenths mL/s (de1app default: 10 = 1.0 mL/s)
    constexpr uint32_t STEAM_TWO_TAP_STOP   = 0x803850;  // SteamPurgeMode: 0=off, 1=two taps to stop steam (first tap → puffs, second → purge)
    constexpr uint32_t USB_CHARGER          = 0x803854;  // USB charger on/off (1=on, 0=off)
    constexpr uint32_t REFILL_KIT           = 0x80385C;
}

// Utility functions
inline QString stateToString(State state) {
    switch (state) {
        case State::Sleep:          return "Sleep";
        case State::GoingToSleep:   return "GoingToSleep";
        case State::Idle:           return "Idle";
        case State::Busy:           return "Busy";
        case State::Espresso:       return "Espresso";
        case State::Steam:          return "Steam";
        case State::HotWater:       return "HotWater";
        case State::ShortCal:       return "ShortCal";
        case State::SelfTest:       return "SelfTest";
        case State::LongCal:        return "LongCal";
        case State::Descale:        return "Descale";
        case State::FatalError:     return "FatalError";
        case State::Init:           return "Init";
        case State::NoRequest:      return "NoRequest";
        case State::SkipToNext:     return "SkipToNext";
        case State::HotWaterRinse:  return "Flush";
        case State::SteamRinse:     return "SteamRinse";
        case State::Refill:         return "Refill";
        case State::Clean:          return "Clean";
        case State::InBootLoader:   return "InBootLoader";
        case State::AirPurge:       return "AirPurge";
        case State::SchedIdle:      return "SchedIdle";
        default:                    return "Unknown";
    }
}

inline QString subStateToString(SubState subState) {
    switch (subState) {
        case SubState::Ready:           return "Ready";
        case SubState::Heating:         return "Heating";
        case SubState::FinalHeating:    return "FinalHeating";
        case SubState::Stabilising:     return "Stabilising";
        case SubState::Preinfusion:     return "Preinfusion";
        case SubState::Pouring:         return "Pouring";
        case SubState::Ending:          return "Ending";
        case SubState::Steaming:        return "Steaming";
        case SubState::DescaleInit:     return "DescaleInit";
        case SubState::DescaleFillGroup:return "DescaleFillGroup";
        case SubState::DescaleReturn:   return "DescaleReturn";
        case SubState::DescaleGroup:    return "DescaleGroup";
        case SubState::DescaleSteam:    return "DescaleSteam";
        case SubState::CleanInit:       return "CleanInit";
        case SubState::CleanFillGroup:  return "CleanFillGroup";
        case SubState::CleanSoak:       return "CleanSoak";
        case SubState::CleanGroup:      return "CleanGroup";
        case SubState::Refill:          return "Refill";
        case SubState::PausedSteam:     return "PausedSteam";
        case SubState::UserNotPresent:  return "UserNotPresent";
        case SubState::Puffing:         return "Puffing";
        case SubState::Error_NoAC:      return "Error_NoAC";
        default:                        return "Unknown";
    }
}

} // namespace DE1

// Scale UUIDs
namespace Scale {

// Decent Scale
namespace Decent {
    const QBluetoothUuid SERVICE(QString("0000FFF0-0000-1000-8000-00805F9B34FB"));
    const QBluetoothUuid READ(QString("0000FFF4-0000-1000-8000-00805F9B34FB"));
    const QBluetoothUuid WRITE(QString("000036F5-0000-1000-8000-00805F9B34FB"));
    const QBluetoothUuid WRITEBACK(QString("83CDC3D4-3BA2-13FC-CC5E-106C351A9352"));
}

// Acaia (IPS - older firmware, Lunar/Pearl)
namespace AcaiaIPS {
    const QBluetoothUuid SERVICE(QString("00001820-0000-1000-8000-00805F9B34FB"));
    const QBluetoothUuid CHARACTERISTIC(QString("00002A80-0000-1000-8000-00805F9B34FB"));
}

// Acaia Pyxis (newer firmware)
namespace Acaia {
    const QBluetoothUuid SERVICE(QString("49535343-FE7D-4AE5-8FA9-9FAFD205E455"));
    const QBluetoothUuid STATUS(QString("49535343-1E4D-4BD9-BA61-23C647249616"));
    const QBluetoothUuid CMD(QString("49535343-8841-43F4-A8D4-ECBE34729BB3"));
}

// Felicita
namespace Felicita {
    const QBluetoothUuid SERVICE(QString("0000FFE0-0000-1000-8000-00805F9B34FB"));
    const QBluetoothUuid CHARACTERISTIC(QString("0000FFE1-0000-1000-8000-00805F9B34FB"));
}

// Skale (Atomax)
namespace Skale {
    const QBluetoothUuid SERVICE(QString("0000FF08-0000-1000-8000-00805F9B34FB"));
    const QBluetoothUuid CMD(QString("0000EF80-0000-1000-8000-00805F9B34FB"));
    const QBluetoothUuid WEIGHT(QString("0000EF81-0000-1000-8000-00805F9B34FB"));
    const QBluetoothUuid BUTTON(QString("0000EF82-0000-1000-8000-00805F9B34FB"));
}

// Bookoo
namespace Bookoo {
    const QBluetoothUuid SERVICE(QString("00000FFE-0000-1000-8000-00805F9B34FB"));
    const QBluetoothUuid STATUS(QString("0000FF11-0000-1000-8000-00805F9B34FB"));
    const QBluetoothUuid CMD(QString("0000FF12-0000-1000-8000-00805F9B34FB"));
}

// Eureka Precisa / Solo Barista / SmartChef / Timemore (same UUIDs)
namespace Generic {
    const QBluetoothUuid SERVICE(QString("0000FFF0-0000-1000-8000-00805F9B34FB"));
    const QBluetoothUuid STATUS(QString("0000FFF1-0000-1000-8000-00805F9B34FB"));
    const QBluetoothUuid CMD(QString("0000FFF2-0000-1000-8000-00805F9B34FB"));
}

// DiFluid Microbalance / Microbalance Ti
//
// The Ti speaks the same DF-DF command protocol but advertises a *different*
// 16-bit service: the original Microbalance uses 0x00EE, the Ti 0x00DD
// (protocolMicrobalance.md, updated Dec 2024). Matching only 0x00EE meant a Ti
// reached service discovery, matched nothing, warned, and never became a
// connected scale. Both are accepted; DifluidScale remembers which one the
// connected device actually exposed.
//
// On the characteristic, the cited document is self-inconsistent: it lists AA01
// for both models, then directs FF01 as the communication channel for the
// original Microbalance and AA01 for the Ti. AA01 is what Beanconqueror uses for
// both (difluidMicrobalance.ts, difluidMicrobalanceTi.ts) and what has shipped
// here, so AA01 it is — but that is a working-practice claim, not a spec one.
namespace DiFluid {
    const QBluetoothUuid SERVICE(QString("000000EE-0000-1000-8000-00805F9B34FB"));
    const QBluetoothUuid SERVICE_TI(QString("000000DD-0000-1000-8000-00805F9B34FB"));
    const QBluetoothUuid CHARACTERISTIC(QString("0000AA01-0000-1000-8000-00805F9B34FB"));
}

// Hiroia Jimmy
namespace HiroiaJimmy {
    const QBluetoothUuid SERVICE(QString("06C31822-8682-4744-9211-FEBC93E3BECE"));
    const QBluetoothUuid CMD(QString("06C31823-8682-4744-9211-FEBC93E3BECE"));
    const QBluetoothUuid STATUS(QString("06C31824-8682-4744-9211-FEBC93E3BECE"));
}

// Atomheart Eclair
namespace AtomheartEclair {
    const QBluetoothUuid SERVICE(QString("B905EAEA-6C7E-4F73-B43D-2CDFCAB29570"));
    const QBluetoothUuid STATUS(QString("B905EAEB-6C7E-4F73-B43D-2CDFCAB29570"));
    const QBluetoothUuid CMD(QString("B905EAEC-6C7E-4F73-B43D-2CDFCAB29570"));
}

// Varia Aku
namespace VariaAku {
    const QBluetoothUuid SERVICE(QString("0000FFF0-0000-1000-8000-00805F9B34FB"));
    const QBluetoothUuid STATUS(QString("0000FFF1-0000-1000-8000-00805F9B34FB"));
    const QBluetoothUuid CMD(QString("0000FFF2-0000-1000-8000-00805F9B34FB"));
}

} // namespace Scale

// Refractometer UUIDs
namespace Refractometer {

namespace DiFluidR2 {
    const QBluetoothUuid SERVICE(QString("000000FF-0000-1000-8000-00805F9B34FB"));
    const QBluetoothUuid CHARACTERISTIC(QString("0000AA01-0000-1000-8000-00805F9B34FB"));
}

// DiFluid R1 — protocol reverse-engineered from the official DiFluid app v1.2.6.
// 16-bit service 0x1EFF (advertised as 0xE01E in the scan record).
// All 16-bit UUIDs expand to the Bluetooth base form.
namespace DiFluidR1 {
    const QBluetoothUuid SERVICE(QString("00001EFF-0000-1000-8000-00805F9B34FB"));
    const QBluetoothUuid DATA   (QString("00001E01-0000-1000-8000-00805F9B34FB")); // notify: 16-byte AES-ECB ciphertext
    const QBluetoothUuid BATTERY(QString("00001E02-0000-1000-8000-00805F9B34FB")); // notify: battery level
    const QBluetoothUuid SALT   (QString("00001E03-0000-1000-8000-00805F9B34FB")); // read: 12-byte salt + HW info
    const QBluetoothUuid STATUS (QString("00001E06-0000-1000-8000-00805F9B34FB")); // notify: status / ack
    const QBluetoothUuid STATUS2(QString("00001E07-0000-1000-8000-00805F9B34FB")); // notify: status (shared handler)
    const QBluetoothUuid CMD    (QString("00001E08-0000-1000-8000-00805F9B34FB")); // notify + write: commands + ack
}

} // namespace Refractometer
