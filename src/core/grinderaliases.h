#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace GrinderAliases {

struct GrinderEntry {
    QString brand;
    QString model;
    QStringList aliases;
    QStringList stockBurrs;  // Multiple options (e.g. Duo ships with espresso + filter burrs)
    bool burrSwappable;
    int burrSizeMm;
    QString mountPattern;    // Compatible mount group (e.g. "mazzer64", "mazzer83", "ek98")
};

struct LookupResult {
    QString brand;
    QString model;
    QString stockBurrs;  // Primary (first) stock burr set
    bool burrSwappable = false;
    bool found = false;
};

inline QVector<GrinderEntry> allGrinders()
{
    static const QVector<GrinderEntry> entries = {
        // --- Niche ---
        {"Niche", "Zero", {"niche", "niche zero", "nichezero", "nz"}, {"63mm Mazzer Kony conical"}, false, 0, ""},
        {"Niche", "Duo", {"niche duo", "duo"}, {"83mm DLC espresso flat", "83mm filter flat"}, false, 0, ""},
        // --- Turin (64mm/83mm use Mazzer patterns) ---
        {"Turin", "DF64 Gen 2", {"df64", "df64 gen 2", "df64 gen2", "df64 ii", "df-64", "g-iota", "solo df64"}, {"64mm flat steel", "64mm DLC flat"}, true, 64, "mazzer64"},
        {"Turin", "DF64V Gen 2", {"df64v", "df64v gen 2", "df64v gen2", "df64 v"}, {"64mm DLC flat"}, true, 64, "mazzer64"},
        {"Turin", "CF64V", {"cf64v", "cf64"}, {"64mm DLC flat"}, true, 64, "mazzer64"},
        {"Turin", "DF83V", {"df83v", "df83 v"}, {"83mm flat steel"}, true, 83, "mazzer83"},
        {"Turin", "DF83", {"df83", "df83 gen 1"}, {"83mm flat steel"}, true, 83, "mazzer83"},
        {"Turin", "DF54", {"df54"}, {"54mm flat steel"}, true, 54, ""},
        // --- Weber Workshops (proprietary magnetic mount) ---
        {"Weber Workshops", "EG-1", {"eg-1", "eg1", "weber eg-1", "weber eg1", "weber workshops eg-1"}, {"80mm CORE (DB-1) flat", "80mm ULTRA (DB-2) flat", "80mm BASE (DB-3) flat"}, true, 80, ""},
        {"Weber Workshops", "HG-2", {"hg-2", "hg2", "weber hg-2", "weber hg2"}, {"83mm Mazzer conical"}, false, 0, ""},
        {"Weber Workshops", "KEY", {"weber key", "key grinder"}, {"83mm conical"}, false, 0, ""},
        // --- Eureka ---
        {"Eureka", "Mignon Specialita", {"specialita", "eureka specialita", "mignon specialita", "eureka mignon specialita"}, {"55mm flat steel"}, false, 0, ""},
        {"Eureka", "Mignon Notte", {"notte", "eureka notte", "mignon notte", "eureka mignon notte"}, {"50mm flat steel"}, false, 0, ""},
        {"Eureka", "Mignon Manuale", {"manuale", "eureka manuale", "mignon manuale"}, {"50mm flat steel"}, false, 0, ""},
        {"Eureka", "Mignon XL", {"mignon xl", "eureka xl", "eureka mignon xl"}, {"65mm flat steel"}, false, 0, ""},
        {"Eureka", "Mignon Turbo", {"turbo", "eureka turbo", "mignon turbo", "eureka mignon turbo"}, {"65mm flat steel"}, false, 0, ""},
        {"Eureka", "Mignon Single Dose", {"mignon single dose", "eureka single dose", "eureka sd", "mignon sd"}, {"65mm Diamond Inside flat"}, false, 0, ""},
        {"Eureka", "Mignon Libra", {"libra", "eureka libra", "mignon libra"}, {"55mm flat steel"}, false, 0, ""},
        {"Eureka", "Mignon Perfetto", {"perfetto", "eureka perfetto", "mignon perfetto"}, {"50mm flat steel"}, false, 0, ""},
        {"Eureka", "Mignon Crono", {"crono", "eureka crono", "mignon crono"}, {"50mm flat steel"}, false, 0, ""},
        {"Eureka", "Atom 65", {"atom 65", "eureka atom 65", "atom65"}, {"65mm flat steel"}, false, 0, ""},
        {"Eureka", "Atom 75", {"atom 75", "eureka atom 75", "atom75"}, {"75mm flat steel"}, false, 0, ""},
        {"Eureka", "Helios 80", {"helios 80", "eureka helios 80", "helios80"}, {"80mm flat steel"}, false, 0, ""},
        // --- Option-O (P64 accepts Mazzer 64mm pattern; P100/01 accept EK43 98mm) ---
        {"Option-O", "Lagom P64", {"p64", "lagom p64", "option-o p64"}, {"64mm Mizen 64OM flat", "64mm Mizen 64ES flat", "64mm SSP High Uniformity flat", "64mm SSP Unimodal Espresso flat"}, true, 64, "mazzer64"},
        {"Option-O", "Lagom P80", {"p80", "lagom p80", "option-o p80"}, {"80mm Mizen 80OM flat"}, true, 80, ""},
        {"Option-O", "Lagom P100", {"p100", "lagom p100", "option-o p100"}, {"98mm Mizen 98OM flat", "98mm SSP High Uniformity flat"}, true, 98, "ek98"},
        {"Option-O", "Lagom 01", {"lagom 01", "option-o 01", "lagom01"}, {"102mm Mizen 102OM blind flat", "98mm Mizen 98OM flat", "102mm SSP 102HU blind flat"}, true, 98, "ek98"},
        {"Option-O", "Lagom Mini 2", {"lagom mini", "lagom mini 2", "option-o mini", "mini 2"}, {"48mm Mizen 48MS flat"}, false, 0, ""},
        {"Option-O", "Lagom Casa", {"lagom casa", "option-o casa", "casa"}, {"65mm Mizen 65CL conical"}, false, 0, ""},
        // --- Zerno (magnetic blind mount; accepts Mazzer screw-mount too, but stock burrs are blind-only) ---
        {"Zerno", "Z1", {"zerno", "zerno z1", "z1"}, {"64mm SSP High Uniformity", "64mm SSP Multipurpose V2", "64mm SSP Unimodal Brew", "64mm SSP Cast V2 Lab Sweet", "64mm SSP Cast V3", "64mm Mazzer 233M"}, true, 64, ""},
        {"Zerno", "Z2", {"zerno z2", "z2"}, {"80mm Modern Espresso", "80mm Ultra Low Fines", "80mm Red Speed"}, true, 80, ""},
        // --- Kafatek (proprietary Shuriken burrs) ---
        {"Kafatek", "Monolith Flat", {"monolith flat", "kafatek flat", "monolith flat sdrm", "mc flat"}, {"75mm Shuriken LM flat", "75mm Shuriken SW flat", "75mm Shuriken MD flat", "75mm Shuriken CR flat"}, false, 0, ""},
        {"Kafatek", "Monolith Flat MAX", {"monolith flat max", "flat max", "kafatek flat max", "monolith max"}, {"98mm Shuriken LM flat", "98mm Shuriken SW flat", "98mm Shuriken MD flat", "98mm Shuriken CR flat"}, false, 0, ""},
        {"Kafatek", "Monolith Conical", {"monolith conical", "kafatek conical", "mc5", "mc6", "monolith mc"}, {"71mm Shurikone conical"}, false, 0, ""},
        // --- Levercraft (accepts EK43 98mm burrs) ---
        {"Levercraft", "Ultra", {"levercraft", "levercraft ultra", "ultra grinder"}, {"98mm SSP High Uniformity flat", "98mm SSP Low Uniformity flat"}, true, 98, "ek98"},
        {"Versalab", "M4", {"versalab", "versalab m4", "versalab m3"}, {"68mm SSP hybrid"}, false, 0, ""},
        {"Bentwood", "Vertical 63", {"bentwood", "bentwood vertical", "bentwood v63", "vertical 63"}, {"63mm flat steel"}, false, 0, ""},
        // --- Acaia / Mazzer (Mazzer 64mm pattern) ---
        {"Acaia", "Orbit", {"orbit", "acaia orbit"}, {"64mm Mazzer 33M flat", "64mm SSP Multipurpose flat", "64mm SSP Lab Sweet flat"}, true, 64, "mazzer64"},
        {"Mazzer", "Philos", {"philos", "mazzer philos"}, {"64mm I200D flat", "64mm I189D flat"}, true, 64, "mazzer64"},
        {"Mazzer", "Mini", {"mazzer mini", "mini electronic", "mazzer mini e"}, {"64mm flat steel"}, false, 0, "mazzer64"},
        {"Mazzer", "Super Jolly", {"super jolly", "mazzer super jolly", "sj", "super jolly v pro"}, {"64mm flat steel (233M)"}, true, 64, "mazzer64"},
        {"Mazzer", "Major", {"mazzer major", "major v"}, {"83mm flat steel"}, true, 83, "mazzer83"},
        {"Mazzer", "Kony", {"mazzer kony", "kony"}, {"63mm conical steel"}, false, 0, ""},
        {"Mazzer", "Robur S", {"mazzer robur", "robur", "robur s"}, {"71mm conical steel"}, false, 0, ""},
        {"Mazzer", "Kold S", {"mazzer kold", "kold", "kold s"}, {"71mm conical steel"}, false, 0, ""},
        {"La Marzocco", "Lux D", {"lux d", "la marzocco lux", "lux grinder"}, {"61mm flat steel"}, false, 0, ""},
        // --- Mahlkonig (EK43 = ek98 pattern; X64/E64 = mazzer64) ---
        {"Mahlkonig", "EK43", {"ek43", "ek-43", "mahlkonig ek43"}, {"98mm flat steel"}, true, 98, "ek98"},
        {"Mahlkonig", "E65W", {"e65w", "mahlkonig e65", "e65"}, {"65mm flat steel"}, false, 0, ""},
        {"Mahlkonig", "E80W", {"e80w", "e80s", "mahlkonig e80"}, {"80mm flat steel"}, false, 0, ""},
        {"Mahlkonig", "X54", {"x54", "mahlkonig x54"}, {"54mm flat steel"}, false, 0, ""},
        {"Mahlkonig", "X64 SD", {"x64", "mahlkonig x64", "x64 sd"}, {"64mm flat steel"}, false, 0, "mazzer64"},
        {"Mahlkonig", "E64 WS", {"e64", "mahlkonig e64", "e64 ws"}, {"64mm flat steel"}, false, 0, "mazzer64"},
        // --- Fiorenzato (Mazzer 64mm pattern) ---
        {"Fiorenzato", "AllGround", {"allground", "fiorenzato allground", "fiorenzato"}, {"64mm Dark-T titanium coated flat"}, false, 0, "mazzer64"},
        {"Varia", "VS3", {"vs3", "varia vs3"}, {"48mm conical stainless"}, false, 0, ""},
        {"Varia", "VS6", {"vs6", "varia vs6"}, {"58mm Supernova flat"}, true, 58, ""},
        {"WPM", "ZP-1", {"zp-1", "zp1", "wpm zp-1"}, {"64mm flat steel"}, false, 0, "mazzer64"},
        {"Baratza", "Sette 270", {"sette 270", "sette", "baratza sette", "sette 270wi"}, {"40mm conical steel"}, false, 0, ""},
        {"Baratza", "Encore ESP", {"encore", "encore esp", "baratza encore"}, {"40mm conical steel"}, false, 0, ""},
        {"Baratza", "Vario+", {"vario", "vario+", "baratza vario", "forte"}, {"54mm flat ceramic", "54mm flat steel"}, false, 0, ""},
        {"Breville", "Smart Grinder Pro", {"smart grinder", "smart grinder pro", "breville smart grinder", "sage smart grinder", "bcg820"}, {"40mm conical steel"}, false, 0, ""},
        {"Fellow", "Opus", {"opus", "fellow opus"}, {"40mm conical steel"}, false, 0, ""},
        // --- Fellow Ode (proprietary 64mm mount, NOT Mazzer compatible) ---
        {"Fellow", "Ode Gen 2", {"ode", "fellow ode", "ode gen 2"}, {"64mm Gen 2 Brew flat", "64mm SSP MP Red Speed flat"}, true, 64, ""},
        {"Rancilio", "Rocky", {"rocky", "rancilio rocky"}, {"50mm flat steel"}, false, 0, ""},
        {"Wilfa", "Uniform", {"uniform", "wilfa uniform", "wilfa svart"}, {"58mm flat steel"}, false, 0, ""},
        {"Compak", "E5", {"compak e5"}, {"58mm flat steel"}, false, 0, ""},
        {"Compak", "E8", {"compak e8"}, {"83mm flat steel"}, true, 83, "mazzer83"},
        {"Compak", "E10", {"compak e10"}, {"68mm conical steel"}, false, 0, ""},
        // --- Ceado (E37S/SD = mazzer83, E37J = mazzer64) ---
        {"Ceado", "E37S", {"e37s", "ceado e37s", "ceado e37"}, {"83mm titanium flat"}, false, 0, "mazzer83"},
        {"Ceado", "E37SD", {"e37sd", "ceado e37sd", "ceado sd"}, {"83mm OpalGlide flat"}, false, 0, "mazzer83"},
        {"Ceado", "E37J", {"e37j", "ceado e37j"}, {"64mm flat steel"}, false, 0, "mazzer64"},
        {"Anfim", "SP II", {"anfim sp", "anfim spii", "sp ii", "sp2"}, {"75mm titanium coated flat"}, false, 0, ""},
        // --- Timemore (Sculptor 064 = mazzer64) ---
        {"Timemore", "Sculptor 064S", {"sculptor 064", "timemore sculptor", "sculptor 064s"}, {"64mm flat steel"}, false, 0, "mazzer64"},
        {"Timemore", "Sculptor 078S", {"sculptor 078", "sculptor 078s"}, {"78mm flat steel"}, false, 0, ""},
        {"Craig Lyn", "HG-1 Prime", {"hg-1", "hg1", "craig lyn", "hg-1 prime"}, {"83mm Mazzer conical"}, false, 0, ""},
        {"Comandante", "C40 MK4", {"comandante", "c40", "comandante c40", "c40 mk4", "c40 mk3"}, {"39mm Nitro Blade conical"}, false, 0, ""},
        {"1Zpresso", "JX-Pro", {"jx-pro", "jx pro", "1zpresso jx", "jx"}, {"48mm conical steel"}, false, 0, ""},
        {"1Zpresso", "J-Max", {"j-max", "jmax", "1zpresso j-max", "1zpresso jmax"}, {"48mm conical steel"}, false, 0, ""},
        {"1Zpresso", "K-Max", {"k-max", "kmax", "1zpresso k-max", "1zpresso kmax"}, {"48mm conical steel"}, false, 0, ""},
        {"1Zpresso", "K-Plus", {"k-plus", "kplus", "1zpresso k-plus", "k plus"}, {"48mm conical steel"}, false, 0, ""},
        {"1Zpresso", "Q2", {"q2", "1zpresso q2"}, {"38mm conical steel"}, false, 0, ""},
        {"KINGrinder", "K6", {"k6", "kingrinder k6", "king grinder k6"}, {"48mm heptagonal conical stainless"}, false, 0, ""},
        {"KINGrinder", "K4", {"k4", "kingrinder k4", "king grinder k4"}, {"48mm titanium coated conical"}, false, 0, ""},
        {"Timemore", "C3 ESP PRO", {"c3 esp", "c3 esp pro", "timemore c3", "chestnut c3"}, {"38mm S2C conical stainless"}, false, 0, ""},
        {"Timemore", "Chestnut X", {"chestnut x", "timemore x", "timemore chestnut x"}, {"42mm S2C conical stainless"}, false, 0, ""},
        {"Normcore", "V2", {"normcore", "normcore v2"}, {"38mm conical stainless"}, false, 0, ""},
        {"Etzinger", "etz-I", {"etzinger", "etz-i", "etzi"}, {"32mm conical stainless"}, false, 0, ""},
        {"ROK", "GrinderGC", {"rok", "rok grinder", "grindergc"}, {"48mm conical steel"}, false, 0, ""},
        {"Hario", "Skerton Pro", {"skerton", "hario skerton", "skerton pro"}, {"38mm ceramic conical"}, false, 0, ""},
        {"Porlex", "Mini II", {"porlex", "porlex mini"}, {"30mm ceramic conical"}, false, 0, ""},
        {"De'Longhi", "Dedica KG521", {"delonghi", "dedica", "kg521", "delonghi dedica"}, {"35mm conical steel"}, false, 0, ""},
        {"Macap", "M2", {"macap m2"}, {"50mm flat steel"}, false, 0, ""},
        {"Macap", "M4", {"macap m4"}, {"58mm flat steel"}, false, 0, ""},
        // --- Titus (accepts EK43 98mm burrs) ---
        {"Titus Grinding", "Nautilus", {"titus", "titus nautilus", "nautilus"}, {"98mm SSP High Uniformity flat", "98mm SSP Low Uniformity flat"}, true, 98, "ek98"},
    };
    return entries;
}

