#include <QtTest>
#include <QColor>
#include <QFile>
#include <QSet>
#include <QSignalSpy>

#include "core/backgroundpresets.h"
#include "core/settings.h"
#include "core/settings_theme.h"

// Background presets + the built-in Glass theme.
//
// The contrast checks here are the point of putting the catalogue in C++ at all: over a
// screensaver photo what sits behind a translucent card is unknowable, so the scrim alpha
// was tuned by eye. Over a preset (or Glass with no background) the base is a known hex
// value, so the colour text actually lands on is exact arithmetic — and therefore
// testable rather than merely intended.
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

    // Theme.textSecondaryColor pushes AWAY from the page when the glass chrome is on:
    // lighter in dark mode, darker in light mode. Qt::lighter/darker take a percentage.
    static QColor adjustedSecondary(const QColor& secondary, bool darkMode) {
        return darkMode ? secondary.lighter(140) : secondary.darker(140);
    }

    // Perceptual lightness (CIE L*, 0-100). Used where a WCAG ratio is the wrong tool —
    // see separationIsPerceptible.
    static double lstar(const QColor& c) {
        const double y = luminance(c);
        return y <= 0.008856 ? 903.3 * y : 116.0 * std::cbrt(y) - 16.0;
    }

    // Every text colour that has to stay readable on `base`, for one mode.
    static void checkTextOn(const QColor& base, bool darkMode, const QString& what,
                            const QVariantMap& palette) {
        const QColor text(palette.value("textColor").toString());
        const QColor surface(palette.value("surfaceColor").toString());
        const QColor secondary = adjustedSecondary(
            QColor(palette.value("textSecondaryColor").toString()), darkMode);

        const QColor card = scrimOver(surface, base);

        struct Case { QColor fg; QColor bg; const char* label; };
        const Case cases[] = {
            {text,      base, "textColor on background"},
            {secondary, base, "textSecondaryColor on background"},
            {text,      card, "textColor on scrimmed card"},
            {secondary, card, "textSecondaryColor on scrimmed card"},
        };

        for (const Case& c : cases) {
            const double ratio = contrast(c.fg, c.bg);
            QVERIFY2(ratio >= kMinContrast,
                     qPrintable(QString("%1 (%2 mode): %3 = %4:1, below %5:1")
                                    .arg(what, darkMode ? "dark" : "light",
                                         QString::fromLatin1(c.label))
                                    .arg(ratio, 0, 'f', 2)
                                    .arg(kMinContrast)));
        }
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
        QCOMPARE(presets.size(), 10);

        QSet<QString> ids;
        for (const auto& p : presets) {
            QVERIFY2(!p.id.isEmpty(), "preset id must not be empty");
            QVERIFY2(!ids.contains(p.id), qPrintable("duplicate preset id: " + p.id));
            ids.insert(p.id);

            QVERIFY2(!p.nameKey.isEmpty(), qPrintable("missing nameKey: " + p.id));
            QVERIFY2(!p.nameFallback.isEmpty(), qPrintable("missing nameFallback: " + p.id));

            QVERIFY2(QColor::isValidColorName(p.darkColor), qPrintable("bad darkColor: " + p.id));
            QVERIFY2(QColor::isValidColorName(p.lightColor), qPrintable("bad lightColor: " + p.id));

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

    void catalogueOrdersSolidsFirst() {
        const auto& presets = BackgroundPresets::catalogue();
        for (qsizetype i = 0; i < 5; ++i)
            QCOMPARE(presets[i].overlayKind, BackgroundPresets::OverlayKind::None);
        for (qsizetype i = 5; i < presets.size(); ++i)
            QCOMPARE(presets[i].overlayKind, BackgroundPresets::OverlayKind::Tile);
    }

    void lookupOfUnknownIdIsEmpty() {
        QVERIFY(BackgroundPresets::byId("no-such-preset").id.isEmpty());
        QVERIFY(BackgroundPresets::byId("").id.isEmpty());
        QVERIFY(!BackgroundPresets::contains("no-such-preset"));
        QVERIFY(BackgroundPresets::contains("graphite"));
        QVERIFY(BackgroundPresets::toVariantMap(BackgroundPresets::byId("nope"), true).isEmpty());
    }

    void variantListResolvesColourForMode() {
        const QVariantList dark = BackgroundPresets::toVariantList(true);
        const QVariantList light = BackgroundPresets::toVariantList(false);
        QCOMPARE(dark.size(), BackgroundPresets::catalogue().size());

        const QVariantMap first = dark.first().toMap();
        QCOMPARE(first.value("color").toString(), BackgroundPresets::catalogue().first().darkColor);
        QCOMPARE(light.first().toMap().value("color").toString(),
                 BackgroundPresets::catalogue().first().lightColor);
    }

    // --- Contrast ----------------------------------------------------------

    void everyPresetKeepsTextLegible() {
        for (const auto& p : BackgroundPresets::catalogue()) {
            checkTextOn(QColor(p.darkColor), true, "preset " + p.id, SettingsTheme::darkDefaults());
            checkTextOn(QColor(p.lightColor), false, "preset " + p.id, SettingsTheme::lightDefaults());
        }
    }

    void glassPalettesKeepTextLegible() {
        const QVariantMap glassDark = SettingsTheme::glassDarkDefaults();
        const QVariantMap glassLight = SettingsTheme::glassLightDefaults();

        // Glass over its own background colour, measured against Glass's own text colours.
        checkTextOn(QColor(glassDark.value("backgroundColor").toString()), true, "glass", glassDark);
        checkTextOn(QColor(glassLight.value("backgroundColor").toString()), false, "glass", glassLight);

        // Glass crossed with every preset — the combination a user actually sits in front
        // of once theme and background are independent choices.
        for (const auto& p : BackgroundPresets::catalogue()) {
            checkTextOn(QColor(p.darkColor), true, "glass + " + p.id, glassDark);
            checkTextOn(QColor(p.lightColor), false, "glass + " + p.id, glassLight);
        }
    }

    void glassSurfaceSeparatesFromBackground() {
        // The one thing a glass palette must do: a card has to stay visibly distinct from
        // the page once composited at the scrim alpha, or every card vanishes.
        //
        // Measured as a CIE L* difference, NOT a WCAG contrast ratio. WCAG ratios are built
        // for text legibility and compress badly in the bright range — the same visible step
        // that scores 1.11 on a near-black page scores 1.05 on a near-white one, so a single
        // ratio threshold would either pass an invisible light card or fail a fine dark one.
        // L* is perceptually uniform, so one threshold means the same thing in both.
        for (bool dark : {true, false}) {
            const QVariantMap palette = dark ? SettingsTheme::glassDarkDefaults()
                                             : SettingsTheme::glassLightDefaults();
            const QColor base(palette.value("backgroundColor").toString());
            const QColor card = scrimOver(QColor(palette.value("surfaceColor").toString()), base);
            const double delta = std::abs(lstar(card) - lstar(base));
            QVERIFY2(delta >= 3.0,
                     qPrintable(QString("glass %1: card is only %2 L* from the page — cards "
                                        "will read as invisible")
                                    .arg(dark ? "dark" : "light").arg(delta, 0, 'f', 2)));
        }
    }

    // --- Settings ----------------------------------------------------------

    void defaultsToNoPreset() {
        SettingsTheme theme;
        QCOMPARE(theme.backgroundPreset(), QString());
        QVERIFY(theme.activeBackgroundPreset().isEmpty());
        QVERIFY(!theme.isGlassPalette());
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

    void modeSwitchKeepsThePresetAndFlipsItsColour() {
        SettingsTheme theme;
        theme.setThemeMode("dark");
        theme.setBackgroundPreset("graphite");
        const QString darkColor = theme.activeBackgroundPreset().value("color").toString();

        theme.setThemeMode("light");
        QCOMPARE(theme.backgroundPreset(), QString("graphite"));  // selection survives
        const QString lightColor = theme.activeBackgroundPreset().value("color").toString();
        QVERIFY(darkColor != lightColor);
        QCOMPARE(lightColor, BackgroundPresets::byId("graphite").lightColor);
    }

    // --- Glass theme -------------------------------------------------------

    void glassIsListedAsABuiltInTheme() {
        SettingsTheme theme;
        QVERIFY(theme.themeNames().contains(SettingsTheme::kGlassThemeName));

        bool found = false;
        for (const QVariant& v : theme.getPresetThemes()) {
            const QVariantMap map = v.toMap();
            if (map.value("name").toString() == SettingsTheme::kGlassThemeName) {
                found = true;
                QVERIFY2(map.value("isBuiltIn").toBool(), "Glass must be marked built-in");
            }
        }
        QVERIFY2(found, "Glass missing from getPresetThemes()");
    }

    void applyingGlassInstallsItsPalette() {
        SettingsTheme theme;

        theme.setThemeMode("dark");
        theme.applyDarkTheme(SettingsTheme::kGlassThemeName);
        QCOMPARE(theme.customThemeColors().value("backgroundColor").toString(),
                 SettingsTheme::glassDarkDefaults().value("backgroundColor").toString());
        QVERIFY(theme.isGlassPalette());

        theme.setThemeMode("light");
        theme.applyLightTheme(SettingsTheme::kGlassThemeName);
        QCOMPARE(theme.customThemeColors().value("backgroundColor").toString(),
                 SettingsTheme::glassLightDefaults().value("backgroundColor").toString());
        QVERIFY(theme.isGlassPalette());
    }

    void glassIsOnlyGlassInTheSlotItWasAppliedTo() {
        SettingsTheme theme;
        theme.applyDarkTheme(SettingsTheme::kGlassThemeName);
        theme.applyLightTheme("Default Light");

        theme.setThemeMode("dark");
        QVERIFY(theme.isGlassPalette());
        theme.setThemeMode("light");
        QVERIFY(!theme.isGlassPalette());
    }

    void glassCannotBeOverwrittenOrDeleted() {
        SettingsTheme theme;
        theme.applyDarkTheme(SettingsTheme::kGlassThemeName);

        theme.saveCurrentTheme(SettingsTheme::kGlassThemeName);
        // A refused save leaves no user theme behind, so Glass appears exactly once.
        QCOMPARE(theme.themeNames().count(SettingsTheme::kGlassThemeName), 1);

        theme.deleteUserTheme(SettingsTheme::kGlassThemeName);
        QVERIFY(theme.themeNames().contains(SettingsTheme::kGlassThemeName));
        QCOMPARE(SettingsTheme::glassDarkDefaults().value("backgroundColor").toString(),
                 QString("#101319"));
    }

    void editingGlassForksButKeepsTheGlassLook() {
        SettingsTheme theme;
        theme.setThemeMode("dark");
        theme.applyDarkTheme(SettingsTheme::kGlassThemeName);
        theme.setEditingPalette("dark");

        theme.setEditingPaletteColor("primaryColor", "#ff8800");

        // The slot forks to "Custom" — the built-in is never written...
        QCOMPARE(theme.darkThemeName(), QString("Custom"));
        QVERIFY(theme.themeNames().contains(SettingsTheme::kGlassThemeName));
        // ...and the glass look rides in the palette, so it survives the fork. This is the
        // whole reason glassiness is a palette entry rather than a theme-name check.
        QVERIFY2(theme.isGlassPalette(), "editing one colour must not turn the chrome opaque");
    }

    void aSavedCopyOfGlassStaysGlass() {
        SettingsTheme theme;
        theme.setThemeMode("dark");
        theme.applyDarkTheme(SettingsTheme::kGlassThemeName);
        theme.setEditingPalette("dark");
        theme.setEditingPaletteColor("primaryColor", "#ff8800");

        theme.saveCurrentTheme("My Glass");
        theme.applyDarkTheme("Default Dark");
        QVERIFY(!theme.isGlassPalette());

        theme.applyDarkTheme("My Glass");
        QVERIFY2(theme.isGlassPalette(), "a user theme saved from Glass must stay glass");
    }

    void nonGlassThemesAreNotGlass() {
        SettingsTheme theme;
        theme.setThemeMode("dark");
        theme.applyDarkTheme("Default Dark");
        QVERIFY(!theme.isGlassPalette());
    }
};

QTEST_MAIN(TestBackgroundPresets)
#include "tst_backgroundpresets.moc"
