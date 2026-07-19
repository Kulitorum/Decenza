#include <QtTest>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>

#include "core/settings.h"
#include "core/settings_brew.h"
#include "mcp/mcptoolregistry.h"
#include "ai/aimanager.h"
#include "controllers/maincontroller.h"

// The test passes a null MainController, so its implementation (and aimanager.cpp)
// isn't linked. Define the handful of symbols mcptools_presets.cpp references so the
// test links; the apply-to-machine paths are null-guarded and never call these.
QVariantList AIManager::availableModels(const QString&) const { return {}; }
void MainController::applySteamSettings() {}
void MainController::applyHotWaterSettings() {}
void MainController::turnOffSteamHeater() {}

// Implemented in src/mcp/mcptools_presets.cpp.
class MachineState;
void registerPresetsTools(McpToolRegistry* registry, Settings* settings, MainController* mainController,
                          MachineState* machineState);

// Exercises the steam-pitcher and hot-water-vessel MCP tools: unit conversions
// (steam flow ×100, water flow ×10), partial-update merge (editing one field must
// not clobber the others), apply-on-select / apply-on-update writing the active
// settings, and the error guards.
class tst_McpToolsPresets : public QObject {
    Q_OBJECT

    Settings m_settings;
    McpToolRegistry m_registry;

    // Raw preset-store snapshot, restored in cleanup so the dev machine's real
    // steam/water presets aren't disturbed by the test.
    QByteArray m_origPitcherPresets;
    QByteArray m_origVesselPresets;
    int m_origSelectedSteam = 0;
    int m_origSelectedWater = 0;

    QJsonObject call(const QString& name, const QJsonObject& args, int accessLevel = 2) {
        QString err;
        QJsonObject r = m_registry.callTool(name, args, accessLevel, err);
        if (!err.isEmpty()) r.insert("callError", err);
        return r;
    }

private slots:
    void initTestCase() {
        // No MachineState in the harness: applySteamPitcher's milk read is
        // nullptr-guarded, so selects resolve to the preset's base duration.
        registerPresetsTools(&m_registry, &m_settings, nullptr, nullptr);
    }

    void init() { QTest::failOnWarning();
        QSettings raw(Settings::testQSettingsPath(), QSettings::IniFormat);
        m_origPitcherPresets = raw.value("steam/pitcherPresets").toByteArray();
        m_origVesselPresets = raw.value("water/vesselPresets").toByteArray();
        m_origSelectedSteam = m_settings.brew()->selectedSteamPitcher();
        m_origSelectedWater = m_settings.brew()->selectedWaterVessel();
    }

    void cleanup() {
        QSettings raw(Settings::testQSettingsPath(), QSettings::IniFormat);
        raw.setValue("steam/pitcherPresets", m_origPitcherPresets);
        raw.setValue("water/vesselPresets", m_origVesselPresets);
        raw.sync();
        m_settings.brew()->setSelectedSteamCup(m_origSelectedSteam);
        m_settings.brew()->setSelectedWaterCup(m_origSelectedWater);
    }

    // --- steam ---------------------------------------------------------------

    void steamAddConvertsFlowAndReportsBack() {
        QJsonObject add = call("steam_pitcher_add",
            {{"name", "Conv"}, {"durationSec", 28}, {"flowMlPerSec", 1.5}, {"temperatureC", 134.0}});
        QVERIFY(add["success"].toBool());
        const int idx = add["selectedIndex"].toInt();

        // Stored in hundredths of mL/s.
        QCOMPARE(m_settings.brew()->getSteamPitcherPreset(idx)["flow"].toInt(), 150);
        QCOMPARE(m_settings.brew()->getSteamPitcherPreset(idx)["temperature"].toDouble(), 134.0);

        // list reports mL/s.
        QJsonArray presets = call("steam_pitcher_list", {})["presets"].toArray();
        QJsonObject p = presets[idx].toObject();
        QCOMPARE(p["flowMlPerSec"].toDouble(), 1.5);
        QCOMPARE(p["durationSec"].toInt(), 28);
        QCOMPARE(p["temperatureC"].toDouble(), 134.0);
    }

    void steamPartialUpdatePreservesOtherFields() {
        QJsonObject add = call("steam_pitcher_add",
            {{"name", "Edit"}, {"durationSec", 40}, {"flowMlPerSec", 1.2}, {"temperatureC", 145.0}});
        const int idx = add["selectedIndex"].toInt();

        // Change only the temperature.
        QVERIFY(call("steam_pitcher_update", {{"index", idx}, {"temperatureC", 150.0}})["success"].toBool());

        QVariantMap p = m_settings.brew()->getSteamPitcherPreset(idx);
        QCOMPARE(p["temperature"].toDouble(), 150.0);
        QCOMPARE(p["duration"].toInt(), 40);   // unchanged
        QCOMPARE(p["flow"].toInt(), 120);      // unchanged (1.2 mL/s)
        QCOMPARE(p["name"].toString(), QString("Edit"));
    }

