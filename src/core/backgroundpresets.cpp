#include "backgroundpresets.h"

namespace BackgroundPresets {

namespace {

QString overlayKindName(OverlayKind kind) {
    return kind == OverlayKind::Tile ? QStringLiteral("tile") : QStringLiteral("none");
}

}  // namespace

const QVector<Preset>& catalogue() {
    // Neutral-leaning and desaturated on purpose: these exist for people who found the
    // screensaver photos too much to work in front of, so a saturated background would
    // miss the request. Every entry is meant to be a background someone uses all day.
    //
    // Each is a dark/light PAIR rather than a single colour. Text colour comes from the
    // theme and does not follow the background, so a single-value preset would make the
    // app unreadable in whichever mode it was not picked for. Pairing removes that
    // footgun instead of guarding it with a warning or a filter.
    //
    // The five patterns each sit on a different solid's pair, so the ten entries are ten
    // distinct backgrounds rather than five colours shown twice. A pattern inherits its
    // base pair wholesale — deliberately NOT a colour × texture grid, which would be a
    // second axis for the user to get wrong.
    static const QVector<Preset> presets = {
        // --- Solids -------------------------------------------------------------
        {QStringLiteral("graphite"), QStringLiteral("backgroundPreset.graphite"), QStringLiteral("Graphite"),
         QStringLiteral("#14161a"), QStringLiteral("#f2f3f5"),
         OverlayKind::None, QString(), 0.0, 0},
        {QStringLiteral("slate"), QStringLiteral("backgroundPreset.slate"), QStringLiteral("Slate"),
         QStringLiteral("#181f2b"), QStringLiteral("#eceff4"),
         OverlayKind::None, QString(), 0.0, 0},
        {QStringLiteral("espresso"), QStringLiteral("backgroundPreset.espresso"), QStringLiteral("Espresso"),
         QStringLiteral("#1e1815"), QStringLiteral("#f5efe8"),
         OverlayKind::None, QString(), 0.0, 0},
        {QStringLiteral("forest"), QStringLiteral("backgroundPreset.forest"), QStringLiteral("Forest"),
         QStringLiteral("#131e19"), QStringLiteral("#eaf1ec"),
         OverlayKind::None, QString(), 0.0, 0},
        {QStringLiteral("plum"), QStringLiteral("backgroundPreset.plum"), QStringLiteral("Plum"),
         QStringLiteral("#1c1620"), QStringLiteral("#f2edf4"),
         OverlayKind::None, QString(), 0.0, 0},

        // --- Patterns -----------------------------------------------------------
        // Opacity lives here so tuning a pattern is a data edit, not a QML edit. The
        // tile edge is authored-size in px and becomes Image.sourceSize when tiling —
        // a tile whose scaled size lands off an integer shimmers.
        {QStringLiteral("grain"), QStringLiteral("backgroundPreset.grain"), QStringLiteral("Grain"),
         QStringLiteral("#1e1815"), QStringLiteral("#f5efe8"),
         OverlayKind::Tile, QStringLiteral("qrc:/backgrounds/grain.svg"), 0.05, 32},
        {QStringLiteral("linen"), QStringLiteral("backgroundPreset.linen"), QStringLiteral("Linen"),
         QStringLiteral("#14161a"), QStringLiteral("#f2f3f5"),
         OverlayKind::Tile, QStringLiteral("qrc:/backgrounds/linen.svg"), 0.05, 16},
        {QStringLiteral("twill"), QStringLiteral("backgroundPreset.twill"), QStringLiteral("Twill"),
         QStringLiteral("#181f2b"), QStringLiteral("#eceff4"),
         OverlayKind::Tile, QStringLiteral("qrc:/backgrounds/twill.svg"), 0.05, 8},
        {QStringLiteral("pinstripe"), QStringLiteral("backgroundPreset.pinstripe"), QStringLiteral("Pinstripe"),
         QStringLiteral("#131e19"), QStringLiteral("#eaf1ec"),
         OverlayKind::Tile, QStringLiteral("qrc:/backgrounds/pinstripe.svg"), 0.04, 8},
        {QStringLiteral("dots"), QStringLiteral("backgroundPreset.dots"), QStringLiteral("Dot Grid"),
         QStringLiteral("#1c1620"), QStringLiteral("#f2edf4"),
         OverlayKind::Tile, QStringLiteral("qrc:/backgrounds/dots.svg"), 0.04, 12},
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

QVariantMap toVariantMap(const Preset& preset, bool darkMode) {
    if (preset.id.isEmpty())
        return {};

    QVariantMap map;
    map["id"] = preset.id;
    map["nameKey"] = preset.nameKey;
    map["nameFallback"] = preset.nameFallback;
    map["darkColor"] = preset.darkColor;
    map["lightColor"] = preset.lightColor;
    map["color"] = darkMode ? preset.darkColor : preset.lightColor;
    map["overlayKind"] = overlayKindName(preset.overlayKind);
    map["overlayAsset"] = preset.overlayAsset;
    map["overlayOpacity"] = preset.overlayOpacity;
    map["overlayTile"] = preset.overlayTile;
    return map;
}

QVariantList toVariantList(bool darkMode) {
    QVariantList list;
    for (const Preset& preset : catalogue()) {
        // Every entry carries both colours as well as the resolved `color`, so a caller
        // that wants to show the other mode's value does not need a second call.
        list.append(toVariantMap(preset, darkMode));
    }
    return list;
}

}  // namespace BackgroundPresets
