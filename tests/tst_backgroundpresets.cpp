#include <QtTest>
#include <QColor>
#include <QFile>
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

    // What Theme.cardBackgroundColor actually paints over a flat page: surfaceColor at
    // backgroundScrimAlpha composited over the background. Keep the alpha in step with
    // Theme.qml's backgroundScrimAlpha.
    static constexpr double kScrimAlpha = 0.4;
    static constexpr double kMinContrast = 4.5;

    static QColor scrimOver(const QColor& surface, const QColor& base) {
        return QColor::fromRgbF(
            kScrimAlpha * surface.redF()   + (1.0 - kScrimAlpha) * base.redF(),
            kScrimAlpha * surface.greenF() + (1.0 - kScrimAlpha) * base.greenF(),
            kScrimAlpha * surface.blueF()  + (1.0 - kScrimAlpha) * base.blueF());
    }

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

    // Mirrors Theme._liftFrom: cards read as raised, which means lighter until there is no
    // headroom left above, at which point the step goes down instead.
    static QColor liftFrom(const QColor& base, double strength) {
        return mix(base, luminance(base) < 0.72 ? QColor("#ffffff") : QColor("#000000"), strength);
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

    void catalogueIsWellFormed() {
        const auto& presets = BackgroundPresets::catalogue();
        QCOMPARE(presets.size(), 20);

        QSet<QString> ids;
        for (const auto& p : presets) {
            QVERIFY2(!p.id.isEmpty(), "preset id must not be empty");
            QVERIFY2(!ids.contains(p.id), qPrintable("duplicate preset id: " + p.id));
            ids.insert(p.id);

            QVERIFY2(!p.nameKey.isEmpty(), qPrintable("missing nameKey: " + p.id));
            QVERIFY2(!p.nameFallback.isEmpty(), qPrintable("missing nameFallback: " + p.id));

            QVERIFY2(QColor::isValidColorName(p.color), qPrintable("bad colour: " + p.id));

            if (p.overlayKind == BackgroundPresets::OverlayKind::Tile) {
                QVERIFY2(!p.overlayAsset.isEmpty(), qPrintable("tile preset without asset: " + p.id));
                // "qrc:/x" -> ":/x", the form QFile understands.
                QString resourcePath = p.overlayAsset;
                QVERIFY2(resourcePath.startsWith("qrc:/"), qPrintable("asset is not a qrc URL: " + p.id));
                resourcePath.replace(0, 4, ":");
                QVERIFY2(QFile::exists(resourcePath),
                         qPrintable("overlay asset missing from resources: " + p.overlayAsset));
                QVERIFY2(p.overlayOpacity > 0.0 && p.overlayOpacity <= 0.2,
                         qPrintable("overlay opacity out of range: " + p.id));
                QVERIFY2(p.overlayTile > 0, qPrintable("tile preset without a tile size: " + p.id));
            } else {
                QVERIFY2(p.overlayAsset.isEmpty(), qPrintable("solid preset with an asset: " + p.id));
                QCOMPARE(p.overlayOpacity, 0.0);
            }
        }
    }

    void catalogueIsOrderedDarkToLight() {
        // The chooser reads as a ramp rather than a jumble, and — more importantly — a
        // user scanning it sees the range on offer instead of ten near-identical squares.
        const auto& presets = BackgroundPresets::catalogue();
        QVERIFY(lstar(QColor(presets.first().color)) < lstar(QColor(presets.last().color)));
    }

    void lookupOfUnknownIdIsEmpty() {
        QVERIFY(BackgroundPresets::byId("no-such-preset").id.isEmpty());
        QVERIFY(BackgroundPresets::byId("").id.isEmpty());
        QVERIFY(!BackgroundPresets::contains("no-such-preset"));
        QVERIFY(BackgroundPresets::contains("graphite"));
        QVERIFY(BackgroundPresets::toVariantMap(BackgroundPresets::byId("nope")).isEmpty());
    }

    void variantListCarriesEveryEntry() {
        const QVariantList list = BackgroundPresets::toVariantList();
        QCOMPARE(list.size(), BackgroundPresets::catalogue().size());
        QCOMPARE(list.first().toMap().value("color").toString(),
                 BackgroundPresets::catalogue().first().color);
    }

    // --- Derived foreground ------------------------------------------------
    //
    // The catalogue spans near-black to near-white and every entry is offered under every
    // theme, so legibility cannot come from the palette — it is derived from the preset
    // colour. These are the checks that make that safe, and they are only possible because
    // a preset is a known colour: over a photo none of this is computable.

    void everyPresetKeepsDerivedTextLegible() {
        for (const auto& p : BackgroundPresets::catalogue()) {
            const QColor page(p.color);
            const QColor text = contrastColorFor(page);
            const QColor secondary = mix(text, page, 0.28);

            // Both card strengths Theme.qml uses: 0.05 with glass chrome on, 0.09 off.
            for (double strength : {0.05, 0.09}) {
                const QColor card = liftFrom(page, strength);
                struct Case { QColor fg; QColor bg; const char* label; };
                const Case cases[] = {
                    {text,      page, "text on page"},
                    {secondary, page, "secondary text on page"},
                    {text,      card, "text on card"},
                    {secondary, card, "secondary text on card"},
                };
                for (const Case& c : cases) {
                    const double ratio = contrast(c.fg, c.bg);
                    QVERIFY2(ratio >= kMinContrast,
                             qPrintable(QString("preset %1 (card %2): %3 = %4:1, below %5:1")
                                            .arg(p.id).arg(strength)
                                            .arg(QString::fromLatin1(c.label))
                                            .arg(ratio, 0, 'f', 2).arg(kMinContrast)));
                }
            }
        }
    }

    void everyPresetKeepsCardsVisible() {
        // A card has to differ from the page or the layout dissolves. Measured as a CIE L*
        // delta, NOT a WCAG ratio: ratios are built for text and compress badly in the
        // bright range, so one ratio threshold would pass an invisible light card or fail a
        // perfectly good dark one. L* is perceptually uniform, so one threshold means the
        // same thing at both ends of the ramp — which this catalogue spans.
        for (const auto& p : BackgroundPresets::catalogue()) {
            const QColor page(p.color);
            for (double strength : {0.05, 0.09}) {
                const double delta = std::abs(lstar(liftFrom(page, strength)) - lstar(page));
                QVERIFY2(delta >= 2.0,
                         qPrintable(QString("preset %1: card is only %2 L* from the page at "
                                            "strength %3 — cards will read as invisible")
                                        .arg(p.id).arg(delta, 0, 'f', 2).arg(strength)));
            }
        }
    }

    void catalogueSpansDarkToLight() {
        // The point of the redesign: in any one mode the user must see more than one end of
        // the range. The previous mode-paired design offered only near-blacks in dark mode.
        double lo = 100.0, hi = 0.0;
        for (const auto& p : BackgroundPresets::catalogue()) {
            const double l = lstar(QColor(p.color));
            lo = std::min(lo, l);
            hi = std::max(hi, l);
        }
        QVERIFY2(lo < 15.0, qPrintable(QString("no deep option: darkest is L* %1").arg(lo)));
        QVERIFY2(hi > 85.0, qPrintable(QString("no light option: lightest is L* %1").arg(hi)));

        // And something in between, or the ramp is just two clusters.
        int mid = 0;
        for (const auto& p : BackgroundPresets::catalogue()) {
            const double l = lstar(QColor(p.color));
            if (l > 20.0 && l < 80.0)
                ++mid;
        }
        QVERIFY2(mid >= 2, "catalogue has no mid-tone options");
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
        theme.setBackgroundPreset("slate");
        QCOMPARE(theme.backgroundPreset(), QString("slate"));
        QCOMPARE(theme.backgroundImagePath(), QString());

        theme.setBackgroundImagePath("/tmp/another-photo.jpg");
        QCOMPARE(theme.backgroundImagePath(), QString("/tmp/another-photo.jpg"));
        QCOMPARE(theme.backgroundPreset(), QString());
    }

    void applyingAThemeClearsThePreset() {
        SettingsTheme theme;
        theme.setBackgroundPreset("forest");
        theme.applyDarkTheme("Default Dark");
        QCOMPARE(theme.backgroundPreset(), QString());

        theme.setBackgroundPreset("forest");
        theme.applyLightTheme("Default Light");
        QCOMPARE(theme.backgroundPreset(), QString());
    }

    void editingBackgroundColourClearsThePreset() {
        SettingsTheme theme;
        theme.setBackgroundPreset("plum");
        theme.setEditingPaletteColor("backgroundColor", "#123456");
        QCOMPARE(theme.backgroundPreset(), QString());
    }

    void editingAnotherColourKeepsThePreset() {
        SettingsTheme theme;
        theme.setBackgroundPreset("plum");
        theme.setEditingPaletteColor("primaryColor", "#123456");
        QCOMPARE(theme.backgroundPreset(), QString("plum"));
    }

    void aPresetIsIndependentOfTheMode() {
        // Presets used to be dark/light pairs that resolved against isDarkMode. They are
        // not any more: one colour, offered under every theme, with the foreground derived
        // from it. A mode switch must change neither the selection nor the colour.
        SettingsTheme theme;
        theme.setThemeMode("dark");
        theme.setBackgroundPreset("chalk");
        const QString beforeSwitch = theme.activeBackgroundPreset().value("color").toString();

        theme.setThemeMode("light");
        QCOMPARE(theme.backgroundPreset(), QString("chalk"));
        QCOMPARE(theme.activeBackgroundPreset().value("color").toString(), beforeSwitch);
        QCOMPARE(beforeSwitch, BackgroundPresets::byId("chalk").color);
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

        theme.setBackgroundPreset("cream");
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
