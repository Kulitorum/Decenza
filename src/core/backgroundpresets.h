#pragma once

#include <QColor>
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

// --- Deriving a readable foreground from a background colour ------------------
//
// A background colour may be anywhere from near-black to near-white and is offered under
// any theme, so legibility cannot come from the palette — everything that has to sit on
// the page is computed from it. This lives in C++ rather than QML so the contrast floors
// in the test measure THE SHIPPED CODE. They previously measured a copy of this arithmetic
// kept in the test file, which meant changing a constant here left the suite green.

// How far a surface sits from the page, in CIE L*. A card should whisper; an action tile
// has to read as something you press, so they are not the same step.
inline constexpr double kCardLift = 6.0;
inline constexpr double kTileLift = 12.0;
// Secondary text, as a fraction of the way from the text colour back toward the page.
// Close to the ceiling: the tightest colour fails 4.5:1 above ~0.30 once the action tile
// is included, and the mid-tones have less headroom than either end of the ramp.
inline constexpr double kSecondaryMix = 0.28;
// Border, as a fraction of the way from the page toward the text colour.
inline constexpr double kBorderMix = 0.22;

double relativeLuminance(const QColor& c);
double lstar(const QColor& c);
double contrastRatio(const QColor& a, const QColor& b);
QColor mixColours(const QColor& a, const QColor& b, double t);

// Black or white, whichever is genuinely more readable on `fill` — by comparing the two
// real contrast ratios, not by thresholding a brightness value.
QColor contrastColorFor(const QColor& fill);

// A surface `deltaL` away from `base` in perceptual lightness, found by bisection.
// Deliberately a fixed step in L* rather than a fixed RGB fraction: a fixed fraction is a
// large perceptual move near black and almost nothing at L* 70, which is what forced an
// earlier catalogue to cluster at the two extremes. Lifts toward white where there is
// headroom above and toward black where there is not, so it never returns `base`.
QColor liftFrom(const QColor& base, double deltaL);

// Everything the app paints on a background colour.
struct Derived {
    QColor background;
    QColor text;
    QColor textSecondary;
    QColor surface;      // cards, dialogs, bars
    QColor actionTile;   // pressable tiles, which need the larger step
    QColor border;
};
Derived derive(const QColor& background);

// As QML sees them: lists of maps, table order preserved.
QVariantList coloursAsVariantList();
QVariantList patternsAsVariantList();
QVariantMap colourToVariantMap(const Colour& colour);
// The derived set as QML reads it, keys matching Derived's fields. Empty map for an
// invalid colour.
QVariantMap deriveAsVariantMap(const QColor& background);
QVariantMap patternToVariantMap(const Pattern& pattern);

}  // namespace BackgroundPresets
