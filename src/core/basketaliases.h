#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace BasketAliases {

// The cross-sectional shape of the basket's brewing chamber. This is the
// single most useful "what kind of basket is it" axis for an AI advisor,
// because it changes how the puck behaves under flow:
//   Straight : vertical walls, holes carried right out to the edge. The
//              competition/precision geometry (VST, IMS, Weber Unibasket,
//              Pesado HE, Normcore). Full ~58mm active puck face, most even
//              water exit, the reference for flow profiling.
//   Tapered  : the wall narrows toward the bottom (classic stamped "stock"
//              basket). The effective puck face is smaller (~48mm) than the
//              rim. More forgiving but less even than straight walls.
//   Stepped  : the bore necks DOWN partway (Decent "waisted", S-Works /
//              Graph / Normcore step-down). Converts a 58mm group to a
//              smaller, deeper puck (~46–52mm) for more body and channel
//              tolerance at low doses; usually needs a bottomless portafilter.
//   Convex   : the floor bulges up in the centre (IMS Convex, S-Works /
//              Normcore convex), concentrating flow toward the middle and
//              demanding a slightly coarser grind.
// Note: every basket here is single-wall. Pressurized / dual-wall baskets are
// deliberately excluded — they self-regulate back-pressure and defeat the
// DE1's flow/pressure profiling, so they have no place on a Decent machine.
enum class WallProfile { Straight, Tapered, Stepped, Convex };

// How freely the basket passes water at a FIXED grind — the relative flow
// characteristic. This is the cross-basket knowledge an AI advisor wants: it
// can't be read off a single shot (flow on a fixed basket is constant), but it
// lets the advisor reason when a user SWITCHES baskets or wants to recreate a
// shot on different gear. e.g. a Weber Unibasket (Open) flows faster than a
// Decent stock double (Standard) at the same grind, so moving to it means
// grinding a touch finer to hold the same shot time.
//   Restrictive : finer filtration / lower open area, slower flow (IMS
//                 SuperFine 170µm, Normcore 0.18mm 'reduced flow', stock
//                 stamped baskets).
//   Standard    : a typical good precision basket (Decent, VST, IMS, most).
//                 The reference point.
//   Open        : holes carried to the edge / high open area / full-coverage
//                 patterns, noticeably faster (Weber Unibasket, Pesado HE,
//                 Wafo Spirit).
// This is intentionally COARSE — it gives the AI the DIRECTION of a grind
// change across baskets, not a magnitude. Reliable magnitude comes from the
// user's own per-basket shot history (equipment_id linkage + grind learning),
// not from a spec number.
enum class FlowRate { Restrictive, Standard, Open };

struct BasketEntry {
    QString brand;
    QString model;
    QStringList aliases;     // lowercase match strings (brand+size shorthands)
    int diameterMm;          // nominal group size — always 58 for the DE1
    double doseMinG;         // recommended dose range, low end (grams)
    double doseMaxG;         // recommended dose range, high end (grams)
    WallProfile wall = WallProfile::Straight;
    // True for a laser-drilled / tolerance-controlled "precision" basket
    // (VST ±20µm, IMS competition, Weber, Pesado, Normcore, S-Works billet,
    // and the precision-inspected Decent stock). Uniform holes + even open
    // area give repeatable shots and remove a confound from flow telemetry;
    // stock-stamped baskets vary, so "same grind, different time" may be the
    // basket rather than the user.
    bool precision = false;
    FlowRate flow = FlowRate::Standard;  // relative flow at a fixed grind
    int holeCount = 0;            // published hole count; 0 = unpublished
    int holeDiameterMicrons = 0;  // published hole Ø in microns; 0 = unpublished
    QString material = QStringLiteral("stainless steel");
    QString notes;               // one-line AI enrichment: what it's known for
};

struct LookupResult {
    QString brand;
    QString model;
    double doseMinG = 0;
    double doseMaxG = 0;
    bool precision = false;
    FlowRate flow = FlowRate::Standard;
    bool found = false;
};

