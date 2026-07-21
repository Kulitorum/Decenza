#include "backgroundpresets.h"

namespace BackgroundPresets {

namespace {

Colour colour(const char* id, const char* name, const char* value) {
    return Colour{QString::fromLatin1(id),
                  QStringLiteral("backgroundColour.") + QString::fromLatin1(id),
                  QString::fromLatin1(name),
                  QString::fromLatin1(value)};
}

Pattern pattern(const char* id, const char* name, const char* asset,
                double opacity, int tile, double coverage) {
    return Pattern{QString::fromLatin1(id),
                   QStringLiteral("backgroundPattern.") + QString::fromLatin1(id),
                   QString::fromLatin1(name),
                   QStringLiteral("qrc:/backgrounds/") + QString::fromLatin1(asset) + QStringLiteral(".svg"),
                   opacity, tile, coverage};
}

}  // namespace

const QVector<Colour>& colours() {
    // Drawn from coffee and from the things around it — roast levels, milk drinks, the
    // machine and the cup — rather than invented greys. An earlier set was neutral by
    // default and read as "eight variations on charcoal, seven on off-white": no range and
    // nothing anyone would pick on purpose.
    //
    // Ordered dark to light so the chooser reads as a ramp. The gap between L* 22 and 66 is
    // deliberate and measured: between roughly L* 30 and 58 neither black nor white text
    // clears 4.5:1 once secondary text is softened, so a mid-grey page cannot carry a
    // monochrome-text UI at all. The contrast tests reject anything placed there.
    static const QVector<Colour> table = {
        // --- Roast: near-black through dark browns -----------------------------
        colour("french-roast", "French Roast",  "#1b1512"),
        colour("cold-brew",    "Cold Brew",     "#14181f"),
        colour("espresso",     "Espresso",      "#241a14"),
        colour("green-bean",   "Green Bean",    "#172018"),
        colour("ristretto",    "Ristretto",     "#2a1a1a"),
        colour("cast-iron",    "Cast Iron",     "#1e1e21"),
        colour("walnut",       "Walnut",        "#2b2019"),
        colour("barista",      "Barista",       "#1c2432"),

        // --- Machine: the lighter dark rung ------------------------------------
        colour("machine-steel", "Machine Steel", "#2b3038"),
        colour("denim-apron",   "Denim Apron",   "#28354a"),

        // --- Milk: mid-light through to the cup --------------------------------
        colour("brushed-steel", "Brushed Steel", "#9aa1a8"),
        colour("cortado",       "Cortado",       "#b9a184"),
        colour("latte",         "Latte",         "#c8b09a"),
        colour("oat-milk",      "Oat Milk",      "#ded3c2"),
        colour("crema",         "Crema",         "#e8d5b5"),
        colour("cappuccino",    "Cappuccino",    "#e5d9cc"),
        colour("steam",         "Steam",         "#e9eef2"),
        colour("flat-white",    "Flat White",    "#f2ece4"),
        colour("porcelain",     "Porcelain",     "#f0f2f5"),
    };
    return table;
}

const QVector<Pattern>& patterns() {
    // Opacities are much higher than the first attempt's 4-6%, which was invisible on a
    // real screen — "subtle" overshot into "absent". They are still safe because a pattern
    // is sparse: what matters for legibility is opacity WEIGHTED BY COVERAGE, and the
    // densest tile here shifts the page by about 4%.
    //
    // `coverage` is the ink fraction of each tile, measured from the artwork; keep it in
    // step if a tile is redrawn, because the contrast test relies on it.
    static const QVector<Pattern> table = {
        pattern("grain",     "Grain",     "grain",     0.18, 32, 0.09),
        pattern("dots",      "Dot Grid",  "dots",      0.18, 12, 0.09),
        pattern("pinstripe", "Pinstripe", "pinstripe", 0.18, 8,  0.13),
        pattern("twill",     "Twill",     "twill",     0.16, 8,  0.18),
        pattern("weave",     "Weave",     "weave",     0.14, 12, 0.22),
        pattern("linen",     "Linen",     "linen",     0.14, 16, 0.30),
    };
    return table;
}

Colour colourById(const QString& id) {
    if (id.isEmpty())
        return Colour{};
    for (const Colour& c : colours()) {
        if (c.id == id)
            return c;
    }
    return Colour{};
}

Pattern patternById(const QString& id) {
    if (id.isEmpty())
        return Pattern{};
    for (const Pattern& p : patterns()) {
        if (p.id == id)
            return p;
    }
    return Pattern{};
}

bool hasColour(const QString& id) { return !colourById(id).id.isEmpty(); }
bool hasPattern(const QString& id) { return !patternById(id).id.isEmpty(); }

double contrastShift(const Pattern& pattern) {
    return pattern.id.isEmpty() ? 0.0 : pattern.opacity * pattern.coverage;
}

QVariantMap colourToVariantMap(const Colour& c) {
    if (c.id.isEmpty())
        return {};
    QVariantMap map;
    map["id"] = c.id;
    map["nameKey"] = c.nameKey;
    map["nameFallback"] = c.nameFallback;
    map["value"] = c.value;
    return map;
}

QVariantMap patternToVariantMap(const Pattern& p) {
    if (p.id.isEmpty())
        return {};
    QVariantMap map;
    map["id"] = p.id;
    map["nameKey"] = p.nameKey;
    map["nameFallback"] = p.nameFallback;
    map["asset"] = p.asset;
    map["opacity"] = p.opacity;
    map["tile"] = p.tile;
    return map;
}

QVariantList coloursAsVariantList() {
    QVariantList list;
    for (const Colour& c : colours())
        list.append(colourToVariantMap(c));
    return list;
}

QVariantList patternsAsVariantList() {
    QVariantList list;
    for (const Pattern& p : patterns())
        list.append(patternToVariantMap(p));
    return list;
}

}  // namespace BackgroundPresets
