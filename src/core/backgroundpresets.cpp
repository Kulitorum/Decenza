#include "backgroundpresets.h"

namespace BackgroundPresets {

namespace {

QString overlayKindName(OverlayKind kind) {
    return kind == OverlayKind::Tile ? QStringLiteral("tile") : QStringLiteral("none");
}

// Shorthand for a solid entry.
Preset solid(const char* id, const char* name, const char* color) {
    return Preset{QString::fromLatin1(id),
                  QStringLiteral("backgroundPreset.") + QString::fromLatin1(id),
                  QString::fromLatin1(name),
                  QString::fromLatin1(color),
                  OverlayKind::None, QString(), 0.0, 0};
}

// Shorthand for a patterned entry. `asset` is the tile basename.
Preset patterned(const char* id, const char* name, const char* color,
                 const char* asset, double opacity, int tile) {
    return Preset{QString::fromLatin1(id),
                  QStringLiteral("backgroundPreset.") + QString::fromLatin1(id),
                  QString::fromLatin1(name),
                  QString::fromLatin1(color),
                  OverlayKind::Tile,
                  QStringLiteral("qrc:/backgrounds/") + QString::fromLatin1(asset) + QStringLiteral(".svg"),
                  opacity, tile};
}

}  // namespace

const QVector<Preset>& catalogue() {
    // Twenty backgrounds spanning near-black to near-white, all available all the time.
    //
    // They are NOT tied to light/dark mode. An earlier design paired each preset to a
    // dark and a light value so the theme's text colour would always land on a suitable
    // background — safe, but it meant that in dark mode every option on offer was a
    // near-black, which is not a choice. Instead the foreground is DERIVED from the
    // chosen colour (Theme.qml's preset surface derivation), so a pale background is
    // readable under a dark theme and vice versa.
    //
    // Ordered dark to light so the chooser reads as a ramp rather than a jumble. Colours
    // are desaturated on purpose: these exist for people who found the screensaver photos
    // too much to work in front of.
    static const QVector<Preset> presets = {
        // --- Deep -------------------------------------------------------------
        solid("graphite", "Graphite", "#14161a"),
        solid("slate",    "Slate",    "#181f2b"),
        solid("espresso", "Espresso", "#1e1815"),
        solid("forest",   "Forest",   "#131e19"),
        solid("plum",     "Plum",     "#1c1620"),

        // Patterns on the deep tones. Opacity lives here so tuning a pattern is a data
        // edit; the tile edge is the authored size in px and becomes Image.sourceSize —
        // a tile whose scaled size lands off an integer shimmers.
        patterned("grain",     "Grain",     "#1e1815", "grain",     0.05, 32),
        patterned("linen",     "Linen",     "#14161a", "linen",     0.05, 16),
        patterned("twill",     "Twill",     "#181f2b", "twill",     0.05, 8),
        patterned("pinstripe", "Pinstripe", "#131e19", "pinstripe", 0.04, 8),
        patterned("dots",      "Dot Grid",  "#1c1620", "dots",      0.04, 12),

        // --- Mid --------------------------------------------------------------
        // The rung that was missing entirely: clearly lighter than a near-black, still a
        // dark UI, derived text still white.
        //
        // They sit at L* 25-29 and cannot go higher, because a mid-tone page cannot carry
        // monochrome text. Between roughly L* 30 and 58 neither black nor white clears
        // 4.5:1 once secondary text is softened at all, and from there to about L* 85 a
        // card cannot separate perceptibly from the page at the glass-chrome lift. That
        // dead band is a property of mid-greys, not a tuning failure — so the catalogue
        // stays on the two shoulders either side of it, and the contrast tests enforce it.
        solid("ash",    "Ash",    "#383d45"),
        solid("denim",  "Denim",  "#333f4f"),
        patterned("oxford", "Oxford", "#3a4452", "twill", 0.06, 8),

        // --- Light ------------------------------------------------------------
        solid("chalk",  "Chalk",  "#f2f3f5"),
        solid("mist",   "Mist",   "#e6ebf2"),
        solid("cream",  "Cream",  "#f5efe8"),
        solid("sage",   "Sage",   "#e6efe8"),
        solid("lilac",  "Lilac",  "#f0eaf4"),
        patterned("parchment", "Parchment", "#efe7da", "grain", 0.06, 32),
        patterned("dove",      "Dove",      "#e8eaee", "dots",  0.05, 12),
    };
    return presets;
}

Preset byId(const QString& id) {
    if (id.isEmpty())
        return Preset{};
    for (const Preset& preset : catalogue()) {
        if (preset.id == id)
            return preset;
    }
    return Preset{};
}

bool contains(const QString& id) {
    return !byId(id).id.isEmpty();
}

QVariantMap toVariantMap(const Preset& preset) {
    if (preset.id.isEmpty())
        return {};

    QVariantMap map;
    map["id"] = preset.id;
    map["nameKey"] = preset.nameKey;
    map["nameFallback"] = preset.nameFallback;
    map["color"] = preset.color;
    map["overlayKind"] = overlayKindName(preset.overlayKind);
    map["overlayAsset"] = preset.overlayAsset;
    map["overlayOpacity"] = preset.overlayOpacity;
    map["overlayTile"] = preset.overlayTile;
    return map;
}

QVariantList toVariantList() {
    QVariantList list;
    for (const Preset& preset : catalogue())
        list.append(toVariantMap(preset));
    return list;
}

}  // namespace BackgroundPresets
