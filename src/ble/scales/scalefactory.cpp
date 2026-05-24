#include "scalefactory.h"
#include "decentscale.h"
#include "decentscalewifi.h"
#include "acaiascale.h"
#include "felicitascale.h"
#include "skalescale.h"
#include "hiroiascale.h"
#include "bookooscale.h"
#include "smartchefscale.h"
#include "difluidscale.h"
#include "eurekaprecisascale.h"
#include "solobaristascale.h"
#include "atomhearteclairscale.h"
#include "variaakuscale.h"
#include "timemorescale.h"

// Transport implementations
#include "../transport/qtscalebletransport.h"
#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
#include "../transport/corebluetooth/corebluetoothscalebletransport.h"
#endif

namespace {
    ScaleBleTransport* createTransportForPlatform() {
#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
        // Use native CoreBluetooth on iOS/macOS - Qt BLE has issues with CCCD discovery
        return new CoreBluetoothScaleBleTransport();
#else
        // Qt 6.10+ BLE works reliably on Android and Desktop
        return new QtScaleBleTransport();
#endif
    }

    // Single scale-agnostic chokepoint: create the transport, the driver, and
    // register the transport on the ScaleDevice base so connection-priority
    // detection (wired in main.cpp) can reach it without per-driver code.
    template <typename ScaleT>
    std::unique_ptr<ScaleDevice> makeScale(QObject* parent) {
        ScaleBleTransport* transport = createTransportForPlatform();
        std::unique_ptr<ScaleT> scale(new ScaleT(transport, parent));
        scale->setBleTransport(transport);
        return scale;
    }
}

ScaleType ScaleFactory::detectScaleType(const QBluetoothDeviceInfo& device) {
    QString name = device.name().toLower();

    // Check each scale type by name pattern
    if (isDecentScale(name)) return ScaleType::DecentScale;
    // All Acaia scales (Lunar, Pearl, Pyxis) now use unified AcaiaScale with auto-detection
    if (isAcaiaPyxis(name) || isAcaiaScale(name)) return ScaleType::Acaia;
    if (isFelicitaScale(name)) return ScaleType::Felicita;
    if (isSkaleScale(name)) return ScaleType::Skale;
    if (isHiroiaJimmy(name)) return ScaleType::HiroiaJimmy;
    if (isBookooScale(name)) return ScaleType::Bookoo;
    if (isSmartChefScale(name)) return ScaleType::SmartChef;
    if (isDifluidScale(name)) return ScaleType::Difluid;
    if (isEurekaPrecisa(name)) return ScaleType::EurekaPrecisa;
    if (isSoloBarista(name)) return ScaleType::SoloBarista;
    if (isAtomheartEclair(name)) return ScaleType::AtomheartEclair;
    if (isVariaAku(name)) return ScaleType::VariaAku;
    if (isTimemoreScale(name)) return ScaleType::Timemore;

    return ScaleType::Unknown;
}

std::unique_ptr<ScaleDevice> ScaleFactory::createScale(const QBluetoothDeviceInfo& device, QObject* parent) {
    ScaleType type = detectScaleType(device);

    switch (type) {
        case ScaleType::DecentScale:
            return makeScale<DecentScale>(parent);
        case ScaleType::Acaia:
        case ScaleType::AcaiaPyxis:
            // Unified AcaiaScale auto-detects IPS vs Pyxis protocol
            return makeScale<AcaiaScale>(parent);
        case ScaleType::Felicita:
            return makeScale<FelicitaScale>(parent);
        case ScaleType::Skale:
            return makeScale<SkaleScale>(parent);
        case ScaleType::HiroiaJimmy:
            return makeScale<HiroiaScale>(parent);
        case ScaleType::Bookoo:
            return makeScale<BookooScale>(parent);
        case ScaleType::SmartChef:
            return makeScale<SmartChefScale>(parent);
        case ScaleType::Difluid:
            return makeScale<DifluidScale>(parent);
        case ScaleType::EurekaPrecisa:
            return makeScale<EurekaPrecisaScale>(parent);
        case ScaleType::SoloBarista:
            return makeScale<SoloBaristaScale>(parent);
        case ScaleType::AtomheartEclair:
            return makeScale<AtomheartEclairScale>(parent);
        case ScaleType::VariaAku:
            return makeScale<VariaAkuScale>(parent);
        case ScaleType::Timemore:
            return makeScale<TimemoreScale>(parent);
        default:
            return nullptr;
    }
}

bool ScaleFactory::isKnownScale(const QBluetoothDeviceInfo& device) {
    return detectScaleType(device) != ScaleType::Unknown;
}

ScaleType ScaleFactory::resolveScaleType(const QString& name) {
    // Reuse the same is*() helpers as detectScaleType to stay in sync.
    // Some type() return values don't match the is*() BLE name patterns
    // (e.g., "decent" vs "decent scale", "solo_barista" vs "solo barista"),
    // so we check internal type codes first as exact matches.
    QString lower = name.toLower();
    if (lower == "decent-wifi") return ScaleType::DecentScaleWifi;
    if (lower == "decent-usb") return ScaleType::DecentScaleUsb;
    if (lower == "decent") return ScaleType::DecentScale;
    if (lower == "solo_barista") return ScaleType::SoloBarista;
    // Then fall through to is*() helpers for display names and BLE device names
    if (isDecentScale(lower)) return ScaleType::DecentScale;
    // Consolidate Pyxis into Acaia, matching detectScaleType (unified AcaiaScale)
    if (isAcaiaPyxis(lower) || isAcaiaScale(lower)) return ScaleType::Acaia;
    if (isFelicitaScale(lower)) return ScaleType::Felicita;
    if (isSkaleScale(lower)) return ScaleType::Skale;
    if (isHiroiaJimmy(lower)) return ScaleType::HiroiaJimmy;
    if (isBookooScale(lower)) return ScaleType::Bookoo;
    if (isSmartChefScale(lower)) return ScaleType::SmartChef;
    if (isDifluidScale(lower)) return ScaleType::Difluid;
    if (isEurekaPrecisa(lower)) return ScaleType::EurekaPrecisa;
    if (isSoloBarista(lower)) return ScaleType::SoloBarista;
    if (isAtomheartEclair(lower)) return ScaleType::AtomheartEclair;
    if (isVariaAku(lower)) return ScaleType::VariaAku;
    if (isTimemoreScale(lower)) return ScaleType::Timemore;
    return ScaleType::Unknown;
}

