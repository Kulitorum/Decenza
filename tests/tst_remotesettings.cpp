#include <QtTest>

#include <QJsonObject>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QUrl>

#include <limits>
#include <utility>

#include "hostsessiontestfixture.h"
#include "core/settings_theme.h"
#include "core/graphpreferences.h"
#include "core/backgroundpresets.h"
#include "webclient/remotesessionbackend.h"
#include "webclient/remotesettings.h"
#include "webclient/remoteaccessibilitymanager.h"

using SessionFixture = HostSessionTest::Fixture;

class tst_RemoteSettings final : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }
    void fontSizesUseNativeDefaultsBeforeAndAfterAppearance()
    {
        QVariantMap nativeDefaults;
        for (auto it = SettingsTheme::fontSizeDefaults().constBegin();
             it != SettingsTheme::fontSizeDefaults().constEnd(); ++it) {
            nativeDefaults.insert(it.key(), it.value());
        }

        RemoteSettingsTheme theme;
        QCOMPARE(theme.effectiveFontSizes(), nativeDefaults);
        for (auto it = nativeDefaults.constBegin(); it != nativeDefaults.constEnd(); ++it) {
            QVERIFY2(it.value().toInt() > 0,
                     qPrintable(it.key() + QStringLiteral(" default must be positive")));
        }

        theme.setAppearance({{QStringLiteral("fontSizes"), QVariantMap{
            {QStringLiteral("titleSize"), 27}
        }}});
        QCOMPARE(theme.effectiveFontSizes().value(QStringLiteral("titleSize")).toInt(), 27);
        theme.setAppearance({});
        QCOMPARE(theme.effectiveFontSizes(), nativeDefaults);
    }

    void facadeTracksSnapshotsAndSessionLifetime()
    {
        const QString preset = BackgroundPresets::colours().at(2).id;
        QVariantMap first = HostSessionTest::appearance(preset, true, 1);
        QVariantMap firstGraph = GraphPreferences::defaults();
        firstGraph.insert(QStringLiteral("showPressure"), false);
        firstGraph.insert(QStringLiteral("showResistance"), true);
        firstGraph.insert(QStringLiteral("flowMultiplier"), 3);
        firstGraph.insert(QStringLiteral("rightAxisMode"), QStringLiteral("temperature"));
        firstGraph.insert(QStringLiteral("advancedMode"), true);
        first.insert(QStringLiteral("graph"), firstGraph);
        SessionFixture server;
        server.setAppearance(first);

        QQmlEngine engine;
        RemoteSettings* settings = RemoteSettings::create(&engine, nullptr);
        auto* session = new RemoteSessionBackend;
        settings->setSession(session);
        QSignalSpy themeChanged(settings->theme(), &RemoteSettingsTheme::changed);
        QSignalSpy appChanged(settings->app(), &RemoteSettingsApp::changed);
        server.attach(session, &engine);
        session->connectToHost();

        QTRY_VERIFY_WITH_TIMEOUT(session->connected(), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(settings->theme()->backgroundPreset(), preset, 3000);
        QCOMPARE(settings->theme()->isDarkMode(), true);
        QCOMPARE(settings->theme()->flashPhase(), 1);
        QCOMPARE(settings->theme()->customThemeColors().value(QStringLiteral("backgroundColor")),
                 QStringLiteral("#123456"));
        QCOMPARE(settings->theme()->effectiveFontSizes().value(QStringLiteral("titleSize")).toInt(), 24);
        QCOMPARE(settings->theme()->activeBackgroundPreset().value(QStringLiteral("id")).toString(), preset);
        QVERIFY(settings->theme()->derivedBackgroundColors().contains(QStringLiteral("surface")));
        QVERIFY(settings->app()->use12HourTime());
        QCOMPARE(settings->app()->temperatureUnit(), QStringLiteral("fahrenheit"));
        QCOMPARE(settings->graph()->showPressure(), false);
        QCOMPARE(settings->graph()->showResistance(), true);
        QCOMPARE(settings->graph()->flowMultiplier(), 3);
        QCOMPARE(settings->graph()->rightAxisMode(), QStringLiteral("temperature"));
        QCOMPARE(settings->graph()->advancedMode(), true);

        const BackgroundPresets::Colour colour = BackgroundPresets::colourById(preset);
        const QString expected = BackgroundPresets::adjustForContrast(
            QColor(QStringLiteral("#e94560")),
            BackgroundPresets::pageUnderDensestPattern(QColor(colour.value))).name();
        QCOMPARE(settings->theme()->adjustedForContrast(QStringLiteral("#e94560"), colour.value),
                 expected);

        QVariantMap second = HostSessionTest::appearance(BackgroundPresets::colours().at(3).id,
                                                         false, 2);
        QVariantMap secondBackground = second.value(QStringLiteral("background")).toMap();
        const QString patternId = BackgroundPresets::patterns().first().id;
        secondBackground.insert(QStringLiteral("patternId"), patternId);
        second.insert(QStringLiteral("background"), secondBackground);
        QVariantMap secondGraph = GraphPreferences::defaults();
        secondGraph.insert(QStringLiteral("showFlow"), false);
        secondGraph.insert(QStringLiteral("flowMultiplier"), 1);
        secondGraph.insert(QStringLiteral("rightAxisMode"), QStringLiteral("flow"));
        secondGraph.insert(QStringLiteral("advancedMode"), false);
        second.insert(QStringLiteral("graph"), secondGraph);
        server.send(second, 1);
        QTRY_COMPARE_WITH_TIMEOUT(settings->theme()->backgroundPreset(),
                                  second.value(QStringLiteral("themeInputs")).toMap()
                                      .value(QStringLiteral("backgroundPreset")).toString(), 2000);
        QCOMPARE(settings->theme()->flashPhase(), 2);
        QCOMPARE(settings->theme()->backgroundPattern(), patternId);
        QCOMPARE(settings->graph()->showFlow(), false);
        QCOMPARE(settings->graph()->flowMultiplier(), 1);
        QCOMPARE(settings->graph()->rightAxisMode(), QStringLiteral("flow"));
        QCOMPARE(settings->graph()->advancedMode(), false);
        QCOMPARE(settings->app()->temperatureUnit(), QStringLiteral("celsius"));
        QVERIFY(themeChanged.count() >= 2);
        QVERIFY(appChanged.count() >= 2);

        delete session;
        QVERIFY(settings->session() == nullptr);
        QCOMPARE(settings->theme()->backgroundPreset(), QString());
        QCOMPARE(settings->theme()->effectiveFontSizes(),
                 RemoteSettingsTheme().effectiveFontSizes());
        QCOMPARE(settings->app()->temperatureUnit(), QStringLiteral("celsius"));
        QVERIFY(!settings->app()->use12HourTime());
        const QVariantMap graphDefaults = GraphPreferences::defaults();
        QCOMPARE(settings->graph()->showFlow(), graphDefaults.value(QStringLiteral("showFlow")).toBool());
        QCOMPARE(settings->graph()->flowMultiplier(), graphDefaults.value(QStringLiteral("flowMultiplier")).toInt());
        QCOMPARE(settings->graph()->rightAxisMode(), graphDefaults.value(QStringLiteral("rightAxisMode")).toString());
    }

    void brewAndWaterReadoutInputsFollowFreshSessionSnapshots()
    {
        MachineUiSnapshot first;
        first.primaryScaleAddress = QStringLiteral("test-scale-address");
        first.hardwareSteamTwoTapStop = true;
        first.brewHasTemperatureOverride = true;
        first.brewTemperatureOverride = 94.5;
        first.brewSteamTemperature = 158.0;
        first.brewSteamTimeout = 30;
        first.brewSteamFlow = 150;
        first.brewSteamAutoFlushSeconds = 4;
        first.brewMilkAutoCaptureEnabled = true;
        first.brewSteamSecondsPerGram = 0.15;
        first.brewLastSteamMilkG = 200.0;
        first.brewLastSteamTimeS = 30.0;
        first.brewDoseCupTareWeight = 42.0;
        first.appWaterLevelDisplayUnit = QStringLiteral("ml");
        first.appWaterRefillPoint = 7;
        first.appRefillKitOverride = 1;

        SessionFixture server;
        server.setMachineUi(first);
        server.send(HostSessionTest::appearance(QString(), false, 0), 0);
        QQmlEngine engine;
        auto* settings = RemoteSettings::create(&engine, nullptr);
        RemoteSessionBackend session;
        settings->setSession(&session);
        QSignalSpy appChanged(settings->app(), &RemoteSettingsApp::changed);
        QSignalSpy brewChanged(settings->brew(), &RemoteSettingsBrew::changed);
        server.attach(&session, &engine);
        QVERIFY(session.stale());
        QVERIFY(!settings->brew()->hasTemperatureOverride());
        QVERIFY(!settings->hardware()->steamTwoTapStop());
        session.connectToHost();
        QTRY_VERIFY_WITH_TIMEOUT(session.connected(), 3000);
        QVERIFY(!session.stale());
        server.send(HostSessionTest::appearance(QString(), false, 0), 1);
        QTRY_COMPARE_WITH_TIMEOUT(session.sequence(), qint64(1), 2000);
        QCOMPARE(settings->primaryScaleAddress(), QStringLiteral("test-scale-address"));
        QVERIFY(settings->hardware()->steamTwoTapStop());
        QCOMPARE(settings->brew()->hasTemperatureOverride(), true);
        QCOMPARE(settings->brew()->temperatureOverride(), 94.5);
        QCOMPARE(settings->brew()->steamTemperature(), 158.0);
        QCOMPARE(settings->brew()->steamTimeout(), 30);
        QCOMPARE(settings->brew()->steamFlow(), 150);
        QCOMPARE(settings->brew()->steamAutoFlushSeconds(), 4);
        QCOMPARE(settings->brew()->milkAutoCaptureEnabled(), true);
        QCOMPARE(settings->brew()->steamSecondsPerGram(), 0.15);
        QCOMPARE(settings->brew()->lastSteamMilkG(), 200.0);
        QCOMPARE(settings->brew()->lastSteamTimeS(), 30.0);
        QCOMPARE(settings->brew()->doseCupTareWeight(), 42.0);
        QCOMPARE(settings->app()->waterLevelDisplayUnit(), QStringLiteral("ml"));
        QCOMPARE(settings->app()->waterRefillPoint(), 7);
        QCOMPARE(settings->app()->refillKitOverride(), 1);

        MachineUiSnapshot second = first;
        second.brewHasTemperatureOverride = false;
        second.brewTemperatureOverride = 0.0;
        second.brewSteamTemperature = 162.0;
        second.brewSteamTimeout = 45;
        second.brewSteamFlow = 175;
        second.brewSteamAutoFlushSeconds = 6;
        second.brewMilkAutoCaptureEnabled = false;
        second.brewSteamSecondsPerGram = 0.2;
        second.brewLastSteamMilkG = 250.0;
        second.brewLastSteamTimeS = 50.0;
        second.brewDoseCupTareWeight = 0.0;
        second.appWaterLevelDisplayUnit = QStringLiteral("percent");
        second.appWaterRefillPoint = 4;
        second.appRefillKitOverride = 2;
        server.setMachineUi(second);
        server.send(HostSessionTest::appearance(QString(), false, 0), 2);
        QTRY_COMPARE_WITH_TIMEOUT(settings->brew()->steamTemperature(), 162.0, 2000);
        QCOMPARE(settings->brew()->steamTimeout(), 45);
        QCOMPARE(settings->brew()->steamFlow(), 175);
        QCOMPARE(settings->brew()->steamAutoFlushSeconds(), 6);
        QCOMPARE(settings->brew()->milkAutoCaptureEnabled(), false);
        QCOMPARE(settings->brew()->steamSecondsPerGram(), 0.2);
        QCOMPARE(settings->brew()->lastSteamMilkG(), 250.0);
        QCOMPARE(settings->brew()->lastSteamTimeS(), 50.0);
        QCOMPARE(settings->brew()->hasTemperatureOverride(), false);
        QCOMPARE(settings->brew()->temperatureOverride(), 0.0);
        QCOMPARE(settings->brew()->doseCupTareWeight(), 0.0);
        QCOMPARE(settings->app()->waterLevelDisplayUnit(), QStringLiteral("percent"));
        QCOMPARE(settings->app()->waterRefillPoint(), 4);
        QCOMPARE(settings->app()->refillKitOverride(), 2);
        QVERIFY(appChanged.count() >= 2);
        QVERIFY(brewChanged.count() >= 2);

        session.disconnectFromHost();
        QCOMPARE(settings->brew()->hasTemperatureOverride(), false);
        QCOMPARE(settings->brew()->temperatureOverride(), 0.0);
        QCOMPARE(settings->brew()->steamTemperature(), 0.0);
        QCOMPARE(settings->brew()->steamTimeout(), 0);
        QCOMPARE(settings->brew()->steamFlow(), 0);
        QCOMPARE(settings->brew()->steamAutoFlushSeconds(), 0);
        QCOMPARE(settings->brew()->milkAutoCaptureEnabled(), false);
        QCOMPARE(settings->brew()->steamSecondsPerGram(), 0.0);
        QCOMPARE(settings->brew()->lastSteamMilkG(), 0.0);
        QCOMPARE(settings->brew()->lastSteamTimeS(), 0.0);
        QCOMPARE(settings->brew()->doseCupTareWeight(), 0.0);
        QCOMPARE(settings->app()->waterLevelDisplayUnit(), QStringLiteral("percent"));
        QCOMPARE(settings->app()->waterRefillPoint(), 5);
        QCOMPARE(settings->app()->refillKitOverride(), 2);
        QVERIFY(settings->primaryScaleAddress().isEmpty());
        QVERIFY(!settings->hardware()->steamTwoTapStop());
    }

    void steamPitcherReadoutsFollowFreshSnapshotsAndClearWhenStale()
    {
        const QJsonObject firstPreset{
            {QStringLiteral("name"), QStringLiteral("Small pitcher")},
            {QStringLiteral("duration"), 180}, {QStringLiteral("flow"), 50},
            {QStringLiteral("temperature"), 155.0},
            {QStringLiteral("pitcherWeightG"), 310.0},
            {QStringLiteral("calibMilkG"), 240.0},
            {QStringLiteral("disabled"), false}, {QStringLiteral("builtin"), false}
        };
        const QJsonObject heaterOff{
            {QStringLiteral("disabled"), true}, {QStringLiteral("builtin"), true}
        };
        SessionFixture server;
        server.setSteamPitchers({firstPreset.toVariantMap(), heaterOff.toVariantMap()}, 0);
        server.send(HostSessionTest::appearance(QString(), false, 0), 0);
        QQmlEngine engine;
        auto* settings = RemoteSettings::create(&engine, nullptr);
        QSignalSpy presetsChanged(settings->brew(), &RemoteSettingsBrew::steamPitcherPresetsChanged);
        QSignalSpy selectionChanged(settings->brew(), &RemoteSettingsBrew::selectedSteamPitcherChanged);
        RemoteSessionBackend session;
        settings->setSession(&session);
        server.attach(&session, &engine);
        session.connectToHost();
        QTRY_VERIFY_WITH_TIMEOUT(session.connected(), 3000);
        server.send(HostSessionTest::appearance(QString(), false, 0), 1);
        QTRY_COMPARE_WITH_TIMEOUT(session.sequence(), qint64(1), 2000);

        QCOMPARE(settings->brew()->steamPitcherPresets().size(), 2);
        QCOMPARE(settings->brew()->selectedSteamPitcher(), 0);
        QCOMPARE(settings->brew()->selectedSteamPitcherDisplayIndex(), 0);
        QCOMPARE(presetsChanged.count(), 1);
        QCOMPARE(selectionChanged.count(), 1);
        QCOMPARE(settings->brew()->getSteamPitcherPreset(0).value(QStringLiteral("name")).toString(),
                 QStringLiteral("Small pitcher"));
        QCOMPARE(settings->brew()->getSteamPitcherPreset(1).value(QStringLiteral("builtin")).toBool(),
                 true);

        const QJsonObject secondPreset{
            {QStringLiteral("name"), QStringLiteral("Large pitcher")},
            {QStringLiteral("duration"), 240}, {QStringLiteral("disabled"), false},
            {QStringLiteral("builtin"), false}
        };
        server.setSteamPitchers({secondPreset.toVariantMap(), heaterOff.toVariantMap()}, -1);
        server.send(HostSessionTest::appearance(QString(), false, 0), 2);
        QTRY_COMPARE_WITH_TIMEOUT(settings->brew()->selectedSteamPitcher(), -1, 2000);
        QCOMPARE(settings->brew()->steamPitcherPresets().first().toMap()
                     .value(QStringLiteral("name")).toString(), QStringLiteral("Large pitcher"));
        QCOMPARE(settings->brew()->selectedSteamPitcherDisplayIndex(), 1);
        QCOMPARE(presetsChanged.count(), 2);
        QCOMPARE(selectionChanged.count(), 2);
        QCOMPARE(settings->brew()->getSteamPitcherPreset(-1).value(QStringLiteral("builtin")).toBool(),
                 true);

        server.send(HostSessionTest::appearance(QString(), false, 0), 3);
        QTRY_COMPARE_WITH_TIMEOUT(session.sequence(), 3, 2000);
        QCOMPARE(presetsChanged.count(), 2);
        QCOMPARE(selectionChanged.count(), 2);

        server.send(HostSessionTest::appearance(QString(), false, 0), 5); // A gap invalidates the whole session snapshot.
        QTRY_VERIFY_WITH_TIMEOUT(session.stale(), 2000);
        QVERIFY(settings->brew()->steamPitcherPresets().isEmpty());
        QCOMPARE(settings->brew()->selectedSteamPitcher(), -1);
        QCOMPARE(settings->brew()->selectedSteamPitcherDisplayIndex(), -1);
        QCOMPARE(presetsChanged.count(), 3);
        QCOMPARE(selectionChanged.count(), 3);
        session.disconnectFromHost();
        QVERIFY(settings->brew()->steamPitcherPresets().isEmpty());
        QCOMPARE(settings->brew()->selectedSteamPitcher(), -1);
        QCOMPARE(settings->brew()->selectedSteamPitcherDisplayIndex(), -1);
        QVERIFY(settings->brew()->getSteamPitcherPreset(0).isEmpty());
    }

    void steamPitcherCalculationsUseFreshHostFactsAndSharedRules()
    {
        const QJsonObject pitcher{
            {QStringLiteral("name"), QStringLiteral("Small pitcher")},
            {QStringLiteral("duration"), 180},
            {QStringLiteral("pitcherWeightG"), 310.0},
            {QStringLiteral("disabled"), false},
            {QStringLiteral("builtin"), false}
        };
        const QJsonObject heaterOff{
            {QStringLiteral("disabled"), true},
            {QStringLiteral("builtin"), true}
        };
        MachineUiSnapshot machineUi;
        machineUi.brewMilkAutoCaptureEnabled = true;
        machineUi.brewSteamSecondsPerGram = 0.15;

        SessionFixture server;
        server.setMachineUi(machineUi);
        server.setSteamPitchers({pitcher.toVariantMap(), heaterOff.toVariantMap()}, 0);
        server.send(HostSessionTest::appearance(QString(), false, 0), 0);
        QQmlEngine engine;
        auto* settings = RemoteSettings::create(&engine, nullptr);
        RemoteSessionBackend session;
        settings->setSession(&session);
        server.attach(&session, &engine);
        session.connectToHost();
        QTRY_VERIFY_WITH_TIMEOUT(session.connected(), 3000);
        server.send(HostSessionTest::appearance(QString(), false, 0), 1);
        QTRY_COMPARE_WITH_TIMEOUT(session.sequence(), qint64(1), 2000);

        RemoteSettingsBrew* brew = settings->brew();
        QCOMPARE(brew->steamPitcherCount(), 1);
        QVERIFY(brew->steamPitcherNameTaken(QStringLiteral("  SMALL PITCHER ")));
        QVERIFY(brew->steamPitcherNameTaken(QStringLiteral("heater OFF")));
        QVERIFY(!brew->steamPitcherNameTaken(QStringLiteral("Heater offside")));
        QVERIFY(!brew->steamPitcherNameTaken(QStringLiteral("Small pitcher"), 0));
        QCOMPARE(brew->netMilkForPitcher(0, 360.0), 50.0);
        QCOMPARE(brew->netMilkForPitcher(0, 359.0), 0.0);
        QCOMPARE(brew->netMilkForPitcher(-1, 1000.0), 0.0);
        QCOMPARE(brew->scaledSteamTime(0, 100.0), 15);
        QCOMPARE(brew->scaledSteamTime(0, std::numeric_limits<double>::max()), 120);
        QCOMPARE(brew->scaledSteamTime(0, std::numeric_limits<double>::quiet_NaN()), 0);
        QCOMPARE(brew->effectiveSteamDurationSec(0, 100.0), 15);
        QCOMPARE(brew->effectiveSteamDurationSec(0, 1000.0), 120);
        QCOMPARE(brew->effectiveSteamDurationSec(0, 0.0), 180);
        QCOMPARE(brew->scaledSteamTime(-1, 100.0), 0);
        QCOMPARE(brew->effectiveSteamDurationSec(-1, 100.0), 0);

        MachineUiSnapshot uncalibrated = machineUi;
        uncalibrated.brewMilkAutoCaptureEnabled = false;
        uncalibrated.brewSteamSecondsPerGram = 0.0;
        server.setMachineUi(uncalibrated);
        server.send(HostSessionTest::appearance(QString(), false, 0), 2);
        QTRY_COMPARE_WITH_TIMEOUT(brew->steamSecondsPerGram(), 0.0, 2000);
        QCOMPARE(brew->scaledSteamTime(0, 100.0), 0);
        QCOMPARE(brew->effectiveSteamDurationSec(0, 100.0), 180);

        QJsonObject invalidDurationPitcher = pitcher;
        invalidDurationPitcher.insert(QStringLiteral("duration"),
                                      std::numeric_limits<double>::max());
        server.setSteamPitchers({invalidDurationPitcher.toVariantMap(), heaterOff.toVariantMap()}, 0);
        server.send(HostSessionTest::appearance(QString(), false, 0), 3);
        QTRY_COMPARE_WITH_TIMEOUT(brew->getSteamPitcherPreset(0)
                                      .value(QStringLiteral("duration")).toDouble(),
                                  std::numeric_limits<double>::max(), 2000);
        QCOMPARE(brew->effectiveSteamDurationSec(0, 100.0), 0);

        session.disconnectFromHost();
        QCOMPARE(brew->steamPitcherCount(), 0);
        QCOMPARE(brew->netMilkForPitcher(0, 360.0), 0.0);
        QCOMPARE(brew->effectiveSteamDurationSec(0, 100.0), 0);
    }

    void malformedPitcherSnapshotsNeverAuthorizeSettingsReadouts()
    {
        const QVariantMap valid = HostSessionTest::snapshot(
            HostSessionTest::appearance(QString(), false, 0), 0);
        QList<QVariantMap> invalidSnapshots;
        QVariantMap missing = valid;
        missing.remove(QStringLiteral("steamPitchers"));
        invalidSnapshots.append(missing);
        QVariantMap nullBlock = valid;
        nullBlock.insert(QStringLiteral("steamPitchers"), QVariant());
        invalidSnapshots.append(nullBlock);
        QVariantMap malformed = valid;
        malformed.insert(QStringLiteral("steamPitchers"), QVariantMap{
            {QStringLiteral("presets"), QVariantList{}},
            {QStringLiteral("selectedIndex"), 0}
        });
        invalidSnapshots.append(malformed);

        for (const QVariantMap& invalid : std::as_const(invalidSnapshots)) {
            SessionFixture server;
            server.source.setSnapshot(invalid);
            QQmlEngine engine;
            auto* settings = RemoteSettings::create(&engine, nullptr);
            RemoteSessionBackend session;
            settings->setSession(&session);
            server.attach(&session, &engine);
            session.connectToHost();
            QTRY_VERIFY_WITH_TIMEOUT(!session.errorMessage().isEmpty(), 3000);
            QVERIFY(session.stale());
            QVERIFY(!session.connected());
            QVERIFY(settings->brew()->steamPitcherPresets().isEmpty());
            QCOMPARE(settings->brew()->selectedSteamPitcher(), -1);
        }
    }

    void themeResolvesBackgroundImageOnlyAgainstItsSessionOrigin()
    {
        RemoteSettingsTheme theme;
        QVariantMap appearance = HostSessionTest::appearance(QString(), false, 0);
        QVariantMap background = appearance.value(QStringLiteral("background")).toMap();
        background.insert(QStringLiteral("source"), QStringLiteral("image"));
        const QString revision(32, QLatin1Char('a'));
        background.insert(QStringLiteral("imageRevision"), revision);
        appearance.insert(QStringLiteral("background"), background);

        const QString origin = QStringLiteral("https://decenza.local:8443");
        theme.setAppearance(appearance, origin);
        QCOMPARE(theme.backgroundImageUrl(),
                 QUrl(origin + QStringLiteral("/api/session/background-image/") + revision));

        QCOMPARE(theme.backgroundPresets().size(), BackgroundPresets::coloursAsVariantList().size());
        QCOMPARE(theme.backgroundPatterns().size(), BackgroundPresets::patternsAsVariantList().size());
        QCOMPARE(theme.backgroundShotAdvanced(), false);

        QVariantMap shaderAppearance = HostSessionTest::appearance(QString(), false, 0);
        QVariantMap themeInputs = shaderAppearance.value(QStringLiteral("themeInputs")).toMap();
        themeInputs.insert(QStringLiteral("activeShader"), QStringLiteral("crt"));
        themeInputs.insert(QStringLiteral("shaderParams"), QVariantMap{
            {QStringLiteral("scanlineIntensity"), 0.42},
            {QStringLiteral("hueShift"), 45.0}
        });
        shaderAppearance.insert(QStringLiteral("themeInputs"), themeInputs);
        theme.setAppearance(shaderAppearance, origin);
        QCOMPARE(theme.activeShader(), QStringLiteral("crt"));
        QCOMPARE(theme.shaderParams().value(QStringLiteral("scanlineIntensity")).toDouble(), 0.42);
        QCOMPARE(theme.shaderParams().value(QStringLiteral("hueShift")).toDouble(), 45.0);

        theme.setAppearance(HostSessionTest::appearance(QString(), false, 0), origin);
        QVERIFY(theme.backgroundImageUrl().isEmpty());
        QCOMPARE(theme.backgroundSource(), QStringLiteral("none"));
        QCOMPARE(theme.activeShader(), QString());
        QVERIFY(theme.shaderParams().isEmpty());
    }

    void graphFacadePublishesReadOnlyValuesAndClearsToDefaults()
    {
        RemoteSettingsGraph graph;
        QSignalSpy changed(&graph, &RemoteSettingsGraph::changed);
        const QVariantMap defaults = GraphPreferences::defaults();
        graph.setSnapshot(defaults);
        QCOMPARE(changed.count(), 1);

        QVariantMap changedValues = defaults;
        changedValues.insert(QStringLiteral("showWeight"), false);
        changedValues.insert(QStringLiteral("showTemperatureMix"), true);
        changedValues.insert(QStringLiteral("flowMultiplier"), 3);
        changedValues.insert(QStringLiteral("rightAxisMode"), QStringLiteral("temperature"));
        changedValues.insert(QStringLiteral("advancedMode"), true);
        graph.setSnapshot(changedValues);
        QCOMPARE(graph.showWeight(), false);
        QCOMPARE(graph.showTemperatureMix(), true);
        QCOMPARE(graph.flowMultiplier(), 3);
        QCOMPARE(graph.rightAxisMode(), QStringLiteral("temperature"));
        QCOMPARE(graph.advancedMode(), true);
        QCOMPARE(changed.count(), 2);

        graph.setSnapshot({});
        QCOMPARE(graph.showWeight(), defaults.value(QStringLiteral("showWeight")).toBool());
        QCOMPARE(graph.showTemperatureMix(), false);
        QCOMPARE(graph.flowMultiplier(), 2);
        QCOMPARE(graph.rightAxisMode(), QStringLiteral("weight"));
        QCOMPARE(graph.advancedMode(), false);
        QCOMPARE(changed.count(), 3);
    }

    void graphPreferencesKeepSteamVisibilitySeparateFromShotTraces()
    {
        const QVariantMap defaults = GraphPreferences::defaults();
        QVERIFY(defaults.contains(QStringLiteral("showSteamPressure")));
        QVERIFY(defaults.contains(QStringLiteral("showSteamFlow")));
        QVERIFY(defaults.contains(QStringLiteral("showSteamTemperature")));
        QVERIFY(!GraphPreferences::shotDefaults().contains(QStringLiteral("showSteamPressure")));
        QVERIFY(GraphPreferences::validate(defaults));
        QVERIFY(GraphPreferences::validateSteam(GraphPreferences::steamDefaults()));

        QVariantMap invalid = defaults;
        invalid.remove(QStringLiteral("showSteamFlow"));
        QVERIFY(!GraphPreferences::validate(invalid));
        invalid = GraphPreferences::steamDefaults();
        invalid.insert(QStringLiteral("showSteamFlow"), QStringLiteral("false"));
        QVERIFY(!GraphPreferences::validateSteam(invalid));
    }

    void accessibilitySettingsFollowFreshHostSnapshots()
    {
        SteamUiState initialAccessibility;
        initialAccessibility.extractionAnnouncementsEnabled = false;
        initialAccessibility.extractionAnnouncementInterval = 10;
        initialAccessibility.extractionAnnouncementMode = QStringLiteral("milestones_only");
        SessionFixture server;
        server.setSteamUi(initialAccessibility);
        server.send(HostSessionTest::appearance(QString(), false, 0), 0);
        QQmlEngine engine;
        auto* settings = RemoteSettings::create(&engine, nullptr);
        RemoteSessionBackend session;
        settings->setSession(&session);
        server.attach(&session, &engine);
        session.connectToHost();
        QTRY_VERIFY_WITH_TIMEOUT(session.connected(), 3000);

        RemoteAccessibilityManager* accessibility =
            RemoteAccessibilityManager::create(&engine, nullptr);
        accessibility->setSession(&session);

        server.send(HostSessionTest::appearance(QString(), false, 0), 1);
        QTRY_COMPARE_WITH_TIMEOUT(session.sequence(), qint64(1), 2000);
        QVERIFY(!accessibility->extractionAnnouncementsEnabled());
        QCOMPARE(accessibility->extractionAnnouncementInterval(), 10);
        QCOMPARE(accessibility->extractionAnnouncementMode(), QStringLiteral("milestones_only"));

        SteamUiState updatedAccessibility;
        updatedAccessibility.extractionAnnouncementsEnabled = true;
        updatedAccessibility.extractionAnnouncementInterval = 5;
        updatedAccessibility.extractionAnnouncementMode = QStringLiteral("both");
        server.setSteamUi(updatedAccessibility);
        server.send(HostSessionTest::appearance(QString(), false, 0), 2);
        QTRY_VERIFY_WITH_TIMEOUT(accessibility->extractionAnnouncementsEnabled(), 2000);
        QCOMPARE(accessibility->extractionAnnouncementInterval(), 5);
        QCOMPARE(accessibility->extractionAnnouncementMode(), QStringLiteral("both"));

        session.disconnectFromHost();
    }
};

QTEST_GUILESS_MAIN(tst_RemoteSettings)
#include "tst_remotesettings.moc"
