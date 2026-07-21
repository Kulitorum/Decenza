#include <QtTest>
#include <QColor>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QRegularExpression>
#include <QSvgRenderer>
#include <QSet>
#include <QSignalSpy>

#include "core/backgroundpresets.h"
#include "core/settings.h"
#include "core/settings_theme.h"
#include "core/settings.h"
#include "core/settingsserializer.h"

// Background presets + the glass-chrome option.
//
// The contrast checks here are the point of putting the catalogue in C++ at all: over a
// screensaver photo what sits behind a translucent card is unknowable, so the scrim alpha
// was tuned by eye. A preset is a KNOWN colour, so everything derived from it — text,
// secondary text, card fill — is exact arithmetic, and therefore testable rather than
// merely intended. That derivation is what allows every colour, from near-black to
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

    // NOTE: there is deliberately no local liftFrom/derive here any more. The floors call
    // BackgroundPresets directly, so they measure the shipped arithmetic. A private copy
    // made the suite agree with itself while the app shipped something else.

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
            // The SHIPPED derivation, not a local copy of the arithmetic. Re-deriving here
            // meant the floors measured this file rather than the app: change kCardLift or
            // kSecondaryMix in backgroundpresets.cpp and every colour could ship failing
            // 4.5:1 while the suite stayed green on its own constants.
            const BackgroundPresets::Derived d = BackgroundPresets::derive(page);
            const QColor text = d.text;
            const QColor secondary = d.textSecondary;
            struct Case { QColor bg; const char* label; };
            const Case surfaces[] = {
                {page,                              "page"},
                {mix(page, text, worstShift),       "page under the densest pattern"},
                {d.surface,                         "card"},
                {d.actionTile,                      "action tile"},
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
            for (double requested : {BackgroundPresets::kCardLift, BackgroundPresets::kTileLift}) {
                const QColor fill = BackgroundPresets::liftFrom(page, requested);
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

    // The semantic palette Theme runs through BackgroundPresets::adjustForContrast. These
    // are the shipped defaults from Theme.qml; a user palette can override them, which is
    // exactly why the adjustment is a function of the colour rather than a second table.
    static QVector<QPair<QString, QColor>> semanticPalette() {
        return {{"primary", QColor("#4e85f4")},
                {"accent",  QColor("#e94560")},
                {"success", QColor("#00cc6d")},
                {"warning", QColor("#ffaa00")},
                {"error",   QColor("#ff4444")}};
    }

    void everySemanticColourStaysReadableOnEveryPage() {
        // The composition SettingsTheme::adjustedForContrast performs: size the colour
        // against the page as the densest pattern renders it, then require it to hold on
        // the bare page too. Sizing against the BARE page is what this originally did, and
        // it failed exactly once — accent on French Roast, 4.65:1 bare and 4.19:1 under
        // Linen — which is the case pageUnderDensestPattern() exists for.
        for (const auto& c : BackgroundPresets::colours()) {
            const QColor page(c.value);
            const QColor patterned = BackgroundPresets::pageUnderDensestPattern(page);
            for (const auto& [name, base] : semanticPalette()) {
                const QColor fixed = BackgroundPresets::adjustForContrast(base, patterned);
                for (auto [bg, what] : {std::pair{page, "page"},
                                        std::pair{patterned, "patterned page"}}) {
                    const double ratio = contrast(fixed, bg);
                    QVERIFY2(ratio >= kMinContrast - 0.05,
                             qPrintable(QString("%1: %2 %3 on %4 = %5:1, below %6:1")
                                            .arg(c.id, name, fixed.name(),
                                                 QString::fromLatin1(what))
                                            .arg(ratio, 0, 'f', 2).arg(kMinContrast)));
                }
            }
        }
    }

    void aSemanticColourThatAlreadyClearsTheFloorIsUntouched() {
        // The dark end of the catalogue is where these colours were authored to work. If the
        // adjustment fired there it would be repainting a palette that is already correct —
        // and every existing dark theme in the app would shift under it.
        int untouched = 0;
        for (const auto& c : BackgroundPresets::colours()) {
            const QColor page(c.value);
            for (const auto& [name, base] : semanticPalette()) {
                const QColor patterned = BackgroundPresets::pageUnderDensestPattern(page);
                if (contrast(base, patterned) < kMinContrast)
                    continue;
                QCOMPARE(BackgroundPresets::adjustForContrast(base, patterned), base);
                ++untouched;
            }
        }
        QVERIFY2(untouched > 0, "no colour left the palette alone — the no-op path is untested");
    }

    void theAdjustmentKeepsTheHue() {
        // The whole reason these five are not simply derived from the page: the hue IS the
        // meaning. A warning that clears 4.5:1 by turning black has been made readable and
        // useless in the same step.
        for (const auto& c : BackgroundPresets::colours()) {
            const QColor page(c.value);
            for (const auto& [name, base] : semanticPalette()) {
                const QColor fixed = BackgroundPresets::adjustForContrast(
                    base, BackgroundPresets::pageUnderDensestPattern(page));
                if (fixed == base)
                    continue;
                const int drift = std::abs(fixed.hslHue() - base.hslHue());
                QVERIFY2(std::min(drift, 360 - drift) <= 3,
                         qPrintable(QString("%1: %2 shifted hue %3 -> %4 (%5 -> %6)")
                                        .arg(c.id, name)
                                        .arg(base.hslHue()).arg(fixed.hslHue())
                                        .arg(base.name(), fixed.name())));
                QVERIFY2(fixed.hslSaturation() > 60,
                         qPrintable(QString("%1: %2 washed out to %3")
                                        .arg(c.id, name, fixed.name())));
            }
        }
    }

    void theAdjustmentTakesTheSmallestStepThatWorks() {
        // Bisecting for the smallest step is what keeps "a deeper amber" from becoming
        // "black": one notch less adjustment must fail the floor it just cleared.
        const QColor page("#b9a184");  // Cortado — the tightest light colour in the table
        for (const auto& [name, base] : semanticPalette()) {
            const QColor fixed = BackgroundPresets::adjustForContrast(base, page);
            QVERIFY(contrast(fixed, page) >= kMinContrast - 0.05);
            const QColor lessAdjusted = mix(base, fixed, 0.9);
            QVERIFY2(contrast(lessAdjusted, page) < kMinContrast,
                     qPrintable(QString("%1 overshot: 90%% of the step already clears the floor")
                                    .arg(name)));
        }
    }

    void aLiftWithNoHeadroomIsCaught() {
        // Guards the guard: pure white and pure black are the cases where one lift direction
        // is impossible, and they must still produce a visible step by going the other way.
        for (const QColor& extreme : {QColor("#ffffff"), QColor("#000000")}) {
            for (double requested : {BackgroundPresets::kCardLift, BackgroundPresets::kTileLift}) {
                const QColor fill = BackgroundPresets::liftFrom(extreme, requested);
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

        // The read path now says so out loud — upgrading past a removed colour used to make
        // the background vanish with nothing in the log to connect the two. Expected here,
        // and asserted rather than merely tolerated: if the warning stops firing, the
        // diagnosis it exists to provide has gone with it.
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("not in this build's catalogue"));

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

    // --- The background source ---------------------------------------------
    //
    // Three kinds mean six ordered pairs, and the failure this guards is two of them live
    // at once — whereupon whichever renderer tests first decides what you see. Driving all
    // six is the point: the pairwise clearing these replaced was correct for the two pairs
    // it was written for and had no opinion about the other four.

    // Select a source by kind, so the exclusivity table below reads as a table.
    static void selectSource(SettingsTheme& theme, const QString& kind) {
        if (kind == "colour")      theme.setBackgroundPreset("cast-iron");
        else if (kind == "image")  theme.setBackgroundImagePath("/tmp/a-photo.jpg");
        else if (kind == "shot")   theme.selectShotChartBackground(false);
        else                       theme.clearBackground();
    }

    void everySourceReplacesEveryOther() {
        const QStringList kinds = {"colour", "image", "shot"};
        for (const QString& first : kinds) {
            for (const QString& second : kinds) {
                if (first == second)
                    continue;
                SettingsTheme theme;
                selectSource(theme, first);
                QCOMPARE(theme.backgroundSource(), first);

                selectSource(theme, second);
                QVERIFY2(theme.backgroundSource() == second,
                         qPrintable(QString("%1 -> %2 left the source at %3")
                                        .arg(first, second, theme.backgroundSource())));

                // The parameters of the sources NOT selected must be empty, or a renderer
                // testing them in a different order draws something else entirely.
                if (second != "colour")
                    QVERIFY2(theme.backgroundPreset().isEmpty(),
                             qPrintable(QString("%1 -> %2 left a colour set").arg(first, second)));
                if (second != "image")
                    QVERIFY2(theme.backgroundImagePath().isEmpty(),
                             qPrintable(QString("%1 -> %2 left an image path set").arg(first, second)));

                // The STORED key, not just the derived getter. backgroundSource() falls back
                // to deriving the kind from whichever parameter is set, which is right for
                // migration and self-healing but means it reconstructs the correct answer
                // even when nothing wrote the key at all. Asserting only the getter, this
                // test passed with every source write deleted.
                QSettings raw(Settings::testQSettingsPath(), QSettings::IniFormat);
                QCOMPARE(raw.value("theme/backgroundSource").toString(), second);
            }
        }
    }

    void clearingReturnsToNone() {
        for (const QString& kind : {QString("colour"), QString("image"), QString("shot")}) {
            SettingsTheme theme;
            selectSource(theme, kind);
            theme.clearBackground();
            QCOMPARE(theme.backgroundSource(), QString("none"));
            QVERIFY(theme.backgroundPreset().isEmpty());
            QVERIFY(theme.backgroundImagePath().isEmpty());
        }
    }

    void clearingOneSourceDoesNotDisturbAnother() {
        // Choosing an image clears the colour as a SIDE EFFECT, and that clear arrives
        // after the source is already "image". If releasing a source were unconditional
        // rather than "only if it is still mine", this is where it would stomp.
        SettingsTheme theme;
        theme.setBackgroundPreset("cortado");
        theme.setBackgroundImagePath("/tmp/a-photo.jpg");
        QCOMPARE(theme.backgroundSource(), QString("image"));

        theme.selectShotChartBackground(true);
        QCOMPARE(theme.backgroundSource(), QString("shot"));
    }

    // --- The source has to NOTIFY, not just hold the right value -----------
    //
    // Theme.hasBackgroundImage and Theme.glassChrome read backgroundSource, and ~70 chrome
    // call sites read those. A correct value that never announces itself leaves every one
    // of them latched on the previous answer until the app restarts.

    void clearingTheImageTellsTheAppTheBackgroundIsGone() {
        // The path ScreensaverVideoManager takes when it deletes the file a background was
        // using. Without a notify the whole app stays in translucent-over-a-photo mode with
        // no photo — which is the exact state that clearing code exists to prevent.
        SettingsTheme theme;
        theme.setBackgroundImagePath("/tmp/photo.jpg");
        QSignalSpy spy(&theme, &SettingsTheme::backgroundSourceChanged);

        theme.setBackgroundImagePath(QString());

        QCOMPARE(theme.backgroundSource(), QString("none"));
        QVERIFY2(spy.count() >= 1,
                 "no backgroundSourceChanged on clear — every binding stays on \"image\"");
    }

    void everyClearLeavesBindingsOnTheFinalValue() {
        // A QML binding re-reads the property INSIDE the change handler, so emitting before
        // the parameters are cleared hands every binding the value being cleared.
        for (const QString& kind : {QString("colour"), QString("image"), QString("shot")}) {
            SettingsTheme theme;
            selectSource(theme, kind);

            QString atEmit;
            QObject::connect(&theme, &SettingsTheme::backgroundSourceChanged,
                             &theme, [&]{ atEmit = theme.backgroundSource(); });

            theme.clearBackground();

            QCOMPARE(theme.backgroundSource(), QString("none"));
            QVERIFY2(atEmit == QString("none"),
                     qPrintable(QString("clearing from %1 announced %2").arg(kind, atEmit)));
        }
    }

    void flippingAdvancedWhileAlreadyOnTheChartStillNotifies() {
        // The source does not move here, so the only thing that can tell the renderer to
        // re-draw is this signal. Without it the Advanced entry is a permanent no-op that
        // still shows as selected in the picker.
        SettingsTheme theme;
        theme.selectShotChartBackground(false);
        QSignalSpy spy(&theme, &SettingsTheme::backgroundSourceChanged);

        theme.selectShotChartBackground(true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(theme.backgroundShotAdvanced(), true);

        // ...and re-selecting the same entry must NOT, or every apply costs a re-render.
        theme.selectShotChartBackground(true);
        QCOMPARE(spy.count(), 1);
    }

    void theShotSelectionSurvivesARestart() {
        // The most visible way this feature can fail: it works all session and is gone on
        // the next launch. A second SettingsTheme over the same store is that restart.
        {
            SettingsTheme theme;
            theme.selectShotChartBackground(true);
        }
        SettingsTheme reopened;
        QCOMPARE(reopened.backgroundSource(), QString("shot"));
        QCOMPARE(reopened.backgroundShotAdvanced(), true);
    }

    void choosingTheShotChartKeepsThePattern() {
        // Documented in settings_theme.h — the pattern is retained so returning to a colour
        // restores it. Nothing pinned it, and losing a user's texture silently is exactly
        // what happened once already when the preset was split into two axes.
        SettingsTheme theme;
        theme.setBackgroundPreset("cortado");
        theme.setBackgroundPattern("linen");

        theme.selectShotChartBackground(false);
        QCOMPARE(theme.backgroundPattern(), QString("linen"));

        theme.setBackgroundPreset("cortado");
        QCOMPARE(theme.backgroundPattern(), QString("linen"));
    }

    void theShotEntryCarriesItsOwnAdvancedFlag() {
        SettingsTheme theme;
        theme.selectShotChartBackground(false);
        QCOMPARE(theme.backgroundShotAdvanced(), false);
        theme.selectShotChartBackground(true);
        QCOMPARE(theme.backgroundSource(), QString("shot"));
        QCOMPARE(theme.backgroundShotAdvanced(), true);
    }

    void anInstallPredatingTheSourceKeepsItsBackground() {
        // The migration: no stored source at all, which is every existing install. The
        // kind is derived from the values that ARE set, and nothing is rewritten.
        {
            SettingsTheme theme;
            theme.setBackgroundPreset("walnut");
            QSettings raw(Settings::testQSettingsPath(), QSettings::IniFormat);
            raw.remove("theme/backgroundSource");
            raw.sync();
            QCOMPARE(theme.backgroundSource(), QString("colour"));
        }
        {
            SettingsTheme theme;
            theme.setBackgroundImagePath("/tmp/legacy.jpg");
            QSettings raw(Settings::testQSettingsPath(), QSettings::IniFormat);
            raw.remove("theme/backgroundSource");
            raw.sync();
            QCOMPARE(theme.backgroundSource(), QString("image"));
        }
        {
            // The blocks share one store and init() only runs per test FUNCTION, so the
            // image above is still set unless it is cleared — an install with no background
            // at all has to start from an empty store to mean anything.
            QSettings raw(Settings::testQSettingsPath(), QSettings::IniFormat);
            raw.remove("theme");
            raw.sync();
            SettingsTheme theme;
            QCOMPARE(theme.backgroundSource(), QString("none"));
        }
    }

    void aStoredSourceItsParameterCannotBackIsNotBelieved() {
        // "colour" with no colour describes something no renderer can draw. A hand-edited
        // ini, a downgrade, or a colour removed in a later release all reach this. Falling
        // through to the derivation is the same code path as the migration, deliberately.
        SettingsTheme theme;
        theme.setBackgroundImagePath("/tmp/real.jpg");
        QSettings raw(Settings::testQSettingsPath(), QSettings::IniFormat);
        raw.setValue("theme/backgroundSource", "colour");
        raw.sync();
        QCOMPARE(theme.backgroundSource(), QString("image"));
    }

    // Named for what it actually checks. The private setter's refusal branch is only
    // reachable from internal callers passing compile-time constants, so the reachable
    // hazard is a BAD STORED value — a hand-edited ini, or a downgrade — and what matters
    // is that reading one does not erase the background the user still has.
    void anUnknownStoredSourceIsDerivedAroundRatherThanErasingTheBackground() {
        SettingsTheme theme;
        theme.setBackgroundPreset("espresso");
        QSettings raw(Settings::testQSettingsPath(), QSettings::IniFormat);
        raw.setValue("theme/backgroundSource", "hologram");
        raw.sync();
        // Unknown reads back as the derived kind, and the colour itself is untouched.
        QCOMPARE(theme.backgroundSource(), QString("colour"));
        QCOMPARE(theme.backgroundPreset(), QString("espresso"));
    }

    void everyStoredSourceItsParameterCannotBackFallsThrough() {
        // One case was covered ("colour" with no colour). These are the rest, including the
        // one that does NOT fall through: "shot" carries no parameter, so it is believed as
        // stored, and that asymmetry should be deliberate rather than discovered.
        struct Case { const char* stored; const char* preset; const char* image; const char* expect; };
        const Case cases[] = {
            {"image",  "",        "",              "none"},    // no path to back it
            {"none",   "walnut",  "",              "colour"},  // a colour IS set
            {"none",   "",        "/tmp/a.jpg",    "image"},   // a path IS set
            {"shot",   "",        "",              "shot"},    // no parameter needed
        };
        for (const Case& c : cases) {
            QSettings raw(Settings::testQSettingsPath(), QSettings::IniFormat);
            raw.remove("theme");
            raw.setValue("theme/backgroundSource", c.stored);
            if (*c.preset) raw.setValue("theme/backgroundPreset", c.preset);
            if (*c.image)  raw.setValue("theme/backgroundImagePath", c.image);
            raw.sync();

            SettingsTheme theme;
            QVERIFY2(theme.backgroundSource() == QString(c.expect),
                     qPrintable(QString("stored %1 (preset '%2', image '%3') derived %4, expected %5")
                                    .arg(c.stored, c.preset, c.image, theme.backgroundSource(), c.expect)));
        }
    }

    void theShotSourceSurvivesABackup() {
        // A shot-chart background has no parameter of its own, so it is the one source a
        // backup can lose completely while reporting success — exactly how the pattern was
        // lost when the single preset became two axes.
        Settings settings;
        settings.theme()->selectShotChartBackground(true);

        const QJsonObject exported = SettingsSerializer::exportToJson(&settings, false);

        settings.theme()->setBackgroundPreset("porcelain");
        QCOMPARE(settings.theme()->backgroundSource(), QString("colour"));

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("importFromJson replacing .* favorites"));
        SettingsSerializer::importFromJson(&settings, exported);

        QCOMPARE(settings.theme()->backgroundSource(), QString("shot"));
        QCOMPARE(settings.theme()->backgroundShotAdvanced(), true);
        QVERIFY(settings.theme()->backgroundPreset().isEmpty());
    }

    // --- The shot-chart background's cache key -----------------------------
    //
    // The key itself lives in QML (LastShotChartSource) and this suite is C++ with no Qt
    // Quick Test harness, so the key's ARITHMETIC is verified live rather than here. What
    // is testable, and what actually breaks, is its COMPLETENESS: the key lists the graph
    // visibility settings by name, and a curve added to the chart later without a matching
    // entry produces a background that silently stops updating when you toggle it. Nothing
    // about that looks broken — the old chart is still a real chart.
    //
    // So compare the two lists in the two files directly. A source-consistency check rather
    // than a behaviour one, in the same spirit as declaredCoverageMatchesTheArtwork above.

    static QStringList graphKeysIn(const QString& relativePath, const QRegularExpression& pattern) {
        QFile file(QStringLiteral(DECENZA_SOURCE_DIR "/") + relativePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return {};
        QString text = QString::fromUtf8(file.readAll());
        // Drop // comments first. Without this, a key commented out while debugging still
        // counts as covered — the cache key silently loses a curve and the test stays green,
        // which is the exact failure this test exists to prevent.
        text.remove(QRegularExpression(QStringLiteral("//[^\n]*")));
        QStringList keys;
        auto it = pattern.globalMatch(text);
        while (it.hasNext()) {
            const QString key = it.next().captured(1);
            if (!keys.contains(key))
                keys << key;
        }
        keys.sort();
        return keys;
    }

    void theShotBackgroundKeyCoversEveryCurveTheChartDraws() {
        // Every graph/show* the chart reads...
        const QStringList drawn = graphKeysIn(
            "qml/components/HistoryShotGraph.qml",
            QRegularExpression(R"RX(Settings\.boolValue\("(graph/[^"]+)")RX"));
        QVERIFY2(drawn.size() >= 10,
                 qPrintable(QString("only found %1 curve settings in HistoryShotGraph — the "
                                    "pattern that finds them has probably gone stale")
                                .arg(drawn.size())));

        // ...must appear in the key the cached render is invalidated on.
        const QStringList keyed = graphKeysIn(
            "qml/components/LastShotChartSource.qml",
            QRegularExpression(R"RX("(graph/[^"]+)")RX"));

        QStringList missing;
        for (const QString& k : drawn) {
            if (!keyed.contains(k))
                missing << k;
        }
        QVERIFY2(missing.isEmpty(),
                 qPrintable(QString("LastShotChartSource's cache key does not mention %1 — "
                                    "toggling %2 would leave the background showing the "
                                    "previous render, with nothing to indicate it is stale")
                                .arg(missing.join(", "),
                                     missing.size() == 1 ? missing.first()
                                                         : QStringLiteral("one of them"))));

        // And nothing keyed that the chart does not actually draw: a stale entry here is a
        // re-render triggered by a setting with no effect on the picture.
        QStringList extra;
        for (const QString& k : keyed) {
            if (!drawn.contains(k))
                extra << k;
        }
        QVERIFY2(extra.isEmpty(),
                 qPrintable("cache key mentions settings the chart does not read: "
                            + extra.join(", ")));
    }

    void changingTheActiveThemeClearsTheColour() {
        // A theme carries its own background colour, so choosing one is an explicit choice
        // of the value a background colour overrides.
        SettingsTheme theme;
        theme.setThemeMode("dark");
        theme.setBackgroundPreset("walnut");
        theme.applyDarkTheme("Default Light");   // a real change to the active slot
        QCOMPARE(theme.backgroundPreset(), QString());
    }

    void reSelectingTheSameThemeKeepsTheColour() {
        // A combo box emits activated() when you re-pick the entry already selected. Left
        // unguarded, opening the dropdown to see what was set and tapping it destroyed the
        // user's background having changed nothing at all.
        SettingsTheme theme;
        theme.setThemeMode("dark");
        theme.applyDarkTheme("Default Dark");
        theme.setBackgroundPreset("walnut");

        theme.applyDarkTheme("Default Dark");    // same value again
        QCOMPARE(theme.backgroundPreset(), QString("walnut"));
    }

    void changingTheOtherPolaritysThemeKeepsTheColour() {
        // The colour is ONE global value; the theme slots are per-polarity. Changing the
        // light theme while looking at dark mode must not pull the background out from
        // under the page on screen.
        SettingsTheme theme;
        theme.setThemeMode("dark");
        theme.setBackgroundPreset("walnut");

        theme.applyLightTheme("Default Dark");   // a real change, but to the other slot
        QCOMPARE(theme.backgroundPreset(), QString("walnut"));
    }

    void editingTheOtherPalettesBackgroundKeepsTheColour() {
        // Same reasoning for the theme editor, which can edit the inactive palette.
        SettingsTheme theme;
        theme.setThemeMode("dark");
        theme.setBackgroundPreset("walnut");
        theme.setEditingPalette("light");

        theme.setEditingPaletteColor("backgroundColor", "#123456");
        QCOMPARE(theme.backgroundPreset(), QString("walnut"));
    }

    void anUnknownIdIsRefusedRatherThanErasingTheStoredOne() {
        // A restore naming a colour this build does not know must not wipe the colour the
        // device already has, then report success.
        SettingsTheme theme;
        theme.setBackgroundPreset("walnut");
        // The refusal is logged, and asserting that here also pins that it is not silent:
        // a background that reverts on restore is undiagnosable without this line.
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("Ignoring unknown background colour id"));
        theme.setBackgroundPreset("retired-in-a-later-release");
        QCOMPARE(theme.backgroundPreset(), QString("walnut"));

        theme.setBackgroundPattern("linen");
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("Ignoring unknown background pattern id"));
        theme.setBackgroundPattern("no-such-pattern");
        QCOMPARE(theme.backgroundPattern(), QString("linen"));

        // Empty is still how a caller asks for none.
        theme.setBackgroundPreset(QString());
        QCOMPARE(theme.backgroundPreset(), QString());
    }

    void thePatternSettingBehavesLikeTheColour() {
        {
            SettingsTheme theme;
            QCOMPARE(theme.backgroundPattern(), QString());
            QVERIFY(theme.activeBackgroundPattern().isEmpty());

            QSignalSpy spy(&theme, &SettingsTheme::backgroundPatternChanged);
            theme.setBackgroundPattern("twill");
            QCOMPARE(spy.count(), 1);
            theme.setBackgroundPattern("twill");   // idempotent
            QCOMPARE(spy.count(), 1);
        }
        SettingsTheme reopened;
        QCOMPARE(reopened.backgroundPattern(), QString("twill"));
        QCOMPARE(reopened.activeBackgroundPattern().value("id").toString(), QString("twill"));
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

    void colourAndPatternSurviveABackup() {
        // exportToJson/importFromJson back every persistence surface — local backup,
        // device-to-device migration and the web restore. The pattern was omitted when the
        // single preset was split into two axes, so a migration restored the colour and
        // silently dropped its texture while reporting success.
        Settings settings;
        settings.theme()->setBackgroundPreset("espresso");
        settings.theme()->setBackgroundPattern("linen");
        settings.theme()->setGlassChrome(true);

        const QJsonObject exported = SettingsSerializer::exportToJson(&settings, false);

        settings.theme()->setBackgroundPreset("porcelain");
        settings.theme()->setBackgroundPattern(QString());
        settings.theme()->setGlassChrome(false);

        // Unrelated to this test: the serializer warns whenever an import replaces the
        // favourites list, which every import does.
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("importFromJson replacing .* favorites"));
        SettingsSerializer::importFromJson(&settings, exported);

        QCOMPARE(settings.theme()->backgroundPreset(), QString("espresso"));
        QCOMPARE(settings.theme()->backgroundPattern(), QString("linen"));
        QVERIFY(settings.theme()->glassChrome());
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
