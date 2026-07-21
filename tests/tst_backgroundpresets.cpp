#include <QtTest>
#include <QColor>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <QSet>
#include <QSignalSpy>

#include "core/backgroundpresets.h"
#include "core/settings.h"
#include "core/settings_theme.h"

// Background presets + the glass-chrome option.
//
// The contrast checks here are the point of putting the catalogue in C++ at all: over a
// screensaver photo what sits behind a translucent card is unknowable, so the scrim alpha
// was tuned by eye. A preset is a KNOWN colour, so everything derived from it — text,
// secondary text, card fill — is exact arithmetic, and therefore testable rather than
// merely intended. That derivation is what allows all twenty presets, from near-black to
// near-white, to be offered under any theme.
class TestBackgroundPresets : public QObject {
    Q_OBJECT

private:
    // WCAG 2.x relative luminance. Mirrors Theme.qml's _relativeLuminance — the same
    // formula has to exist here because the QML one is not reachable from a headless test.
    static double luminance(const QColor& c) {
        auto linearise = [](double channel) {
            return channel <= 0.03928 ? channel / 12.92
                                      : std::pow((channel + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * linearise(c.redF())
             + 0.7152 * linearise(c.greenF())
             + 0.0722 * linearise(c.blueF());
    }

    static double contrast(const QColor& a, const QColor& b) {
        const double la = luminance(a);
        const double lb = luminance(b);
        return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
    }

    static constexpr double kMinContrast = 4.5;

    // Perceptual lightness (CIE L*, 0-100). Used where a WCAG ratio is the wrong tool —
    // see glassSurfaceSeparatesFromBackground.
    static double lstar(const QColor& c) {
        const double y = luminance(c);
        return y <= 0.008856 ? 903.3 * y : 116.0 * std::cbrt(y) - 16.0;
    }

    static QColor mix(const QColor& a, const QColor& b, double t) {
        return QColor::fromRgbF(a.redF()   + (b.redF()   - a.redF())   * t,
                                a.greenF() + (b.greenF() - a.greenF()) * t,
                                a.blueF()  + (b.blueF()  - a.blueF())  * t);
    }

    // Mirrors Theme.contrastColorFor: black or white, by comparing the two real contrast
    // ratios rather than thresholding a brightness value.
    static QColor contrastColorFor(const QColor& fill) {
        const double l = luminance(fill);
        return (l + 0.05) / 0.05 >= 1.05 / (l + 0.05) ? QColor("#000000") : QColor("#ffffff");
    }

    // Mirrors Theme._liftFrom: a fixed step in L* (not a fixed RGB fraction), found by
    // bisection. The distinction matters — a fixed fraction is a large perceptual move
    // near black and almost nothing at L* 70, which is what previously made the whole
    // mid-light range unusable.
    static constexpr double kCardLift = 6.0;   // cards and bars
    static constexpr double kTileLift = 12.0;  // action tiles, which must read as pressable
    static QColor liftFrom(const QColor& base, double deltaL = kCardLift) {
        const QColor target = lstar(base) + deltaL <= 100.0 ? QColor("#ffffff") : QColor("#000000");
        double lo = 0.0, hi = 1.0;
        for (int i = 0; i < 24; ++i) {
            const double m = (lo + hi) / 2;
            if (std::abs(lstar(mix(base, target, m)) - lstar(base)) < deltaL) lo = m;
            else hi = m;
        }
        return mix(base, target, (lo + hi) / 2);
    }

private slots:
    void init() {
        QTest::failOnWarning();
        // Isolated per-process store — see the note in tst_settings.cpp.
        QSettings raw(Settings::testQSettingsPath(), QSettings::IniFormat);
        raw.remove("theme");
        raw.sync();
    }

    // --- Catalogue shape ---------------------------------------------------

    void coloursAreWellFormed() {
        const auto& table = BackgroundPresets::colours();
        QVERIFY2(table.size() >= 12, "catalogue is too small to offer real choice");

        QSet<QString> ids;
        for (const auto& c : table) {
            QVERIFY2(!c.id.isEmpty(), "colour id must not be empty");
            QVERIFY2(!ids.contains(c.id), qPrintable("duplicate colour id: " + c.id));
            ids.insert(c.id);
            QVERIFY2(!c.nameKey.isEmpty(), qPrintable("missing nameKey: " + c.id));
            QVERIFY2(!c.nameFallback.isEmpty(), qPrintable("missing nameFallback: " + c.id));
            QVERIFY2(QColor::isValidColorName(c.value), qPrintable("bad colour: " + c.id));
        }
    }

    // Render a tile and average its alpha — the fraction of the tile that is actually ink.
    // This is the number `contrastShift()` multiplies opacity by, so it decides how much
    // luminance shift the contrast floors are told to expect.
    static double measuredCoverage(const BackgroundPresets::Pattern& pattern) {
        QString path = pattern.asset;
        path.replace(0, 4, ":");

        QSvgRenderer renderer(path);
        if (!renderer.isValid())
            return -1.0;

        // Render well above the authored size so thin strokes and circles are measured by
        // area rather than by pixel-grid luck, then scale back to a fraction.
        const int side = pattern.tile * 16;
        QImage image(side, side, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        renderer.render(&painter, QRectF(0, 0, side, side));
        painter.end();

        double ink = 0.0;
        for (int y = 0; y < side; ++y) {
            for (int x = 0; x < side; ++x)
                ink += qAlpha(image.pixel(x, y)) / 255.0;
        }
        return ink / (side * side);
    }

    void declaredCoverageMatchesTheArtwork() {
        // backgroundpresets.cpp asks whoever redraws a tile to keep `coverage` in step,
        // because the contrast test relies on it. That was a comment guarding a number
        // about a file in another directory, and it did not hold: weave was declared 0.22
        // against 0.28 of ink and linen 0.30 against 0.34 — both UNDER-stating the shift,
        // so the floors were being told the patterns were gentler than they are.
        // Reported together rather than one QVERIFY2 per pattern: if several have drifted
        // you want the whole list from one run, not to re-run six times.
        QStringList wrong;
        for (const auto& p : BackgroundPresets::patterns()) {
            const double measured = measuredCoverage(p);
            QVERIFY2(measured >= 0.0, qPrintable("could not render tile: " + p.asset));
            const double error = std::abs(measured - p.coverage) / measured;
            if (error > 0.15) {
                wrong << QString("%1: declares %2, artwork is %3 (%4% off)")
                             .arg(p.id).arg(p.coverage, 0, 'f', 3)
                             .arg(measured, 0, 'f', 3).arg(error * 100, 0, 'f', 0);
            }
        }
        QVERIFY2(wrong.isEmpty(),
                 qPrintable("coverage no longer matches the artwork — re-measure or redraw:\n  "
                            + wrong.join("\n  ")));
    }

    void patternsAreWellFormed() {
        const auto& table = BackgroundPresets::patterns();
        QVERIFY(!table.isEmpty());

        QSet<QString> ids;
        for (const auto& p : table) {
            // An empty id would be unfindable by patternById, unselectable via hasPattern,
            // and would still render as a blank untitled tile in the picker.
            QVERIFY2(!p.id.isEmpty(), "pattern id must not be empty");
            QVERIFY2(!ids.contains(p.id), qPrintable("duplicate pattern id: " + p.id));
            ids.insert(p.id);
            QVERIFY2(!p.nameKey.isEmpty(), qPrintable("missing nameKey: " + p.id));
            QVERIFY2(!p.nameFallback.isEmpty(), qPrintable("missing nameFallback: " + p.id));

            QString resourcePath = p.asset;
            QVERIFY2(resourcePath.startsWith("qrc:/"), qPrintable("asset is not a qrc URL: " + p.id));
            resourcePath.replace(0, 4, ":");
            QVERIFY2(QFile::exists(resourcePath),
                     qPrintable("pattern asset missing from resources: " + p.asset));

            QVERIFY2(p.opacity > 0.0 && p.opacity <= 0.25, qPrintable("opacity out of range: " + p.id));
            QVERIFY2(p.tile > 0, qPrintable("pattern without a tile size: " + p.id));
            QVERIFY2(p.coverage > 0.0 && p.coverage < 1.0, qPrintable("bad coverage: " + p.id));
        }
    }

    void coloursAreOrderedDarkToLight() {
        const auto& table = BackgroundPresets::colours();
        QVERIFY(lstar(QColor(table.first().value)) < lstar(QColor(table.last().value)));
    }

    void catalogueSpansTheUsableRange() {
        // The complaint that produced this catalogue was that every option looked the same.
        // Guard both ends AND the middle, so the set cannot silently collapse back into one
        // cluster of near-blacks or one of off-whites.
        double lo = 100.0, hi = 0.0;
        int mid = 0;
        for (const auto& c : BackgroundPresets::colours()) {
            const double l = lstar(QColor(c.value));
            lo = std::min(lo, l);
            hi = std::max(hi, l);
            if (l > 15.0 && l < 90.0) ++mid;
        }
        QVERIFY2(lo < 12.0, qPrintable(QString("no deep option: darkest is L* %1").arg(lo)));
        QVERIFY2(hi > 90.0, qPrintable(QString("no light option: lightest is L* %1").arg(hi)));
        QVERIFY2(mid >= 4, "catalogue collapses to the two extremes with nothing between");
    }

    void lookupOfUnknownIdIsEmpty() {
        QVERIFY(BackgroundPresets::colourById("no-such-colour").id.isEmpty());
        QVERIFY(BackgroundPresets::colourById("").id.isEmpty());
        QVERIFY(!BackgroundPresets::hasColour("no-such-colour"));
        QVERIFY(BackgroundPresets::hasColour("espresso"));
        QVERIFY(!BackgroundPresets::hasPattern("no-such-pattern"));
        QVERIFY(BackgroundPresets::colourToVariantMap(BackgroundPresets::colourById("nope")).isEmpty());
        QVERIFY(BackgroundPresets::patternToVariantMap(BackgroundPresets::patternById("nope")).isEmpty());
    }

    void variantListsCarryEveryEntry() {
        QCOMPARE(BackgroundPresets::coloursAsVariantList().size(), BackgroundPresets::colours().size());
        QCOMPARE(BackgroundPresets::patternsAsVariantList().size(), BackgroundPresets::patterns().size());
        QCOMPARE(BackgroundPresets::coloursAsVariantList().first().toMap().value("value").toString(),
                 BackgroundPresets::colours().first().value);
    }

    // --- Derived foreground ------------------------------------------------
    //
    // The catalogue spans near-black to near-white and every colour is offered under every
    // theme, so legibility cannot come from the palette — it is derived from the chosen
    // colour. These checks are only possible because a colour is known: over a photo none
    // of this is computable.

    void everyColourKeepsDerivedTextLegible() {
        // The densest pattern, since a pattern shifts the page toward the text colour and
        // so eats a little contrast. Weighted by coverage: a hairline moves the pixels it
        // covers a lot and the page as a whole very little, and the page is what text is
        // read against.
        double worstShift = 0.0;
        for (const auto& p : BackgroundPresets::patterns())
            worstShift = std::max(worstShift, BackgroundPresets::contrastShift(p));

        for (const auto& c : BackgroundPresets::colours()) {
            const QColor page(c.value);
            const QColor text = contrastColorFor(page);
            const QColor secondary = mix(text, page, 0.28);
            struct Case { QColor bg; const char* label; };
            const Case surfaces[] = {
                {page,                              "page"},
                {mix(page, text, worstShift),       "page under the densest pattern"},
                {liftFrom(page, kCardLift),         "card"},
                {liftFrom(page, kTileLift),         "action tile"},
            };
            for (const Case& s : surfaces) {
                for (auto [fg, what] : {std::pair{text, "text"}, std::pair{secondary, "secondary"}}) {
                    const double ratio = contrast(fg, s.bg);
                    QVERIFY2(ratio >= kMinContrast,
                             qPrintable(QString("%1: %2 on %3 = %4:1, below %5:1")
                                            .arg(c.id, QString::fromLatin1(what),
                                                 QString::fromLatin1(s.label))
                                            .arg(ratio, 0, 'f', 2).arg(kMinContrast)));
                }
            }
        }
    }

    void everyColourHasRoomForItsChrome() {
        // What this can and cannot prove. `liftFrom` BISECTS until the delta equals what it
        // was asked for, so asserting "the delta is at least 2 L*" would be a tautology over
        // this file's own helper — it could not fail for any catalogue, including one whose
        // colours sat in the dead band. That is what this test used to do.
        //
        // The property that can actually fail is CONVERGENCE: near either end of the range
        // the requested step may not be reachable in the direction chosen, and the bisection
        // then returns a colour short of it — or, at the very extremes, the page colour
        // itself, which paints chrome that is invisible with nothing to report it.
        for (const auto& c : BackgroundPresets::colours()) {
            const QColor page(c.value);
            for (double requested : {kCardLift, kTileLift}) {
                const QColor fill = liftFrom(page, requested);
                const double achieved = std::abs(lstar(fill) - lstar(page));
                QVERIFY2(std::abs(achieved - requested) < 0.5,
                         qPrintable(QString("%1: asked for %2 L* of lift, got %3 — no headroom "
                                            "in the direction chosen")
                                        .arg(c.id).arg(requested, 0, 'f', 1)
                                        .arg(achieved, 0, 'f', 2)));
                QVERIFY2(fill != page,
                         qPrintable(QString("%1: chrome resolved to the page colour itself")
                                        .arg(c.id)));
            }
        }
    }

    void aLiftWithNoHeadroomIsCaught() {
        // Guards the guard: pure white and pure black are the cases where one lift direction
        // is impossible, and they must still produce a visible step by going the other way.
        for (const QColor& extreme : {QColor("#ffffff"), QColor("#000000")}) {
            for (double requested : {kCardLift, kTileLift}) {
                const QColor fill = liftFrom(extreme, requested);
                const double achieved = std::abs(lstar(fill) - lstar(extreme));
                QVERIFY2(std::abs(achieved - requested) < 0.5,
                         qPrintable(QString("%1: asked for %2 L*, got %3")
                                        .arg(extreme.name()).arg(requested, 0, 'f', 1)
                                        .arg(achieved, 0, 'f', 2)));
            }
        }
    }

    // --- Settings ----------------------------------------------------------

    void defaultsToNoPreset() {
        SettingsTheme theme;
        QCOMPARE(theme.backgroundPreset(), QString());
        QVERIFY(theme.activeBackgroundPreset().isEmpty());
    }

    void selectionPersistsAndNotifies() {
        {
            SettingsTheme theme;
            QSignalSpy spy(&theme, &SettingsTheme::backgroundPresetChanged);
            theme.setBackgroundPreset("espresso");
            QCOMPARE(spy.count(), 1);
            QCOMPARE(theme.backgroundPreset(), QString("espresso"));
        }
        SettingsTheme reopened;  // fresh instance = same backing store, as after a restart
        QCOMPARE(reopened.backgroundPreset(), QString("espresso"));
        QCOMPARE(reopened.activeBackgroundPreset().value("id").toString(), QString("espresso"));
    }

    void unknownIdReadsBackAsNone() {
        QSettings raw(Settings::testQSettingsPath(), QSettings::IniFormat);
        raw.setValue("theme/backgroundPreset", "retired-in-a-later-release");
        raw.sync();

        SettingsTheme theme;
        QCOMPARE(theme.backgroundPreset(), QString());
        QVERIFY(theme.activeBackgroundPreset().isEmpty());
    }

    void presetAndImageAreMutuallyExclusive() {
        SettingsTheme theme;

        theme.setBackgroundImagePath("/tmp/some-photo.jpg");
        theme.setBackgroundPreset("cast-iron");
        QCOMPARE(theme.backgroundPreset(), QString("cast-iron"));
        QCOMPARE(theme.backgroundImagePath(), QString());

        theme.setBackgroundImagePath("/tmp/another-photo.jpg");
        QCOMPARE(theme.backgroundImagePath(), QString("/tmp/another-photo.jpg"));
        QCOMPARE(theme.backgroundPreset(), QString());
    }

    void applyingAThemeClearsThePreset() {
        SettingsTheme theme;
        theme.setBackgroundPreset("walnut");
        theme.applyDarkTheme("Default Dark");
        QCOMPARE(theme.backgroundPreset(), QString());

        theme.setBackgroundPreset("walnut");
        theme.applyLightTheme("Default Light");
        QCOMPARE(theme.backgroundPreset(), QString());
    }

    void editingBackgroundColourClearsThePreset() {
        SettingsTheme theme;
        theme.setBackgroundPreset("ristretto");
        theme.setEditingPaletteColor("backgroundColor", "#123456");
        QCOMPARE(theme.backgroundPreset(), QString());
    }

    void editingAnotherColourKeepsThePreset() {
        SettingsTheme theme;
        theme.setBackgroundPreset("ristretto");
        theme.setEditingPaletteColor("primaryColor", "#123456");
        QCOMPARE(theme.backgroundPreset(), QString("ristretto"));
    }

    void aPresetIsIndependentOfTheMode() {
        // Presets used to be dark/light pairs that resolved against isDarkMode. They are
        // not any more: one colour, offered under every theme, with the foreground derived
        // from it. A mode switch must change neither the selection nor the colour.
        SettingsTheme theme;
        theme.setThemeMode("dark");
        theme.setBackgroundPreset("porcelain");
        const QString beforeSwitch = theme.activeBackgroundPreset().value("value").toString();

        theme.setThemeMode("light");
        QCOMPARE(theme.backgroundPreset(), QString("porcelain"));
        QCOMPARE(theme.activeBackgroundPreset().value("value").toString(), beforeSwitch);
        QCOMPARE(beforeSwitch, BackgroundPresets::colourById("porcelain").value);
    }

    // --- Glass chrome option -----------------------------------------------

    void glassChromeDefaultsOffAndRoundTrips() {
        {
            SettingsTheme theme;
            QVERIFY(!theme.glassChrome());

            QSignalSpy spy(&theme, &SettingsTheme::glassChromeChanged);
            theme.setGlassChrome(true);
            QCOMPARE(spy.count(), 1);
            theme.setGlassChrome(true);  // idempotent
            QCOMPARE(spy.count(), 1);
        }
        SettingsTheme reopened;
        QVERIFY(reopened.glassChrome());
    }

    void glassChromeIsIndependentOfThemeAndBackground() {
        // The whole reason it stopped being a theme: it is orthogonal to light/dark and to
        // the background, so it must survive both changing underneath it.
        SettingsTheme theme;
        theme.setGlassChrome(true);

        theme.applyDarkTheme("Default Dark");
        QVERIFY2(theme.glassChrome(), "applying a theme must not clear the glass option");

        theme.setThemeMode("light");
        QVERIFY2(theme.glassChrome(), "a mode switch must not clear the glass option");

        theme.setBackgroundPreset("latte");
        QVERIFY2(theme.glassChrome(), "choosing a background must not clear the glass option");
    }

    void glassIsNoLongerATheme() {
        // It was one, briefly, and that was the wrong shape — a theme occupies a single
        // polarity slot, so it could only ever be half-applied.
        SettingsTheme theme;
        QVERIFY(!theme.themeNames().contains("Glass"));
    }
};

QTEST_MAIN(TestBackgroundPresets)
#include "tst_backgroundpresets.moc"
