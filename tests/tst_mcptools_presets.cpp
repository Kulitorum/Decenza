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
void registerPresetsTools(McpToolRegistry* registry, Settings* settings, MainController* mainController);

// Selecting a pitcher is ONE function shared with the idle pill row, the Steam
// widget popup, the Steam page and recipe activation; the MCP tool calls it
// rather than re-implementing its tail, which is what let the two drift. This
// file links no MainController, so the stub makes select a no-op here — and the
// rule it dispatches (which values a pitcher writes, and what the built-in
// entry skips) is asserted directly against SettingsBrew in tst_settings, where
// it lives and where no stub can hollow it out.
void MainController::selectSteamPitcher(int, double) {}

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

    // Merged tools dispatch through the async path even when every verb is
    // synchronous (see registerActionTool), so the helper takes both routes and
    // the sync-in-async verbs answer inline.
    QJsonObject call(const QString& name, const QJsonObject& args, int accessLevel = 2) {
        QString err;
        QJsonObject r;
        if (m_registry.isAsyncTool(name))
            m_registry.callAsyncTool(name, args, accessLevel, err,
                                     [&r](QJsonObject out) { r = out; });
        else
            r = m_registry.callTool(name, args, accessLevel, err);
        if (!err.isEmpty()) r.insert("callError", err);
        return r;
    }