inline QVector<BasketEntry> allBaskets()
{
    using WP = WallProfile;
    using FR = FlowRate;
    static const QVector<BasketEntry> entries = {
        // --- Decent (58mm single-wall; holes precision-inspected under
        //     microscope by Decent's own QC software, sold as the DE1 stock) ---
        {"Decent", "Ridgeless 15g", {"decent 15g", "decent ridgeless 15g", "de1 15g"}, 58, 14, 16, WP::Straight, true, FR::Standard, 0, 0, QStringLiteral("stainless steel"), QStringLiteral("Stock small double; ridgeless for bottomless portafilters.")},
        {"Decent", "Ridgeless 18g", {"decent 18g", "decent ridgeless 18g", "de1 18g", "decent stock"}, 58, 17, 19, WP::Straight, true, FR::Standard, 0, 0, QStringLiteral("stainless steel"), QStringLiteral("Basket shipped with the DE1 — the default everyday double.")},
        {"Decent", "Ridgeless 20g", {"decent 20g", "decent ridgeless 20g", "de1 20g"}, 58, 19, 21, WP::Straight, true, FR::Standard, 0, 0, QStringLiteral("stainless steel"), QStringLiteral("Larger capacity ridgeless double for higher doses.")},
        {"Decent", "Ridgeless 22g", {"decent 22g", "decent ridgeless 22g", "de1 22g"}, 58, 21, 23, WP::Straight, true, FR::Standard, 0, 0, QStringLiteral("stainless steel"), QStringLiteral("Largest standard ridgeless; high dose / darker roasts.")},
        {"Decent", "Ridged 7g", {"decent 7g", "decent ridged 7g"}, 58, 6, 8, WP::Straight, true, FR::Standard, 0, 0, QStringLiteral("stainless steel"), QStringLiteral("Single-ristretto small basket; ridge grips a standard portafilter.")},
        {"Decent", "Ridged 10g", {"decent 10g", "decent ridged 10g"}, 58, 9, 11, WP::Straight, true, FR::Standard, 0, 0, QStringLiteral("stainless steel"), QStringLiteral("Small low-dose basket for single / lighter shots.")},
        {"Decent", "Slightly Waisted 14g", {"decent slightly waisted", "slightly waisted"}, 58, 12, 15, WP::Stepped, true, FR::Standard, 0, 0, QStringLiteral("stainless steel"), QStringLiteral("Necks the 58mm bed down (~52mm) for added body; best near 14g.")},
        {"Decent", "Very Waisted 14g", {"decent very waisted", "very waisted"}, 58, 12, 16, WP::Stepped, true, FR::Standard, 0, 0, QStringLiteral("stainless steel"), QStringLiteral("More taper than 'slightly' — more body, easier no-channel shots.")},
        {"Decent", "Extremely Waisted 12g", {"decent extremely waisted", "extremely waisted"}, 58, 11, 14, WP::Stepped, true, FR::Standard, 0, 0, QStringLiteral("stainless steel"), QStringLiteral("Most aggressive taper (~50mm bed); deep lever-style puck for body.")},

        // --- Weber Workshops Unibasket (forged 304, 1.2mm blank, straight
        //     walls with laser-ablated holes carried to the side wall — the
        //     reason it flows faster than a tapered stock basket) ---
        {"Weber Workshops", "Unibasket 16g", {"unibasket 16g", "weber 16g", "weber unibasket 16g"}, 58, 15, 17, WP::Straight, true, FR::Open, 0, 0, QStringLiteral("forged 304 stainless, 1.2mm"), QStringLiteral("Holes to the side wall give a full ~58mm active puck vs ~48mm stock — flows faster.")},
        {"Weber Workshops", "Unibasket 20g", {"unibasket 20g", "weber 20g", "weber unibasket 20g"}, 58, 19, 21, WP::Straight, true, FR::Open, 0, 0, QStringLiteral("forged 304 stainless, 1.2mm"), QStringLiteral("20g drop-in Unibasket; same unibody straight-wall, full-active-area design.")},
        {"Weber Workshops", "Unibasket 24g", {"unibasket 24g", "weber 24g", "weber unibasket 24g"}, 58, 23, 25, WP::Straight, true, FR::Open, 0, 0, QStringLiteral("forged 304 stainless, 1.2mm"), QStringLiteral("24g drop-in Unibasket.")},
        {"Weber Workshops", "Unibasket 28g", {"unibasket 28g", "weber 28g", "weber unibasket 28g"}, 58, 27, 29, WP::Straight, true, FR::Open, 0, 0, QStringLiteral("forged 304 stainless, 1.2mm"), QStringLiteral("Largest Unibasket — a 28g 'mega-shot' size.")},

        // --- VST (the WBC reference precision basket; every hole optically
        //     verified to ~±20µm, individual test certificate per basket;
        //     20% thicker steel; sold ridged AND ridgeless. Hole count and
        //     micron Ø intentionally NOT published — the spec is the tolerance) ---
        {"VST", "7g Single", {"vst 7g", "vst 7", "vst single"}, 58, 6, 8, WP::Straight, true, FR::Standard, 0, 0, QStringLiteral("20% thicker stainless"), QStringLiteral("Precision single basket; ±20µm hole tolerance like the rest of the line.")},
        {"VST", "15g Double", {"vst 15g", "vst 15"}, 58, 14, 16, WP::Straight, true, FR::Standard, 0, 0, QStringLiteral("20% thicker stainless"), QStringLiteral("Precision EU double; ridged or ridgeless.")},
        {"VST", "18g Double", {"vst 18g", "vst 18", "vst", "vst ridgeless 18g"}, 58, 17, 19, WP::Straight, true, FR::Standard, 0, 0, QStringLiteral("20% thicker stainless"), QStringLiteral("Most popular precision double and a WBC reference basket.")},
        {"VST", "20g Competition", {"vst 20g", "vst 20"}, 58, 19, 21, WP::Straight, true, FR::Standard, 0, 0, QStringLiteral("20% thicker stainless"), QStringLiteral("20g competition double; ridged or ridgeless.")},
        {"VST", "22g Triple", {"vst 22g", "vst 22"}, 58, 21, 23, WP::Straight, true, FR::Standard, 0, 0, QStringLiteral("20% thicker stainless"), QStringLiteral("Deeper triple-style precision basket for larger doses.")},
        {"VST", "25g Triple+", {"vst 25g", "vst 25"}, 58, 24, 26, WP::Straight, true, FR::Standard, 0, 0, QStringLiteral("20% thicker stainless"), QStringLiteral("Largest VST size.")},

        // --- IMS (competition precision; 0.30mm holes standard. "M" flat
        //     pattern, "TC" convex base, "E" ridgeless, "SF" SuperFine
        //     membrane, "NT" Nanoquartz non-stick coating) ---
        {"IMS", "Competition B70 2T H24.5 (12-18g)", {"ims 24.5", "ims h24.5", "ims competition 24.5"}, 58, 12, 18, WP::Straight, true, FR::Standard, 0, 300, QStringLiteral("AISI 304, electropolished"), QStringLiteral("Flexible low-dose competition flat; 0.30mm holes.")},
        {"IMS", "Competition B70 2T H26.5 (18-21g)", {"ims 26.5", "ims h26.5", "ims competition", "ims 18g"}, 58, 18, 21, WP::Straight, true, FR::Standard, 641, 300, QStringLiteral("AISI 304, electropolished"), QStringLiteral("Reference IMS competition double — 641 holes at 0.30mm.")},
        {"IMS", "Competition Convex B70 2TC H28.5 (19-22g)", {"ims convex", "ims 2tc", "ims h28.5"}, 58, 19, 22, WP::Convex, true, FR::Standard, 715, 300, QStringLiteral("AISI 304, electropolished"), QStringLiteral("Convex base concentrates flow to the centre; 715 holes, ridgeless.")},
        {"IMS", "SuperFine B70 2T H24 (14-16g)", {"ims superfine", "ims sf", "eb lab superfine"}, 58, 14, 16, WP::Straight, true, FR::Restrictive, 565, 170, QStringLiteral("stainless + 170µm membrane"), QStringLiteral("170µm membrane over 565 holes cuts fines passthrough — finer filtration, slower flow.")},
        {"IMS", "Nanoquartz B70 2TF NT (14-28g)", {"ims nanotech", "ims nanoquartz", "ims nt"}, 58, 14, 28, WP::Straight, true, FR::Standard, 715, 300, QStringLiteral("304 + Nanoquartz non-stick coating"), QStringLiteral("Competition geometry plus a water/oil-repellent non-stick coating for clean puck release.")},

        // --- S-Works (CNC-machined from solid 17-4 PH stainless billet,
        //     0.75mm walls, holes to the edge; each model is ALSO sold in
        //     Reduced / Standard / High flow hole patterns, so the flow class
        //     below is the default — the user's chosen pattern can shift it) ---
        {"S-Works", "Billet Basket", {"sworks billet", "s-works billet", "sworks basket", "sworks"}, 58, 18, 20, WP::Straight, true, FR::Standard, 0, 0, QStringLiteral("17-4 PH stainless billet"), QStringLiteral("Machined billet, holes to the edge; sold in Reduced/Standard/High flow patterns (~4% open area standard).")},
        {"S-Works", "Tapered Billet", {"sworks tapered", "s-works tapered"}, 58, 18, 20, WP::Tapered, true, FR::Standard, 0, 0, QStringLiteral("17-4 PH stainless billet"), QStringLiteral("Truncated-cone billet for a 'blended', rounder shot.")},
        {"S-Works", "Convex Billet", {"sworks convex", "s-works convex"}, 58, 17, 19, WP::Convex, true, FR::Standard, 0, 0, QStringLiteral("17-4 PH stainless billet"), QStringLiteral("Raised centre floor; needs a coarser grind, favours blooming shots.")},
        {"S-Works", "Step Down Billet", {"sworks step down", "s-works step down", "sworks stepdown"}, 58, 12, 20, WP::Stepped, true, FR::Standard, 0, 0, QStringLiteral("17-4 PH stainless billet"), QStringLiteral("Necks 58mm to a ~49/32mm brew bed for a deeper, more forgiving puck.")},
        {"S-Works", "Step Down Stamped", {"sworks step down stamped", "s-works stamped", "sworks stamped step down"}, 58, 12, 20, WP::Stepped, false, FR::Standard, 600, 0, QStringLiteral("304 stainless, stamped"), QStringLiteral("Budget ~$30 stamped step-down; 58mm necks to a 50mm brew bed (~600 holes).")},
        {"S-Works", "Titanium Billet", {"sworks titanium", "s-works titanium"}, 58, 18, 20, WP::Straight, true, FR::Standard, 0, 0, QStringLiteral("solid titanium"), QStringLiteral("Halo item — ~30% lighter, each hole individually drilled.")},

        // --- Graph Coffee (step-down baskets that convert a 58mm group to a
        //     deep, narrow 46mm puck; need a bottomless portafilter) ---
        {"Graph Coffee", "Stepped 58→46mm", {"graph stepped", "graph 58 46", "graph step down"}, 58, 16, 22, WP::Stepped, true, FR::Standard, 648, 300, QStringLiteral("laser-perforated stainless"), QStringLiteral("Steps a 58mm portafilter down to a 46mm deep puck; 648 holes at 0.30mm.")},

        // --- Pesado ---
        {"Pesado", "HE High Extraction", {"pesado he", "pesado high extraction", "pesado 58.5", "pesado"}, 58, 17, 19, WP::Straight, true, FR::Open, 715, 300, QStringLiteral("1.1mm stainless, electro-polished"), QStringLiteral("Funnel-shaped holes to the edge; high open area, faster flow, favours light roasts.")},
        {"Pesado", "EP Electro-Polished", {"pesado ep", "pesado electro polished"}, 58, 17, 19, WP::Tapered, false, FR::Standard, 0, 0, QStringLiteral("0.5mm stainless, electro-polished"), QStringLiteral("Budget electro-polished stock-replacement; smoother holes, more forgiving.")},

        // --- Normcore (two flow tiers by hole geometry) ---
        {"Normcore", "HE 0.28mm Standard Flow", {"normcore 0.28", "normcore standard flow", "normcore he 0.28"}, 58, 17, 19, WP::Straight, true, FR::Standard, 786, 280, QStringLiteral("0.8mm 304 stainless"), QStringLiteral("786 holes — Normcore's faster-flow tier; finer grind, higher clarity (light/medium).")},
        {"Normcore", "HE 0.18mm Reduced Flow", {"normcore 0.18", "normcore reduced flow", "normcore he 0.18"}, 58, 17, 19, WP::Straight, true, FR::Restrictive, 2475, 180, QStringLiteral("0.8mm 304 stainless"), QStringLiteral("2475 holes — slower 'reduced flow' tier; thicker texture, coarser grind tolerance (medium/dark).")},
        {"Normcore", "58→32mm Convex Step-Down", {"normcore step down", "normcore convex", "normcore 58 32"}, 58, 21, 23, WP::Convex, true, FR::Standard, 444, 280, QStringLiteral("stainless steel"), QStringLiteral("Convex step-down forms a deeper, focused puck; sold with puck screen.")},

        // --- Pullman ---
        {"Pullman", "Filtration876", {"pullman 876", "pullman filtration876", "pullman", "filtration876"}, 58, 17, 19, WP::Straight, true, FR::Standard, 876, 300, QStringLiteral("AISI 304 stainless, polished"), QStringLiteral("Guaranteed 58.7mm bore, 876 holes at 0.30mm; pairs with the BigStep tamper.")},

        // --- Wafo (boutique 316L, naturally curved bottom) ---
        {"Wafo", "Spirit", {"wafo spirit", "wafo soe spirit"}, 58, 17, 19, WP::Straight, true, FR::Open, 0, 0, QStringLiteral("316L stainless"), QStringLiteral("Boutique full-coverage pattern — shortest flow path, the most even extraction / clearest cup.")},
        {"Wafo", "Checkmate", {"wafo checkmate", "wafo blend"}, 58, 17, 19, WP::Straight, true, FR::Standard, 0, 0, QStringLiteral("316L stainless"), QStringLiteral("Intentionally irregular hole layout for a deliberately distinctive flavour.")},

        // --- E&B Lab (made by IMS; ridgeless competition, optional coating) ---
        {"E&B Lab", "Superfine Competition", {"eb lab", "e&b lab", "eb lab competition", "e&b superfine"}, 58, 17, 19, WP::Straight, true, FR::Standard, 715, 0, QStringLiteral("AISI 304, electropolished"), QStringLiteral("Ridgeless competition double with extra edge holes; optional nanotech coating.")},

        // --- La Marzocco (current stock on LM machines is itself precision) ---
        {"La Marzocco", "Advanced Precision", {"la marzocco advanced", "lm advanced precision", "la marzocco stock", "lm stock"}, 58, 16, 18, WP::Straight, true, FR::Standard, 0, 0, QStringLiteral("brushed stainless"), QStringLiteral("LM stock precision basket with digital-scan QC; fits Strada/Linea/GS3/Micra.")},
        {"La Marzocco", "Strada (VST-developed)", {"la marzocco strada", "lm strada", "strada basket"}, 58, 16, 18, WP::Straight, true, FR::Standard, 0, 0, QStringLiteral("brushed stainless"), QStringLiteral("Older LM precision line co-developed with VST.")},

        // --- MHW-3BOMBER (widely-sold budget precision basket; the common
        //     VST/IMS alternative on Amazon. Brand states 58.5mm, 872 holes,
        //     straight-flat bottom; hole Ø not brand-published) ---
        {"MHW-3BOMBER", "Filter Basket DEX", {"mhw 3bomber", "mhw3bomber", "3bomber", "mhw dex", "bomber dex"}, 58, 15, 22, WP::Straight, true, FR::Standard, 872, 0, QStringLiteral("AISI 304 stainless"), QStringLiteral("Budget 58.5mm precision basket, 872 laser-cut holes; sold in 15/18/20/22g.")},

        // --- Generic / unbranded catch-all (for a no-name or stock basket;
        //     non-precision stamped, smaller open area → slower, variable) ---
        {"Generic", "Other / Unbranded", {"generic basket", "stock basket", "stock double", "stamped basket", "unbranded basket"}, 58, 14, 18, WP::Tapered, false, FR::Restrictive, 0, 0, QStringLiteral("stamped stainless"), QStringLiteral("Unbranded single-wall stamped basket; non-precision, smaller open area, hole pattern varies basket-to-basket.")},
    };
    return entries;
}

