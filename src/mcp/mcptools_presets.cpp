// MCP tools for steam-pitcher and hot-water-vessel preset CRUD + selection.
//
// Units follow the MCP data conventions (see docs/CLAUDE_MD/MCP_SERVER.md):
// temperatures in °C, flow in mL/s, durations in seconds, volumes in mL,
// weights in grams. The underlying SettingsBrew store keeps steam flow in
// hundredths of mL/s and water flow rate in tenths of mL/s; the conversions
// live here so the MCP surface stays human-readable.

#include "mcptoolregistry.h"
#include "../core/settings.h"
#include "../core/settings_brew.h"
#include "../core/settings_hardware.h"
#include "../controllers/maincontroller.h"
#include "../machine/machinestate.h"
#include "../ble/scaledevice.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QVariantMap>
#include <cmath>

namespace {

// Stored-unit <-> mL/s conversions.
constexpr double kSteamFlowScale = 100.0;  // steam preset "flow" is hundredths of mL/s
constexpr double kWaterFlowScale = 10.0;   // water preset "flowRate" is tenths of mL/s

QJsonObject steamPitcherToJson(const QVariantMap& m, double globalTempC)
{
    QJsonObject o;
    o["name"] = m.value("name").toString();
    const bool disabled = m.value("disabled").toBool();
    if (disabled) {
        // "Off" preset — heater stays off, so no duration/flow/temperature.
        o["disabled"] = true;
        return o;
    }
    o["durationSec"] = m.value("duration").toInt();
    o["flowMlPerSec"] = m.value("flow").toDouble() / kSteamFlowScale;
    o["temperatureC"] = m.contains("temperature") ? m.value("temperature").toDouble() : globalTempC;
    if (m.contains("pitcherWeightG")) o["pitcherWeightG"] = m.value("pitcherWeightG").toDouble();
    if (m.contains("calibMilkG")) o["calibMilkG"] = m.value("calibMilkG").toDouble();
    return o;
}

QJsonObject waterVesselToJson(const QVariantMap& m, double globalTempC)
{
    QJsonObject o;
    o["name"] = m.value("name").toString();
    o["volumeMl"] = m.value("volume").toInt();
    o["mode"] = m.contains("mode") ? m.value("mode").toString() : QStringLiteral("weight");
    o["flowMlPerSec"] = (m.contains("flowRate") ? m.value("flowRate").toDouble() : 40.0) / kWaterFlowScale;
    o["temperatureC"] = m.contains("temperature") ? m.value("temperature").toDouble() : globalTempC;
    return o;
}

bool indexInRange(int index, qsizetype count)
{
    return index >= 0 && index < static_cast<int>(count);
}

const QJsonObject kConfirmedProp{
    {"confirmed", QJsonObject{{"type", "boolean"},
        {"description", "Set to true after the user confirms this action in chat"}}}};

} // namespace

