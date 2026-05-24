#pragma once

#include <QString>

// Scale types supported. Lives in this lightweight, dependency-free header (no
// QtBluetooth, no scale-class includes) so core code (Settings / SettingsCalibration)
// and unit tests can normalize/key on scale type-ids without linking the whole
// ScaleFactory + every concrete scale driver.
enum class ScaleType {
    Unknown,
    // The Half Decent Scale is ONE physical scale reachable over three transports;
    // each is a distinct type-id so per-transport state (e.g. SAW latency) stays isolated.
    DecentScale,      // Bluetooth transport
    DecentScaleWifi,  // WiFi transport
    DecentScaleUsb,   // USB transport
    Acaia,
    AcaiaPyxis,
    Felicita,
    Skale,
    HiroiaJimmy,
    Bookoo,
    SmartChef,
    Difluid,
    EurekaPrecisa,
    SoloBarista,
    AtomheartEclair,
    VariaAku,
    Timemore
};

// Canonical scale type-id / display-name mapping and normalization.
//
// The type-id is the stable key used everywhere a scale is persisted or keyed on:
// the `scale/type` setting, known-scale entries, and SAW per-(profile, scale)
// learning. It matches each driver's ScaleDevice::type(). The display name is a
// human label only — renaming it must never change a key, which is the whole point
// of keeping the two separate. See docs/CLAUDE_MD/SAW_LEARNING.md.
namespace ScaleTypeIds {

// Enum -> canonical id (mirrors ScaleDevice::type(), e.g. DecentScale -> "decent").
// Returns an empty string for ScaleType::Unknown.
QString scaleTypeId(ScaleType type);

// Enum -> human-readable display name (e.g. DecentScale -> "Decent Scale").
QString scaleTypeName(ScaleType type);

// Any legacy display-name OR id string -> canonical id. Idempotent. Unrecognized
// strings (e.g. a future custom value with no ScaleType enum) are returned
// unchanged so no data is ever destroyed.
QString normalizeScaleTypeId(const QString& typeOrName);

}  // namespace ScaleTypeIds
