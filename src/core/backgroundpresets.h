#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

// Built-in app backgrounds — the calm alternative to a screensaver photo.
//
// Two independent tables: a COLOUR and, optionally, a PATTERN drawn over it. They are
// separate axes because baking them together produced a catalogue where half the entries
// were near-invisible variants of the other half; as a pair of choices, every colour can
// carry every pattern from two short rows of tiles.
//
// Both live in C++ rather than QML so they can be unit-tested headlessly (contrast floors,
// asset existence) and so a future web/MCP surface reads one definition rather than a
// second copy. Same reasoning as widgetCatalogTable() in settings_network.cpp.
//
// A colour supplies the app's flat background; it is never stored as a background image
// (Settings.theme.backgroundImagePath stays empty). Everything readable on it — text,
// secondary text, icons, borders, card fills — is DERIVED from it in Theme.qml, which is
// what lets a pale background work under a dark theme and vice versa.
namespace BackgroundPresets {

struct Colour {
    QString id;
    QString nameKey;       // translation key
    QString nameFallback;  // English
    QString value;         // "#rrggbb"
};

struct Pattern {
    QString id;
    QString nameKey;
    QString nameFallback;
    QString asset;      // qrc path
    double opacity;     // tint strength over the colour
    int tile;           // authored tile edge in px; sets Image.sourceSize when tiling
    double coverage;    // fraction of the tile that is ink — see contrastShift()
};

// Ordered dark to light.
const QVector<Colour>& colours();

// Ordered loosely by how much texture they add. "None" is not an entry; an empty id means
// no pattern.
const QVector<Pattern>& patterns();

// Lookups. Return an entry with an empty id when `id` is unknown or empty, which callers
// treat as "none" — that is how a stale or hand-edited settings value degrades gracefully
// rather than rendering an undefined background.
Colour colourById(const QString& id);
Pattern patternById(const QString& id);
bool hasColour(const QString& id);
bool hasPattern(const QString& id);

// The average luminance shift a pattern imposes on the page: opacity weighted by how much
// of the tile is actually ink. A hairline at 18% shifts the pixels it covers a lot and the
// page as a whole very little, so the honest figure for "can text still be read over this"
// is the coverage-weighted one, not the raw opacity.
double contrastShift(const Pattern& pattern);

// As QML sees them: lists of maps, table order preserved.
QVariantList coloursAsVariantList();
QVariantList patternsAsVariantList();
QVariantMap colourToVariantMap(const Colour& colour);
QVariantMap patternToVariantMap(const Pattern& pattern);

}  // namespace BackgroundPresets
