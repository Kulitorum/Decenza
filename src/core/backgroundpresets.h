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
    QString color;          // the background colour; foreground is derived from it
    OverlayKind overlayKind = OverlayKind::None;
    QString overlayAsset;   // qrc path, empty for OverlayKind::None
    double overlayOpacity = 0.0;
    int overlayTile = 0;    // authored tile edge in px; sets Image.sourceSize when tiling
};

// The twenty presets, ordered dark to light. NOT paired to light/dark mode: every
// preset is available under every theme, and the readable foreground is derived from
// the preset colour (see Theme.qml) rather than taken from the palette — which is what
// lets a pale background work under a dark theme without leaving white text on it.
const QVector<Preset>& catalogue();

// Lookup by id. Returns a Preset with an empty id when `id` is unknown or empty —
// callers treat that as "no preset", which is how a stale or hand-edited settings
// value degrades gracefully instead of rendering an undefined background.
Preset byId(const QString& id);

bool contains(const QString& id);

// The catalogue as QML sees it: a list of maps, catalogue order preserved.
QVariantList toVariantList();

// One entry as a map. Empty map when `id` is unknown.
QVariantMap toVariantMap(const Preset& preset);

}  // namespace BackgroundPresets