    void steamSelectAppliesTemperatureToActive() {
        const int a = call("steam_pitcher_add", {{"name", "A"}, {"temperatureC", 130.0}})["selectedIndex"].toInt();
        call("steam_pitcher_add", {{"name", "B"}, {"temperatureC", 150.0}});  // now selected, active = 150
        QCOMPARE(m_settings.brew()->steamTemperature(), 150.0);

        QVERIFY(call("steam_pitcher_select", {{"index", a}}, /*control*/ 1)["success"].toBool());
        QCOMPARE(m_settings.brew()->steamTemperature(), 130.0);  // apply-on-select
    }

    void steamUpdateOfSelectedReappliesActive() {
        const int a = call("steam_pitcher_add", {{"name", "Sel"}, {"temperatureC", 128.0}})["selectedIndex"].toInt();
        QCOMPARE(m_settings.brew()->steamTemperature(), 128.0);
        QVERIFY(call("steam_pitcher_update", {{"index", a}, {"temperatureC", 137.0}})["success"].toBool());
        QCOMPARE(m_settings.brew()->steamTemperature(), 137.0);  // re-applied because it's selected
    }

    void steamUpdateDisabledRejected() {
        const qsizetype before = m_settings.brew()->steamPitcherPresets().size();
        call("steam_pitcher_add", {{"name", "Off"}, {"disabled", true}});
        const qsizetype idx = m_settings.brew()->steamPitcherPresets().size() - 1;
        QVERIFY(idx >= before);
        QJsonObject r = call("steam_pitcher_update", {{"index", idx}, {"temperatureC", 150.0}});
        QVERIFY(!r["success"].toBool());
        QVERIFY(r.contains("error"));
    }

    void steamUpdateOutOfRangeErrors() {
        QJsonObject r = call("steam_pitcher_update", {{"index", 9999}, {"temperatureC", 150.0}});
        QVERIFY(!r["success"].toBool());
        QCOMPARE(r["error"].toString(), QString("index out of range"));
    }

    void steamAddRequiresName() {
        QJsonObject r = call("steam_pitcher_add", {{"name", "   "}});
        QVERIFY(r.contains("error"));
    }

    // --- water ---------------------------------------------------------------

    void waterAddConvertsFlowAndReportsBack() {
        QJsonObject add = call("water_vessel_add",
            {{"name", "WConv"}, {"volumeMl", 150}, {"flowMlPerSec", 4.0}, {"temperatureC", 79.0}, {"mode", "volume"}});
        QVERIFY(add["success"].toBool());
        const int idx = add["selectedIndex"].toInt();

        QCOMPARE(m_settings.brew()->getWaterVesselPreset(idx)["flowRate"].toInt(), 40);  // tenths of mL/s
        QJsonObject p = call("water_vessel_list", {})["presets"].toArray()[idx].toObject();
        QCOMPARE(p["flowMlPerSec"].toDouble(), 4.0);
        QCOMPARE(p["volumeMl"].toInt(), 150);
        QCOMPARE(p["mode"].toString(), QString("volume"));
        QCOMPARE(p["temperatureC"].toDouble(), 79.0);
    }

    void waterPartialUpdatePreservesOtherFields() {
        QJsonObject add = call("water_vessel_add",
            {{"name", "WEdit"}, {"volumeMl", 250}, {"flowMlPerSec", 3.0}, {"temperatureC", 86.0}});
        const int idx = add["selectedIndex"].toInt();

        QVERIFY(call("water_vessel_update", {{"index", idx}, {"temperatureC", 90.0}})["success"].toBool());

        QVariantMap p = m_settings.brew()->getWaterVesselPreset(idx);
        QCOMPARE(p["temperature"].toDouble(), 90.0);
        QCOMPARE(p["volume"].toInt(), 250);    // unchanged
        QCOMPARE(p["flowRate"].toInt(), 30);   // unchanged (3.0 mL/s)
    }

    void waterSelectAppliesToActive() {
        const int a = call("water_vessel_add", {{"name", "WA"}, {"temperatureC", 74.0}, {"volumeMl", 120}})["selectedIndex"].toInt();
        call("water_vessel_add", {{"name", "WB"}, {"temperatureC", 95.0}});  // selected, active = 95
        QCOMPARE(m_settings.brew()->waterTemperature(), 95.0);

        QVERIFY(call("water_vessel_select", {{"index", a}}, /*control*/ 1)["success"].toBool());
        QCOMPARE(m_settings.brew()->waterTemperature(), 74.0);
        QCOMPARE(m_settings.brew()->waterVolume(), 120);
    }

    void waterDeleteOutOfRangeErrors() {
        QJsonObject r = call("water_vessel_delete", {{"index", 9999}});
        QVERIFY(!r["success"].toBool());
        QCOMPARE(r["error"].toString(), QString("index out of range"));
    }
};

QTEST_GUILESS_MAIN(tst_McpToolsPresets)
#include "tst_mcptools_presets.moc"
