#pragma once

#include <QBluetoothUuid>
#include <QString>

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
    Puffing         = 20
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
    constexpr uint32_t PHASE1_FLOW_RATE     = 0x803810;  // Heater warmup flow rate in tenths mL/s (de1app default: 20 = 2.0 mL/s)
    constexpr uint32_t PHASE2_FLOW_RATE     = 0x803814;  // Heater test flow rate in tenths mL/s (de1app default: 40 = 4.0 mL/s)
    constexpr uint32_t HOT_WATER_IDLE_TEMP  = 0x803818;  // Heater idle temperature in tenths °C (de1app default: 990 = 99.0°C)
    constexpr uint32_t GHC_INFO             = 0x80381C;
    constexpr uint32_t GHC_MODE             = 0x803820;
    constexpr uint32_t STEAM_FLOW           = 0x803828;
    constexpr uint32_t SERIAL_NUMBER        = 0x803830;
    constexpr uint32_t HEATER_VOLTAGE       = 0x803834;
    constexpr uint32_t ESPRESSO_WARMUP_TIMEOUT = 0x803838;  // Warmup timeout in seconds (de1app default: 10)
    constexpr uint32_t FLOW_CALIBRATION     = 0x80383C;  // Flow calibration multiplier (value = int(1000 * multiplier))
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

// Eureka Precisa / Solo Barista / SmartChef (same UUIDs)
namespace Generic {
    const QBluetoothUuid SERVICE(QString("0000FFF0-0000-1000-8000-00805F9B34FB"));
    const QBluetoothUuid STATUS(QString("0000FFF1-0000-1000-8000-00805F9B34FB"));
    const QBluetoothUuid CMD(QString("0000FFF2-0000-1000-8000-00805F9B34FB"));
}

// DiFluid
namespace DiFluid {
    const QBluetoothUuid SERVICE(QString("000000EE-0000-1000-8000-00805F9B34FB"));
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
