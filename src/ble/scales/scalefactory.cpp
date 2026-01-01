#include "scalefactory.h"
#include "decentscale.h"
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

    return ScaleType::Unknown;
}

std::unique_ptr<ScaleDevice> ScaleFactory::createScale(const QBluetoothDeviceInfo& device, QObject* parent) {
    ScaleType type = detectScaleType(device);

    switch (type) {
        case ScaleType::DecentScale:
            return std::make_unique<DecentScale>(parent);
        case ScaleType::Acaia:
        case ScaleType::AcaiaPyxis:
            // Unified AcaiaScale auto-detects IPS vs Pyxis protocol
            return std::make_unique<AcaiaScale>(parent);
        case ScaleType::Felicita:
            return std::make_unique<FelicitaScale>(parent);
        case ScaleType::Skale:
            return std::make_unique<SkaleScale>(parent);
        case ScaleType::HiroiaJimmy:
            return std::make_unique<HiroiaScale>(parent);
        case ScaleType::Bookoo:
            return std::make_unique<BookooScale>(parent);
        case ScaleType::SmartChef:
            return std::make_unique<SmartChefScale>(parent);
        case ScaleType::Difluid:
            return std::make_unique<DifluidScale>(parent);
        case ScaleType::EurekaPrecisa:
            return std::make_unique<EurekaPrecisaScale>(parent);
        case ScaleType::SoloBarista:
            return std::make_unique<SoloBarristaScale>(parent);
        case ScaleType::AtomheartEclair:
            return std::make_unique<AtomheartEclairScale>(parent);
        case ScaleType::VariaAku:
            return std::make_unique<VariaAkuScale>(parent);
        default:
            return nullptr;
    }
}

bool ScaleFactory::isKnownScale(const QBluetoothDeviceInfo& device) {
    return detectScaleType(device) != ScaleType::Unknown;
}

std::unique_ptr<ScaleDevice> ScaleFactory::createScale(const QBluetoothDeviceInfo& device, const QString& typeName, QObject* parent) {
    // Map type name to ScaleType enum
    ScaleType type = ScaleType::Unknown;

    QString name = typeName.toLower();
    if (name.contains("decent")) type = ScaleType::DecentScale;
    else if (name.contains("pyxis")) type = ScaleType::AcaiaPyxis;
    else if (name.contains("acaia")) type = ScaleType::Acaia;
    else if (name.contains("felicita")) type = ScaleType::Felicita;
    else if (name.contains("skale")) type = ScaleType::Skale;
    else if (name.contains("hiroia") || name.contains("jimmy")) type = ScaleType::HiroiaJimmy;
    else if (name.contains("bookoo")) type = ScaleType::Bookoo;
    else if (name.contains("smartchef")) type = ScaleType::SmartChef;
    else if (name.contains("difluid")) type = ScaleType::Difluid;
    else if (name.contains("eureka") || name.contains("precisa")) type = ScaleType::EurekaPrecisa;
    else if (name.contains("solo") || name.contains("barista")) type = ScaleType::SoloBarista;
    else if (name.contains("eclair") || name.contains("atomheart")) type = ScaleType::AtomheartEclair;
    else if (name.contains("aku") || name.contains("varia")) type = ScaleType::VariaAku;

    if (type == ScaleType::Unknown) {
        // Fall back to detection from device name
        return createScale(device, parent);
    }

    switch (type) {
        case ScaleType::DecentScale:
            return std::make_unique<DecentScale>(parent);
        case ScaleType::Acaia:
        case ScaleType::AcaiaPyxis:
            // Unified AcaiaScale auto-detects IPS vs Pyxis protocol
            return std::make_unique<AcaiaScale>(parent);
        case ScaleType::Felicita:
            return std::make_unique<FelicitaScale>(parent);
        case ScaleType::Skale:
            return std::make_unique<SkaleScale>(parent);
        case ScaleType::HiroiaJimmy:
            return std::make_unique<HiroiaScale>(parent);
        case ScaleType::Bookoo:
            return std::make_unique<BookooScale>(parent);
        case ScaleType::SmartChef:
            return std::make_unique<SmartChefScale>(parent);
        case ScaleType::Difluid:
            return std::make_unique<DifluidScale>(parent);
        case ScaleType::EurekaPrecisa:
            return std::make_unique<EurekaPrecisaScale>(parent);
        case ScaleType::SoloBarista:
            return std::make_unique<SoloBarristaScale>(parent);
        case ScaleType::AtomheartEclair:
            return std::make_unique<AtomheartEclairScale>(parent);
        case ScaleType::VariaAku:
            return std::make_unique<VariaAkuScale>(parent);
        default:
            return nullptr;
    }
}

QString ScaleFactory::scaleTypeName(ScaleType type) {
    switch (type) {
        case ScaleType::DecentScale: return "Decent Scale";
        case ScaleType::Acaia: return "Acaia";
        case ScaleType::AcaiaPyxis: return "Acaia Pyxis";
        case ScaleType::Felicita: return "Felicita";
        case ScaleType::Skale: return "Skale";
        case ScaleType::HiroiaJimmy: return "Hiroia Jimmy";
        case ScaleType::Bookoo: return "Bookoo";
        case ScaleType::SmartChef: return "SmartChef";
        case ScaleType::Difluid: return "Difluid";
        case ScaleType::EurekaPrecisa: return "Eureka Precisa";
        case ScaleType::SoloBarista: return "Solo Barista";
        case ScaleType::AtomheartEclair: return "Atomheart Eclair";
        case ScaleType::VariaAku: return "Varia Aku";
        default: return "Unknown";
    }
}

// Detection functions based on device name patterns from de1app
bool ScaleFactory::isDecentScale(const QString& name) {
    return name.contains("decent scale");
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