// Case-insensitive lookup: tries each alias, longest-first for ambiguity resolution.
// Returns found=true if the raw string matches any alias.
inline LookupResult lookup(const QString& rawGrinderString)
{
    LookupResult result;
    if (rawGrinderString.trimmed().isEmpty())
        return result;

    const QString lower = rawGrinderString.trimmed().toLower();
    const auto& grinders = allGrinders();

    // First pass: exact alias match (case-insensitive)
    int bestLen = 0;
    const GrinderEntry* bestMatch = nullptr;

    for (const auto& entry : grinders) {
        for (const auto& alias : entry.aliases) {
            if (lower == alias && alias.length() > bestLen) {
                bestLen = alias.length();
                bestMatch = &entry;
            }
        }
    }

    // Second pass: check if the raw string contains an alias (for strings like "Niche Zero (modded)")
    if (!bestMatch) {
        for (const auto& entry : grinders) {
            for (const auto& alias : entry.aliases) {
                if (lower.contains(alias) && alias.length() > bestLen) {
                    bestLen = alias.length();
                    bestMatch = &entry;
                }
            }
        }
    }

    if (bestMatch) {
        result.brand = bestMatch->brand;
        result.model = bestMatch->model;
        result.stockBurrs = bestMatch->stockBurrs.isEmpty() ? QString() : bestMatch->stockBurrs.first();
        result.burrSwappable = bestMatch->burrSwappable;
        result.found = true;
    }

    return result;
}