void registerPresetsTools(McpToolRegistry* registry, Settings* settings, MainController* mainController,
                          MachineState* machineState)
{
    // Apply a steam pitcher's stored parameters to the active steam settings and
    // push them to the machine — the non-UI equivalent of selecting the pitcher
    // on the Steam page. Used by select and by add (which auto-selects).
    auto applySteamPitcher = [settings, mainController, machineState](int index) {
        if (!settings) return;
        const QVariantMap p = settings->brew()->getSteamPitcherPreset(index);
        if (p.isEmpty()) return;  // out-of-range index returns an empty map
        if (p.value("disabled").toBool()) {
            if (mainController) mainController->turnOffSteamHeater();
            return;
        }
        // Weight-scaled steaming: resolve scaled-or-base through the shared SettingsBrew
        // helper (same as the UI preset taps) so an MCP pitcher select can't program an
        // unscaled duration while the steam plan shows a scaled one. Net milk on the
        // scale now; 0 (base duration) when no weighing scale is connected.
        double milk = 0.0;
        if (machineState && machineState->scale() && !machineState->scale()->isFlowScale())
            milk = settings->brew()->netMilkForPitcher(index, machineState->scaleWeight());
        settings->brew()->setSteamTimeout(settings->brew()->effectiveSteamDurationSec(index, milk));
        settings->brew()->setSteamFlow(p.value("flow").toInt());
        settings->brew()->setSteamTemperature(p.contains("temperature")
            ? p.value("temperature").toDouble() : settings->brew()->steamTemperature());
        if (mainController) mainController->applySteamSettings();
    };

    // Likewise for a hot water vessel.
    auto applyWaterVessel = [settings, mainController](int index) {
        if (!settings) return;
        const QVariantMap p = settings->brew()->getWaterVesselPreset(index);
        if (p.isEmpty()) return;  // out-of-range index returns an empty map
        settings->brew()->setWaterVolume(p.value("volume").toInt());
        settings->brew()->setWaterVolumeMode(p.contains("mode") ? p.value("mode").toString()
                                                                 : QStringLiteral("weight"));
        settings->hardware()->setHotWaterFlowRate(p.contains("flowRate") ? p.value("flowRate").toInt() : 40);
        settings->brew()->setWaterTemperature(p.contains("temperature")
            ? p.value("temperature").toDouble() : settings->brew()->waterTemperature());
        if (mainController) mainController->applyHotWaterSettings();
    };

    // ---------------------------------------------------------------------
    // Steam pitcher presets
    // ---------------------------------------------------------------------

    registry->registerTool(
        "steam_pitcher_list",
        "List all steam pitcher presets and which one is currently selected. Each preset has a "
        "name, durationSec, flowMlPerSec and temperatureC (per-pitcher steam temperature). "
        "Disabled \"Off\" presets only carry name + disabled. pitcherWeightG is the saved "
        "empty-pitcher weight (for net-milk capture). calibMilkG is a legacy per-pitcher "
        "reference weight, no longer used for scaling — weight-timed steaming now uses a single "
        "global rate (steamSecondsPerGram in settings), not per-pitcher calibration.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [settings](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!settings) { result["error"] = "Settings unavailable"; return result; }
            const double globalTemp = settings->brew()->steamTemperature();
            QJsonArray presets;
            const QVariantList list = settings->brew()->steamPitcherPresets();
            for (const QVariant& v : list)
                presets.append(steamPitcherToJson(v.toMap(), globalTemp));
            result["presets"] = presets;
            result["selectedIndex"] = settings->brew()->selectedSteamPitcher();
            result["count"] = static_cast<int>(list.size());
            return result;
        },
        "read");

    registry->registerTool(
        "steam_pitcher_add",
        "Add a new steam pitcher preset and select it. Provide a name; durationSec, flowMlPerSec "
        "and temperatureC are optional (default 30s / 1.5 mL/s; temperature defaults to the current "
        "global steam temperature). Set disabled=true to add an \"Off\" preset that turns the steam "
        "heater off (other fields are ignored).",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"name", QJsonObject{{"type", "string"}, {"description", "Display name for the pitcher preset"}}},
            {"durationSec", QJsonObject{{"type", "integer"}, {"description", "Steam duration in seconds (default 30)"}}},
            {"flowMlPerSec", QJsonObject{{"type", "number"}, {"description", "Steam flow rate in mL/s (default 1.5)"}}},
            {"temperatureC", QJsonObject{{"type", "number"}, {"description", "Steam temperature in °C (defaults to the current global steam temperature)"}}},
            {"disabled", QJsonObject{{"type", "boolean"}, {"description", "Add an \"Off\" preset (heater off). Other fields ignored."}}},
            {"confirmed", kConfirmedProp.value("confirmed")}
        }}, {"required", QJsonArray{"name"}}},
        [settings, applySteamPitcher](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!settings) { result["error"] = "Settings unavailable"; return result; }
            const QString name = args.value("name").toString().trimmed();
            if (name.isEmpty()) { result["error"] = "name is required"; return result; }
            if (args.value("disabled").toBool()) {
                settings->brew()->addSteamPitcherPresetDisabled(name);
            } else {
                const int duration = args.contains("durationSec") ? args.value("durationSec").toInt() : 30;
                const int flow = args.contains("flowMlPerSec")
                    ? static_cast<int>(std::lround(args.value("flowMlPerSec").toDouble() * kSteamFlowScale)) : 150;
                const double temp = args.contains("temperatureC")
                    ? args.value("temperatureC").toDouble() : settings->brew()->steamTemperature();
                settings->brew()->addSteamPitcherPreset(name, duration, flow, temp);
            }
            const int newIndex = static_cast<int>(settings->brew()->steamPitcherPresets().size()) - 1;
            settings->brew()->setSelectedSteamCup(newIndex);
            applySteamPitcher(newIndex);
            result["success"] = true;
            result["selectedIndex"] = newIndex;
            return result;
        },
        "settings");

    registry->registerTool(
        "steam_pitcher_update",
        "Update an existing steam pitcher preset by index. Only the fields you pass are changed; "
        "the rest keep their current values. Disabled \"Off\" presets cannot be edited — delete "
        "and re-add instead.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"index", QJsonObject{{"type", "integer"}, {"description", "Index of the preset (from steam_pitcher_list)"}}},
            {"name", QJsonObject{{"type", "string"}, {"description", "New name"}}},
            {"durationSec", QJsonObject{{"type", "integer"}, {"description", "New steam duration in seconds"}}},
            {"flowMlPerSec", QJsonObject{{"type", "number"}, {"description", "New steam flow rate in mL/s"}}},
            {"temperatureC", QJsonObject{{"type", "number"}, {"description", "New steam temperature in °C"}}},
            {"confirmed", kConfirmedProp.value("confirmed")}
        }}, {"required", QJsonArray{"index"}}},
        [settings, applySteamPitcher](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!settings) { result["error"] = "Settings unavailable"; return result; }
            const int index = args.value("index").toInt();
            const QVariantList list = settings->brew()->steamPitcherPresets();
            if (!indexInRange(index, list.size())) { result["error"] = "index out of range"; return result; }
            const QVariantMap existing = settings->brew()->getSteamPitcherPreset(index);
            if (existing.value("disabled").toBool()) {
                result["error"] = "Cannot edit a disabled (Off) pitcher; delete and re-add it";
                return result;
            }
            const QString name = args.contains("name") ? args.value("name").toString() : existing.value("name").toString();
            const int duration = args.contains("durationSec") ? args.value("durationSec").toInt() : existing.value("duration").toInt();
            const int flow = args.contains("flowMlPerSec")
                ? static_cast<int>(std::lround(args.value("flowMlPerSec").toDouble() * kSteamFlowScale))
                : existing.value("flow").toInt();
            const double temp = args.contains("temperatureC")
                ? args.value("temperatureC").toDouble()
                : (existing.contains("temperature") ? existing.value("temperature").toDouble()
                                                     : settings->brew()->steamTemperature());
            settings->brew()->updateSteamPitcherPreset(index, name, duration, flow, temp);
            // If we edited the active pitcher, re-apply so the live steam settings
            // (and the machine) reflect the change immediately.
            if (index == settings->brew()->selectedSteamPitcher()) applySteamPitcher(index);
            result["success"] = true;
            return result;
        },
        "settings");

    registry->registerTool(
        "steam_pitcher_delete",
        "Delete a steam pitcher preset by index.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"index", QJsonObject{{"type", "integer"}, {"description", "Index of the preset to delete"}}},
            {"confirmed", kConfirmedProp.value("confirmed")}
        }}, {"required", QJsonArray{"index"}}},
        [settings](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!settings) { result["error"] = "Settings unavailable"; return result; }
            const int index = args.value("index").toInt();
            if (!indexInRange(index, settings->brew()->steamPitcherPresets().size())) {
                result["error"] = "index out of range"; return result;
            }
            settings->brew()->removeSteamPitcherPreset(index);
            result["success"] = true;
            return result;
        },
        "settings");

    registry->registerTool(
        "steam_pitcher_select",
        "Select (switch to) a steam pitcher preset by index. Its duration, flow and per-pitcher "
        "temperature become active and are sent to the machine.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"index", QJsonObject{{"type", "integer"}, {"description", "Index of the preset to select"}}},
            {"confirmed", kConfirmedProp.value("confirmed")}
        }}, {"required", QJsonArray{"index"}}},
        [settings, applySteamPitcher](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!settings) { result["error"] = "Settings unavailable"; return result; }
            const int index = args.value("index").toInt();
            if (!indexInRange(index, settings->brew()->steamPitcherPresets().size())) {
                result["error"] = "index out of range"; return result;
            }
            settings->brew()->setSelectedSteamCup(index);
            applySteamPitcher(index);
            result["success"] = true;
            result["selectedIndex"] = index;
            return result;
        },
        "control");  // switching the active pitcher, like bag_select / equipment_select

    // ---------------------------------------------------------------------
    // Hot water vessel presets
    // ---------------------------------------------------------------------

    registry->registerTool(
        "water_vessel_list",
        "List all hot water vessel presets and which one is currently selected. Each preset has a "
        "name, volumeMl, mode (\"weight\" or \"volume\"), flowMlPerSec and temperatureC "
        "(per-vessel hot water temperature).",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [settings](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!settings) { result["error"] = "Settings unavailable"; return result; }
            const double globalTemp = settings->brew()->waterTemperature();
            QJsonArray presets;
            const QVariantList list = settings->brew()->waterVesselPresets();
            for (const QVariant& v : list)
                presets.append(waterVesselToJson(v.toMap(), globalTemp));
            result["presets"] = presets;
            result["selectedIndex"] = settings->brew()->selectedWaterVessel();
            result["count"] = static_cast<int>(list.size());
            return result;
        },
        "read");

    registry->registerTool(
        "water_vessel_add",
        "Add a new hot water vessel preset and select it. Provide a name; volumeMl, mode, "
        "flowMlPerSec and temperatureC are optional (default 200 mL / weight / 4.0 mL/s; "
        "temperature defaults to the current global hot water temperature).",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"name", QJsonObject{{"type", "string"}, {"description", "Display name for the vessel preset"}}},
            {"volumeMl", QJsonObject{{"type", "integer"}, {"description", "Target volume in mL (default 200)"}}},
            {"mode", QJsonObject{{"type", "string"}, {"description", "\"weight\" or \"volume\" (default weight)"}}},
            {"flowMlPerSec", QJsonObject{{"type", "number"}, {"description", "Hot water flow rate in mL/s (default 4.0)"}}},
            {"temperatureC", QJsonObject{{"type", "number"}, {"description", "Hot water temperature in °C (defaults to the current global hot water temperature)"}}},
            {"confirmed", kConfirmedProp.value("confirmed")}
        }}, {"required", QJsonArray{"name"}}},
        [settings, applyWaterVessel](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!settings) { result["error"] = "Settings unavailable"; return result; }
            const QString name = args.value("name").toString().trimmed();
            if (name.isEmpty()) { result["error"] = "name is required"; return result; }
            const int volume = args.contains("volumeMl") ? args.value("volumeMl").toInt() : 200;
            const QString mode = args.contains("mode") ? args.value("mode").toString() : QStringLiteral("weight");
            const int flowRate = args.contains("flowMlPerSec")
                ? static_cast<int>(std::lround(args.value("flowMlPerSec").toDouble() * kWaterFlowScale)) : 40;
            const double temp = args.contains("temperatureC")
                ? args.value("temperatureC").toDouble() : settings->brew()->waterTemperature();
            settings->brew()->addWaterVesselPreset(name, volume, mode, flowRate, temp);
            const int newIndex = static_cast<int>(settings->brew()->waterVesselPresets().size()) - 1;
            settings->brew()->setSelectedWaterCup(newIndex);
            applyWaterVessel(newIndex);
            result["success"] = true;
            result["selectedIndex"] = newIndex;
            return result;
        },
        "settings");

    registry->registerTool(
        "water_vessel_update",
        "Update an existing hot water vessel preset by index. Only the fields you pass are changed; "
        "the rest keep their current values.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"index", QJsonObject{{"type", "integer"}, {"description", "Index of the preset (from water_vessel_list)"}}},
            {"name", QJsonObject{{"type", "string"}, {"description", "New name"}}},
            {"volumeMl", QJsonObject{{"type", "integer"}, {"description", "New target volume in mL"}}},
            {"mode", QJsonObject{{"type", "string"}, {"description", "\"weight\" or \"volume\""}}},
            {"flowMlPerSec", QJsonObject{{"type", "number"}, {"description", "New hot water flow rate in mL/s"}}},
            {"temperatureC", QJsonObject{{"type", "number"}, {"description", "New hot water temperature in °C"}}},
            {"confirmed", kConfirmedProp.value("confirmed")}
        }}, {"required", QJsonArray{"index"}}},
        [settings, applyWaterVessel](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!settings) { result["error"] = "Settings unavailable"; return result; }
            const int index = args.value("index").toInt();
            const QVariantList list = settings->brew()->waterVesselPresets();
            if (!indexInRange(index, list.size())) { result["error"] = "index out of range"; return result; }
            const QVariantMap existing = settings->brew()->getWaterVesselPreset(index);
            const QString name = args.contains("name") ? args.value("name").toString() : existing.value("name").toString();
            const int volume = args.contains("volumeMl") ? args.value("volumeMl").toInt() : existing.value("volume").toInt();
            const QString mode = args.contains("mode") ? args.value("mode").toString()
                : (existing.contains("mode") ? existing.value("mode").toString() : QStringLiteral("weight"));
            const int flowRate = args.contains("flowMlPerSec")
                ? static_cast<int>(std::lround(args.value("flowMlPerSec").toDouble() * kWaterFlowScale))
                : (existing.contains("flowRate") ? existing.value("flowRate").toInt() : 40);
            const double temp = args.contains("temperatureC")
                ? args.value("temperatureC").toDouble()
                : (existing.contains("temperature") ? existing.value("temperature").toDouble()
                                                     : settings->brew()->waterTemperature());
            settings->brew()->updateWaterVesselPreset(index, name, volume, mode, flowRate, temp);
            // If we edited the active vessel, re-apply so the live hot water settings
            // (and the machine) reflect the change immediately.
            if (index == settings->brew()->selectedWaterVessel()) applyWaterVessel(index);
            result["success"] = true;
            return result;
        },
        "settings");

    registry->registerTool(
        "water_vessel_delete",
        "Delete a hot water vessel preset by index.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"index", QJsonObject{{"type", "integer"}, {"description", "Index of the preset to delete"}}},
            {"confirmed", kConfirmedProp.value("confirmed")}
        }}, {"required", QJsonArray{"index"}}},
        [settings](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!settings) { result["error"] = "Settings unavailable"; return result; }
            const int index = args.value("index").toInt();
            if (!indexInRange(index, settings->brew()->waterVesselPresets().size())) {
                result["error"] = "index out of range"; return result;
            }
            settings->brew()->removeWaterVesselPreset(index);
            result["success"] = true;
            return result;
        },
        "settings");

    registry->registerTool(
        "water_vessel_select",
        "Select (switch to) a hot water vessel preset by index. Its volume, mode, flow and "
        "per-vessel temperature become active and are sent to the machine.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"index", QJsonObject{{"type", "integer"}, {"description", "Index of the preset to select"}}},
            {"confirmed", kConfirmedProp.value("confirmed")}
        }}, {"required", QJsonArray{"index"}}},
        [settings, applyWaterVessel](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!settings) { result["error"] = "Settings unavailable"; return result; }
            const int index = args.value("index").toInt();
            if (!indexInRange(index, settings->brew()->waterVesselPresets().size())) {
                result["error"] = "index out of range"; return result;
            }
            settings->brew()->setSelectedWaterCup(index);
            applyWaterVessel(index);
            result["success"] = true;
            result["selectedIndex"] = index;
            return result;
        },
        "control");  // switching the active vessel, like bag_select / equipment_select
}
