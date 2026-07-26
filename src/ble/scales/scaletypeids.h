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
// learning. It matches each scale's ScaleDevice::type() — BLE/WiFi drivers in
// src/ble/scales/, and the USB scale via UsbDecentScale in src/usb/. The display
// name is a human label only — renaming it must never change a key, which is the
// whole point of keeping the two separate. See docs/CLAUDE_MD/SAW_LEARNING.md.
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

// True only for a string that maps to a real ScaleType — i.e. a physical scale
// whose per-transport state is worth keying on.
//
// Deliberately FALSE for the virtual scales: FlowScale reports "flow" (a raw
// string, not a scaleTypeId), and ScaleDevice's base type() returns "". Callers
// that key persistent per-scale state — SAW learning above all — use this to
// decide whether the serving scale is one they should key on at all, rather than
// inventing a pool for a scale that has no transport latency to learn.
// normalizeScaleTypeId() deliberately passes unknown strings through unchanged,
// so it cannot answer this question on its own.
bool isCanonicalScaleTypeId(const QString& typeOrName);

// Canonical id -> display name, without the caller needing the enum. Returns an
// empty string for anything isCanonicalScaleTypeId() rejects, so a caller cannot
// accidentally render "Unknown" for a virtual scale.
QString scaleTypeNameForId(const QString& typeId);

}  // namespace ScaleTypeIds
