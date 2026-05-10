#pragma once

#include <QObject>
#include <QBluetoothDeviceInfo>
#include <memory>

class ScaleDevice;
class SettingsConnections;

// Scale types supported
enum class ScaleType {
    Unknown,
    DecentScale,
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

class ScaleFactory {
public:
    // Detect scale type from BLE device info
    static ScaleType detectScaleType(const QBluetoothDeviceInfo& device);

    // Create appropriate scale instance (auto-detect type from device name).
    // If `connections` is provided and the device matches a Decenza Scale
    // with a stored Wi-Fi pairing, the scale is built on a WifiScaleTransport
    // targeting the paired IP. Otherwise the platform BLE transport is used.
    // Pass `nullptr` to force the BLE path (e.g. on a Wi-Fi-failure retry).
    static std::unique_ptr<ScaleDevice> createScale(const QBluetoothDeviceInfo& device,
                                                    QObject* parent = nullptr,
                                                    SettingsConnections* connections = nullptr);

    // Create scale with explicit type (for direct connect without device name).
    // Same `connections` semantics as the auto-detect overload.
    static std::unique_ptr<ScaleDevice> createScale(const QBluetoothDeviceInfo& device,
                                                    const QString& typeName,
                                                    QObject* parent = nullptr,
                                                    SettingsConnections* connections = nullptr);

    // Check if a device is a known scale
    static bool isKnownScale(const QBluetoothDeviceInfo& device);

    // Get human-readable name for scale type
    static QString scaleTypeName(ScaleType type);

    // Resolve any type string (type() lowercase or scaleTypeName() display name) to ScaleType enum
    static ScaleType resolveScaleType(const QString& name);

private:
    // Device name patterns for detection
    static bool isDecentScale(const QString& name);
    static bool isAcaiaScale(const QString& name);
    static bool isAcaiaPyxis(const QString& name);
    static bool isFelicitaScale(const QString& name);
    static bool isSkaleScale(const QString& name);
    static bool isHiroiaJimmy(const QString& name);
    static bool isBookooScale(const QString& name);
    static bool isSmartChefScale(const QString& name);
    static bool isDifluidScale(const QString& name);
    static bool isEurekaPrecisa(const QString& name);
    static bool isSoloBarista(const QString& name);
    static bool isAtomheartEclair(const QString& name);
    static bool isVariaAku(const QString& name);
    static bool isTimemoreScale(const QString& name);
};