// Get suggested burrs for a given brand+model.
// Returns: stock burrs for this model first, then compatible burrs from other
// grinders sharing the same mount pattern (e.g. "mazzer64"). Only grinders
// with a non-empty mountPattern get cross-compatible suggestions.
inline QStringList suggestedBurrs(const QString& brand, const QString& model)
{
    QStringList burrs;
    const auto& grinders = allGrinders();
    QString mount;

    // First: stock burrs for the exact model
    for (const auto& entry : grinders) {
        if (entry.brand.compare(brand, Qt::CaseInsensitive) == 0 &&
            entry.model.compare(model, Qt::CaseInsensitive) == 0) {
            burrs << entry.stockBurrs;
            mount = entry.mountPattern;
            break;
        }
    }

    // Second: compatible burrs from other grinders with the same mount pattern
    if (!mount.isEmpty()) {
        for (const auto& entry : grinders) {
            if (entry.mountPattern != mount)
                continue;
            if (entry.brand.compare(brand, Qt::CaseInsensitive) == 0 &&
                entry.model.compare(model, Qt::CaseInsensitive) == 0)
                continue;
            for (const auto& b : entry.stockBurrs) {
                if (!burrs.contains(b))
                    burrs << b;
            }
        }
    }

    return burrs;
}

// Check if a grinder model supports swappable burrs
inline bool isBurrSwappable(const QString& brand, const QString& model)
{
    const auto& grinders = allGrinders();
    for (const auto& entry : grinders) {
        if (entry.brand.compare(brand, Qt::CaseInsensitive) == 0 &&
            entry.model.compare(model, Qt::CaseInsensitive) == 0) {
            return entry.burrSwappable;
        }
    }
    return false;
}

// Get all known brands (unique, sorted)
inline QStringList allBrands()
{
    QStringList brands;
    const auto& grinders = allGrinders();
    for (const auto& entry : grinders) {
        if (!brands.contains(entry.brand))
            brands << entry.brand;
    }
    brands.sort(Qt::CaseInsensitive);
    return brands;
}

// Get all known models for a brand
inline QStringList modelsForBrand(const QString& brand)
{
    QStringList models;
    const auto& grinders = allGrinders();
    for (const auto& entry : grinders) {
        if (entry.brand.compare(brand, Qt::CaseInsensitive) == 0)
            models << entry.model;
    }
    return models;
}

} // namespace GrinderAliases
