#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

// Built-in app background presets — the calm alternative to a screensaver photo.
//
// This table is the single source of truth for preset identity, colour and texture.
// It lives in C++ rather than QML so it can be unit-tested headlessly (contrast floors,
// asset existence) and so a future web/MCP surface has one definition to read rather
// than a second copy. Same reasoning as widgetCatalogTable() in settings_network.cpp.
//
// A preset supplies the app's flat background COLOUR; it is never stored as a background
// image (Settings.theme.backgroundImagePath stays empty). It does, however, drive the same
// translucent chrome an image does — see Theme.glassChrome.
namespace BackgroundPresets {

// How the pattern above the flat colour is drawn.
enum class OverlayKind {
    None,  // solid colour, no overlay
    Tile   // asset tiled at overlayOpacity, tinted with the theme's text colour
};

struct Preset {
    QString id;
    QString nameKey;        // translation key
    QString nameFallback;   // English
    QString darkColor;      // used when Settings.theme.isDarkMode
    QString lightColor;     // used otherwise
    OverlayKind overlayKind = OverlayKind::None;
    QString overlayAsset;   // qrc path, empty for OverlayKind::None
    double overlayOpacity = 0.0;
    int overlayTile = 0;    // authored tile edge in px; sets Image.sourceSize when tiling
};

// The ten presets, five solids followed by five patterns.
const QVector<Preset>& catalogue();

// Lookup by id. Returns a Preset with an empty id when `id` is unknown or empty —
// callers treat that as "no preset", which is how a stale or hand-edited settings
// value degrades gracefully instead of rendering an undefined background.
Preset byId(const QString& id);

bool contains(const QString& id);

// The catalogue as QML sees it: a list of maps, catalogue order preserved. `darkMode`
// resolves each entry's `color` field, so the chooser's tiles show what the user would
// actually get right now rather than always the dark value.
QVariantList toVariantList(bool darkMode);

// One entry as a map, with `color` already resolved for the requested mode, plus the
// overlay fields. Empty map when `id` is unknown.
QVariantMap toVariantMap(const Preset& preset, bool darkMode);

}  // namespace BackgroundPresets