private slots:
    void initTestCase() {
        // No MachineState in the harness: applySteamPitcher's milk read is
        // nullptr-guarded, so selects resolve to the preset's base duration.
        registerPresetsTools(&m_registry, &m_settings, nullptr);
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
        QJsonObject add = call("steam_pitcher", {{"action", "add"}, {"name", "Conv"}, {"durationSec", 28}, {"flowMlPerSec", 1.5}, {"temperatureC", 134.0}});
        QVERIFY(add["success"].toBool());
        const int idx = add["selectedIndex"].toInt();

        // Stored in hundredths of mL/s.
        QCOMPARE(m_settings.brew()->getSteamPitcherPreset(idx)["flow"].toInt(), 150);
        QCOMPARE(m_settings.brew()->getSteamPitcherPreset(idx)["temperature"].toDouble(), 134.0);

        // list reports mL/s.
        QJsonArray presets = call("steam_pitcher", {{"action", "list"}})["presets"].toArray();
        QJsonObject p = presets[idx].toObject();
        QCOMPARE(p["flowMlPerSec"].toDouble(), 1.5);
        QCOMPARE(p["durationSec"].toInt(), 28);
        QCOMPARE(p["temperatureC"].toDouble(), 134.0);
    }

    void steamPartialUpdatePreservesOtherFields() {
        QJsonObject add = call("steam_pitcher", {{"action", "add"}, {"name", "Edit"}, {"durationSec", 40}, {"flowMlPerSec", 1.2}, {"temperatureC", 145.0}});
        const int idx = add["selectedIndex"].toInt();

        // Change only the temperature.
        QVERIFY(call("steam_pitcher", {{"action", "update"}, {"index", idx}, {"temperatureC", 150.0}})["success"].toBool());

        QVariantMap p = m_settings.brew()->getSteamPitcherPreset(idx);
        QCOMPARE(p["temperature"].toDouble(), 150.0);
        QCOMPARE(p["duration"].toInt(), 40);   // unchanged
        QCOMPARE(p["flow"].toInt(), 120);      // unchanged (1.2 mL/s)
        QCOMPARE(p["name"].toString(), QString("Edit"));
    }

    // What SELECT still owns here is the access boundary and the index it
    // reports. Applying the pitcher's values is delegated to the shared
    // MainController::selectSteamPitcher — stubbed in this file — and the rule
    // itself is asserted against SettingsBrew in tst_settings. Asserting it
    // through this tool would only re-test the stub.
    void steamSelectIsControlLevelButEditingIsNot() {
        const int a = call("steam_pitcher", {{"action", "add"}, {"name", "A"}, {"temperatureC", 130.0}})["selectedIndex"].toInt();
        call("steam_pitcher", {{"action", "add"}, {"name", "B"}, {"temperatureC", 150.0}});

        QVERIFY(call("steam_pitcher", {{"action", "select"}, {"index", a}}, /*control*/ 1)["success"].toBool());
        // A Control-level client may switch pitchers but must not rewrite them.
        // Declaring add/update/delete as "control" would let a network client
        // edit the user's presets, and every other assertion here would stay green.
        QCOMPARE(call("steam_pitcher", {{"action", "delete"}, {"index", a}}, /*control*/ 1)["callError"].toString(),
                 QString("Access level insufficient"));
    }

    // The built-in "Heater off" entry is listed one past the last real preset
    // and must be SELECTABLE at that position — a surface that lists a row and
    // then refuses it contradicts itself. It is stored as the sentinel, never as
    // a position, so adding or deleting a pitcher cannot invalidate it.
    void steamSelectAcceptsTheBuiltInHeaterOffEntry() {
        const int builtIn = m_settings.brew()->steamPitcherCount();
        const QJsonObject r = call("steam_pitcher", {{"action", "select"}, {"index", builtIn}}, /*control*/ 1);
        QVERIFY(r["success"].toBool());
        QCOMPARE(r["selectedIndex"].toInt(), int(SettingsBrew::HeaterOffPitcherIndex));
        QCOMPARE(m_settings.brew()->selectedSteamPitcher(), int(SettingsBrew::HeaterOffPitcherIndex));

        // ...and one past THAT is still out of range.
        QCOMPARE(call("steam_pitcher", {{"action", "select"}, {"index", builtIn + 1}}, 1)["error"].toString(),
                 QString("index out of range"));
    }

    void steamUpdateOfSelectedReappliesActive() {
        const int a = call("steam_pitcher", {{"action", "add"}, {"name", "Sel"}, {"temperatureC", 128.0}})["selectedIndex"].toInt();
        QVERIFY(call("steam_pitcher", {{"action", "update"}, {"index", a}, {"temperatureC", 137.0}})["success"].toBool());
        QCOMPARE(m_settings.brew()->getSteamPitcherPreset(a)["temperature"].toDouble(), 137.0);
    }

    // Heater-off pitchers are no longer user-creatable: every machine has one
    // built-in "Heater off" entry. Refusing loudly matters more than refusing —
    // silently creating a NORMAL pitcher would leave the caller believing it had
    // made a heater switch, and it would sit in the list looking like one.
    void steamDisabledPresetsCannotBeCreated() {
        const int before = m_settings.brew()->steamPitcherCount();
        QJsonObject r = call("steam_pitcher", {{"action", "add"}, {"name", "Off"}, {"disabled", true}});
        QVERIFY(!r["success"].toBool());
        QVERIFY(r["error"].toString().contains("Heater off"));
        QCOMPARE(m_settings.brew()->steamPitcherCount(), before);
    }

    // The built-in entry is not in the stored array, so its display slot is one
    // past the last real preset. Addressing it must fail rather than fall through
    // onto a real pitcher.
    void steamBuiltInHeaterOffCannotBeUpdated() {
        const int builtInSlot = m_settings.brew()->steamPitcherCount();
        QJsonObject r = call("steam_pitcher",
                             {{"action", "update"}, {"index", builtInSlot}, {"temperatureC", 150.0}});
        QVERIFY(!r["success"].toBool());
        QVERIFY(r.contains("error"));
    }

    void steamUpdateOutOfRangeErrors() {
        QJsonObject r = call("steam_pitcher", {{"action", "update"}, {"index", 9999}, {"temperatureC", 150.0}});
        QVERIFY(!r["success"].toBool());
        QCOMPARE(r["error"].toString(), QString("index out of range"));
    }

    // A missing index is not index 0. The per-tool `required: ["index"]` that used to
    // guarantee this could not survive the merge — a merged tool's `required` can only
    // name `action` — so the guard moved into the handlers.
    void presetVerbsRequireAnIndexRatherThanDefaultingToZero() {
        const int a = call("steam_pitcher", {{"action", "add"}, {"name", "Keep"}})["selectedIndex"].toInt();
        QVERIFY(a >= 0);
        const int before = call("steam_pitcher", {{"action", "list"}})["count"].toInt();
        for (const char* verb : {"delete", "update", "select"}) {
            const QJsonObject r = call("steam_pitcher", {{"action", verb}});
            QVERIFY2(r["error"].toString().contains("index is required"), verb);
        }
        QCOMPARE(call("steam_pitcher", {{"action", "list"}})["count"].toInt(), before);
        QVERIFY(call("water_vessel", {{"action", "delete"}})["error"].toString().contains("index is required"));
    }

    void steamAddRequiresName() {
        QJsonObject r = call("steam_pitcher", {{"action", "add"}, {"name", "   "}});
        QVERIFY(r.contains("error"));
    }

    // --- water ---------------------------------------------------------------

    void waterAddConvertsFlowAndReportsBack() {
        QJsonObject add = call("water_vessel", {{"action", "add"}, {"name", "WConv"}, {"volumeMl", 150}, {"flowMlPerSec", 4.0}, {"temperatureC", 79.0}, {"mode", "volume"}});
        QVERIFY(add["success"].toBool());
        const int idx = add["selectedIndex"].toInt();

        QCOMPARE(m_settings.brew()->getWaterVesselPreset(idx)["flowRate"].toInt(), 40);  // tenths of mL/s
        QJsonObject p = call("water_vessel", {{"action", "list"}})["presets"].toArray()[idx].toObject();
        QCOMPARE(p["flowMlPerSec"].toDouble(), 4.0);
        QCOMPARE(p["volumeMl"].toInt(), 150);
        QCOMPARE(p["mode"].toString(), QString("volume"));
        QCOMPARE(p["temperatureC"].toDouble(), 79.0);
    }

    void waterPartialUpdatePreservesOtherFields() {
        QJsonObject add = call("water_vessel", {{"action", "add"}, {"name", "WEdit"}, {"volumeMl", 250}, {"flowMlPerSec", 3.0}, {"temperatureC", 86.0}});
        const int idx = add["selectedIndex"].toInt();

        QVERIFY(call("water_vessel", {{"action", "update"}, {"index", idx}, {"temperatureC", 90.0}})["success"].toBool());

        QVariantMap p = m_settings.brew()->getWaterVesselPreset(idx);
        QCOMPARE(p["temperature"].toDouble(), 90.0);
        QCOMPARE(p["volume"].toInt(), 250);    // unchanged
        QCOMPARE(p["flowRate"].toInt(), 30);   // unchanged (3.0 mL/s)
    }

    void waterSelectAppliesToActive() {
        const int a = call("water_vessel", {{"action", "add"}, {"name", "WA"}, {"temperatureC", 74.0}, {"volumeMl", 120}})["selectedIndex"].toInt();
        call("water_vessel", {{"action", "add"}, {"name", "WB"}, {"temperatureC", 95.0}});  // selected, active = 95
        QCOMPARE(m_settings.brew()->waterTemperature(), 95.0);

        QVERIFY(call("water_vessel", {{"action", "select"}, {"index", a}}, /*control*/ 1)["success"].toBool());
        QCOMPARE(m_settings.brew()->waterTemperature(), 74.0);
        QCOMPARE(m_settings.brew()->waterVolume(), 120);
    }

    void waterDeleteOutOfRangeErrors() {
        QJsonObject r = call("water_vessel", {{"action", "delete"}, {"index", 9999}});
        QVERIFY(!r["success"].toBool());
        QCOMPARE(r["error"].toString(), QString("index out of range"));
    }
};

QTEST_GUILESS_MAIN(tst_McpToolsPresets)
#include "tst_mcptools_presets.moc"