// Map a wall profile to a short human/AI-readable word.
inline QString wallProfileName(WallProfile w)
{
    switch (w) {
    case WallProfile::Straight: return QStringLiteral("straight");
    case WallProfile::Tapered:  return QStringLiteral("tapered");
    case WallProfile::Stepped:  return QStringLiteral("stepped");
    case WallProfile::Convex:   return QStringLiteral("convex");
    }
    return QStringLiteral("straight");
}

// Map a flow class to a short human/AI-readable word.
inline QString flowRateName(FlowRate f)
{
    switch (f) {
    case FlowRate::Restrictive: return QStringLiteral("restrictive");
    case FlowRate::Standard:    return QStringLiteral("standard");
    case FlowRate::Open:        return QStringLiteral("open");
    }
    return QStringLiteral("standard");
}

// Case-insensitive lookup by alias, longest-match-first (so "vst 18g ridgeless"
// beats "vst"). Mirrors GrinderAliases::lookup. Returns found=false when no
// alias matches; callers fall back to plain-numeric / no-enrichment behaviour.
inline LookupResult lookup(const QString& rawBasketString)
{
    LookupResult result;
    if (rawBasketString.trimmed().isEmpty())
        return result;

    const QString lower = rawBasketString.trimmed().toLower();
    const auto& baskets = allBaskets();

    qsizetype bestLen = 0;
    const BasketEntry* bestMatch = nullptr;

    // First pass: exact alias match
    for (const auto& entry : baskets) {
        for (const auto& alias : entry.aliases) {
            if (lower == alias && alias.length() > bestLen) {
                bestLen = alias.length();
                bestMatch = &entry;
            }
        }
    }

    // Second pass: substring match (for strings like "VST 18g (ridgeless)")
    if (!bestMatch) {
        for (const auto& entry : baskets) {
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
        result.doseMinG = bestMatch->doseMinG;
        result.doseMaxG = bestMatch->doseMaxG;
        result.precision = bestMatch->precision;
        result.flow = bestMatch->flow;
        result.found = true;
    }

    return result;
}

// All known brands, unique and sorted (drives the vendor picker, like grinders).
inline QStringList allBrands()
{
    QStringList brands;
    for (const auto& entry : allBaskets()) {
        if (!brands.contains(entry.brand))
            brands << entry.brand;
    }
    brands.sort(Qt::CaseInsensitive);
    return brands;
}

// All known models for a brand, in registry order.
inline QStringList modelsForBrand(const QString& brand)
{
    QStringList models;
    for (const auto& entry : allBaskets()) {
        if (entry.brand.compare(brand, Qt::CaseInsensitive) == 0)
            models << entry.model;
    }
    return models;
}

// Find the registry entry for an exact brand+model (case-insensitive).
// Returns nullptr when not found.
inline const BasketEntry* findEntry(const QString& brand, const QString& model)
{
    const auto& baskets = allBaskets();
    for (const auto& e : baskets) {
        if (e.brand.compare(brand, Qt::CaseInsensitive) == 0
            && e.model.compare(model, Qt::CaseInsensitive) == 0)
            return &e;
    }
    return nullptr;
}

// Resolve a raw basket string (e.g. a free-text shot field) to a registry
// entry by alias in one call. Returns nullptr when no alias matches.
inline const BasketEntry* findEntryByAlias(const QString& raw)
{
    const LookupResult r = lookup(raw);
    return r.found ? findEntry(r.brand, r.model) : nullptr;
}

// Build a compact, AI-facing summary of a basket's brewing-relevant traits.
// e.g. the Pesado HE row renders as:
//   "58mm, straight-wall, precision, open flow, 17-19g, 715 holes @300µm".
// Empty/zero fields are omitted so unpublished specs don't read as zeros, and
// the "standard" flow class is omitted (it's the unremarkable default).
inline QString summary(const BasketEntry& b)
{
    QStringList parts;
    if (b.diameterMm > 0)
        parts << QString::number(b.diameterMm) + QStringLiteral("mm");
    parts << wallProfileName(b.wall) + QStringLiteral("-wall");
    if (b.precision)
        parts << QStringLiteral("precision");
    if (b.flow != FlowRate::Standard)
        parts << flowRateName(b.flow) + QStringLiteral(" flow");
    if (b.doseMaxG > 0) {
        auto g = [](double v) {
            QString s = QString::number(v, 'f', 1);
            if (s.endsWith(QStringLiteral(".0"))) s.chop(2);
            return s;
        };
        parts << g(b.doseMinG) + QStringLiteral("-") + g(b.doseMaxG) + QStringLiteral("g");
    }
    if (b.holeCount > 0) {
        QString holes = QString::number(b.holeCount) + QStringLiteral(" holes");
        if (b.holeDiameterMicrons > 0)
            holes += QStringLiteral(" @") + QString::number(b.holeDiameterMicrons) + QStringLiteral("µm");
        parts << holes;
    } else if (b.holeDiameterMicrons > 0) {
        parts << QString::number(b.holeDiameterMicrons) + QStringLiteral("µm holes");
    }
    return parts.join(QStringLiteral(", "));
}

} // namespace BasketAliases
