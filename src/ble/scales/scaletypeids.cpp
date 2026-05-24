#include "scaletypeids.h"

namespace ScaleTypeIds {

QString scaleTypeId(ScaleType type) {
    switch (type) {
        case ScaleType::DecentScale:     return QStringLiteral("decent");
        case ScaleType::DecentScaleWifi: return QStringLiteral("decent-wifi");
        case ScaleType::DecentScaleUsb:  return QStringLiteral("decent-usb");
        case ScaleType::Acaia:           return QStringLiteral("acaia");
        case ScaleType::AcaiaPyxis:      return QStringLiteral("acaiapyxis");
        case ScaleType::Felicita:        return QStringLiteral("felicita");
        case ScaleType::Skale:           return QStringLiteral("skale");
        case ScaleType::HiroiaJimmy:     return QStringLiteral("hiroiajimmy");
        case ScaleType::Bookoo:          return QStringLiteral("bookoo");
        case ScaleType::SmartChef:       return QStringLiteral("smartchef");
        case ScaleType::Difluid:         return QStringLiteral("difluid");
        case ScaleType::EurekaPrecisa:   return QStringLiteral("eureka_precisa");
        case ScaleType::SoloBarista:     return QStringLiteral("solo_barista");
        case ScaleType::AtomheartEclair: return QStringLiteral("atomheart_eclair");
        case ScaleType::VariaAku:        return QStringLiteral("varia_aku");
        case ScaleType::Timemore:        return QStringLiteral("timemore");
        case ScaleType::Unknown:         return QString();
    }
    return QString();
}

QString scaleTypeName(ScaleType type) {
    switch (type) {
        case ScaleType::DecentScale:     return QStringLiteral("Decent Scale");
        case ScaleType::DecentScaleWifi: return QStringLiteral("Half Decent Scale (WiFi)");
        case ScaleType::DecentScaleUsb:  return QStringLiteral("Half Decent Scale (USB)");
        case ScaleType::Acaia:           return QStringLiteral("Acaia");
        case ScaleType::AcaiaPyxis:      return QStringLiteral("Acaia Pyxis");
        case ScaleType::Felicita:        return QStringLiteral("Felicita");
        case ScaleType::Skale:           return QStringLiteral("Skale");
        case ScaleType::HiroiaJimmy:     return QStringLiteral("Hiroia Jimmy");
        case ScaleType::Bookoo:          return QStringLiteral("Bookoo");
        case ScaleType::SmartChef:       return QStringLiteral("SmartChef");
        case ScaleType::Difluid:         return QStringLiteral("Difluid");
        case ScaleType::EurekaPrecisa:   return QStringLiteral("Eureka Precisa");
        case ScaleType::SoloBarista:     return QStringLiteral("Solo Barista");
        case ScaleType::AtomheartEclair: return QStringLiteral("Atomheart Eclair");
        case ScaleType::VariaAku:        return QStringLiteral("Varia Aku");
        case ScaleType::Timemore:        return QStringLiteral("Timemore");
        case ScaleType::Unknown:         return QStringLiteral("Unknown");
    }
    return QStringLiteral("Unknown");
}

QString normalizeScaleTypeId(const QString& typeOrName) {
    if (typeOrName.isEmpty()) return typeOrName;

    // All real (non-Unknown) scale types, for inverse lookup.
    static const ScaleType kAll[] = {
        ScaleType::DecentScale, ScaleType::DecentScaleWifi, ScaleType::DecentScaleUsb,
        ScaleType::Acaia, ScaleType::AcaiaPyxis, ScaleType::Felicita, ScaleType::Skale,
        ScaleType::HiroiaJimmy, ScaleType::Bookoo, ScaleType::SmartChef,
        ScaleType::Difluid, ScaleType::EurekaPrecisa, ScaleType::SoloBarista,
        ScaleType::AtomheartEclair, ScaleType::VariaAku, ScaleType::Timemore,
    };

    for (ScaleType t : kAll) {
        if (typeOrName == scaleTypeId(t)) return typeOrName;        // already a canonical id
        if (typeOrName == scaleTypeName(t)) return scaleTypeId(t);  // legacy display name -> id
    }

    // Unrecognized (e.g. a future custom value) — return unchanged so nothing is destroyed.
    return typeOrName;
}

}  // namespace ScaleTypeIds