std::unique_ptr<ScaleDevice> ScaleFactory::createScale(const QBluetoothDeviceInfo& device, const QString& typeName, QObject* parent) {
    ScaleType type = resolveScaleType(typeName);

    if (type == ScaleType::Unknown) {
        // Fall back to detection from device name
        return createScale(device, parent);
    }

    switch (type) {
        case ScaleType::DecentScale:
            return makeScale<DecentScale>(parent);
        case ScaleType::DecentScaleWifi:
            // No BLE transport — WiFi driver uses QWebSocket directly.
            return std::unique_ptr<ScaleDevice>(new DecentScaleWifi(parent));
        case ScaleType::Acaia:
        case ScaleType::AcaiaPyxis:
            // Unified AcaiaScale auto-detects IPS vs Pyxis protocol
            return makeScale<AcaiaScale>(parent);
        case ScaleType::Felicita:
            return makeScale<FelicitaScale>(parent);
        case ScaleType::Skale:
            return makeScale<SkaleScale>(parent);
        case ScaleType::HiroiaJimmy:
            return makeScale<HiroiaScale>(parent);
        case ScaleType::Bookoo:
            return makeScale<BookooScale>(parent);
        case ScaleType::SmartChef:
            return makeScale<SmartChefScale>(parent);
        case ScaleType::Difluid:
            return makeScale<DifluidScale>(parent);
        case ScaleType::EurekaPrecisa:
            return makeScale<EurekaPrecisaScale>(parent);
        case ScaleType::SoloBarista:
            return makeScale<SoloBaristaScale>(parent);
        case ScaleType::AtomheartEclair:
            return makeScale<AtomheartEclairScale>(parent);
        case ScaleType::VariaAku:
            return makeScale<VariaAkuScale>(parent);
        case ScaleType::Timemore:
            return makeScale<TimemoreScale>(parent);
        default:
            return nullptr;
    }
}

// Display-name / id mappings live in the dependency-free ScaleTypeIds unit so
// core (Settings) and tests can normalize type-ids without linking ScaleFactory.
// These ScaleFactory methods are thin forwarders preserving the existing API.
QString ScaleFactory::scaleTypeName(ScaleType type) {
    return ScaleTypeIds::scaleTypeName(type);
}

QString ScaleFactory::scaleTypeId(ScaleType type) {
    return ScaleTypeIds::scaleTypeId(type);
}

QString ScaleFactory::normalizeScaleTypeId(const QString& typeOrName) {
    return ScaleTypeIds::normalizeScaleTypeId(typeOrName);
}

// Detection functions based on device name patterns from de1app
bool ScaleFactory::isDecentScale(const QString& name) {
    return name.contains("decent scale") || name.contains("decenza");
}

bool ScaleFactory::isAcaiaScale(const QString& name) {
    // Acaia scales: ACAIA, LUNAR, PEARL, PROCH
    return name.contains("acaia") ||
           name.contains("lunar") ||
           name.contains("pearl") ||
           name.contains("proch");
}

bool ScaleFactory::isAcaiaPyxis(const QString& name) {
    return name.contains("pyxis");
}

bool ScaleFactory::isFelicitaScale(const QString& name) {
    return name.contains("felicita") ||
           name.contains("ecompass");
}

bool ScaleFactory::isSkaleScale(const QString& name) {
    return name.contains("skale");
}

bool ScaleFactory::isHiroiaJimmy(const QString& name) {
    return name.contains("hiroia") ||
           name.contains("jimmy");
}

bool ScaleFactory::isBookooScale(const QString& name) {
    // Match bookoo_sc (Themis scale) but NOT bookoo_em (Espresso Monitor pressure sensor)
    if (name.contains("bookoo_em")) {
        return false;  // Espresso Monitor is a pressure sensor, not a scale
    }
    return name.contains("bookoo") ||
           name.contains("bkscale");
}

bool ScaleFactory::isSmartChefScale(const QString& name) {
    return name.contains("smartchef");
}

bool ScaleFactory::isDifluidScale(const QString& name) {
    // Exclude R2 refractometer — it advertises with "difluid" in its name
    if (name.contains("r2")) return false;
    return name.contains("difluid") ||
           name.contains("microbalance");
}

bool ScaleFactory::isEurekaPrecisa(const QString& name) {
    return name.contains("eureka") ||
           name.contains("precisa") ||
           name.contains("cfs-9002");
}

bool ScaleFactory::isSoloBarista(const QString& name) {
    return name.contains("solo barista") ||
           name.contains("lsj-001");
}

bool ScaleFactory::isAtomheartEclair(const QString& name) {
    return name.contains("eclair") ||
           name.contains("atomheart");
}

bool ScaleFactory::isVariaAku(const QString& name) {
    return name.contains("aku") ||
           name.contains("varia");
}

bool ScaleFactory::isTimemoreScale(const QString& name) {
    return name.contains("timemore");
}
