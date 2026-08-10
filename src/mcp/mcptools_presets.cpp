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
    const bool disabled = m.value("disabled").toBool();
    if (disabled) {
        // The built-in "Heater off" entry: no duration/flow/temperature, because
        // it does not steam. It carries no STORED name either — the app's views
        // translate the label — so name it here rather than handing an MCP client
        // a nameless row it cannot refer to or explain. English on purpose: this
        // is a protocol surface, not UI text.
        o["name"] = QStringLiteral("Heater off");
        o["builtin"] = true;
        o["disabled"] = true;
        return o;
    }
    o["name"] = m.value("name").toString();
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

// A missing `index` is NOT index 0, and the difference is a deleted preset.
//
// Each of these verbs used to be its own tool carrying {"required": ["index"]}, and
// that schema line was the only guard: the handlers read args.value("index").toInt(),
// which returns 0 for an absent key, and 0 passes indexInRange(). Merging the family
// moved `required` up to the tool, where it can only say "action" — JSON Schema
// cannot express a per-verb requirement without allOf/if-then. So the check moves
// into the handler, where it should have been anyway. Without it, {"action":"delete"}
// deletes the first preset and answers success, and recipes snapshot pitchers BY NAME,
// so the wrong delete quietly breaks every recipe that referenced it.
bool missingIndex(const QJsonObject& args, const QString& action, QJsonObject& result)
{
    if (args.contains("index")) return false;
    result["error"] = QStringLiteral("index is required for action=%1 (get it from action=list)")
                          .arg(action);
    return true;
}

} // namespace

// machineState is gone from this signature: resolving the milk on the scale
// moved into MainController::selectSteamPitcher with the rest of the shared
// select, so this file no longer needs a machine at all.
void registerPresetsTools(McpToolRegistry* registry, Settings* settings, MainController* mainController)
{
    // Apply a steam pitcher's stored parameters to the active steam settings and
    // push them to the machine — the non-UI equivalent of selecting the pitcher
    // on the Steam page. Used by select and by add (which auto-selects).
    // ONE function does this, and it is the same one the idle pill row, the
    // Steam widget popup, the Steam page and recipe activation call. This lambda
    // used to re-implement its tail — resolve the milk, apply the values,
    // dispatch on the result — and that copy drifted the moment the shared one
    // learned to clear the transient veto when a real pitcher is picked. The
    // symptom on a live machine: selecting "Heater off" over MCP and then
    // selecting a real pitcher left the boiler cold with nothing to explain it.
    auto applySteamPitcher = [mainController](int index) {
        if (mainController)
            mainController->selectSteamPitcher(index);
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
    // Steam pitcher presets — one tool, five verbs
    //
    // The five `steam_pitcher_*` tools this replaces advertised a `confirmed`
    // argument that nothing enforced: none of them was ever in McpServer's
    // confirmation list, so the property was decoration. It is gone rather than
    // honoured — these are small, re-creatable presets, and the confirmation net
    // is worth more when it is not spent on routine edits.
    // ---------------------------------------------------------------------

    const QVector<McpToolAction> pitcherActions{
        McpRegistryHelpers::syncAction("list", "read",
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
        }),
        McpRegistryHelpers::syncAction("add", "settings",
        [settings, applySteamPitcher](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!settings) { result["error"] = "Settings unavailable"; return result; }
            const QString name = args.value("name").toString().trimmed();
            if (name.isEmpty()) { result["error"] = "name is required"; return result; }
            // Presets are addressed by name downstream (recipes snapshot the
            // pitcher by name), so a duplicate is refused here with a message
            // rather than silently dropped by the setter, which returns void.
            if (settings->brew()->steamPitcherNameTaken(name)) {
                result["error"] = "a steam pitcher named \"" + name + "\" already exists";
                return result;
            }
            // "Off" pitchers are no longer user-creatable: every install has one
            // built-in "Heater off" entry. Say so rather than silently creating a
            // normal pitcher, which would leave the caller believing it made a
            // heater switch.
            if (args.value("disabled").toBool()) {
                result["error"] = "heater-off pitchers cannot be created — every machine has a "
                                  "built-in \"Heater off\" entry in the pitcher list; select that instead";
                return result;
            }
            const int duration = args.contains("durationSec") ? args.value("durationSec").toInt() : 30;
            const int flow = args.contains("flowMlPerSec")
                ? static_cast<int>(std::lround(args.value("flowMlPerSec").toDouble() * kSteamFlowScale)) : 150;
            const double temp = args.contains("temperatureC")
                ? args.value("temperatureC").toDouble() : settings->brew()->steamTemperature();
            settings->brew()->addSteamPitcherPreset(name, duration, flow, temp);
            const int newIndex = settings->brew()->steamPitcherCount() - 1;
            settings->brew()->setSelectedSteamCup(newIndex);   // see `select`
            applySteamPitcher(newIndex);
            result["success"] = true;
            result["selectedIndex"] = newIndex;
            return result;
        }),
        McpRegistryHelpers::syncAction("update", "settings",
        [settings, applySteamPitcher](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!settings) { result["error"] = "Settings unavailable"; return result; }
            if (missingIndex(args, QStringLiteral("update"), result)) return result;
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
            if (settings->brew()->steamPitcherNameTaken(name, index)) {
                result["error"] = "a steam pitcher named \"" + name + "\" already exists";
                return result;
            }
            settings->brew()->updateSteamPitcherPreset(index, name, duration, flow, temp);
            // If we edited the active pitcher, re-apply so the live steam settings
            // (and the machine) reflect the change immediately.
            if (index == settings->brew()->selectedSteamPitcher()) applySteamPitcher(index);
            result["success"] = true;
            return result;
        }),
        McpRegistryHelpers::syncAction("delete", "settings",
        [settings](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!settings) { result["error"] = "Settings unavailable"; return result; }
            if (missingIndex(args, QStringLiteral("delete"), result)) return result;
            const int index = args.value("index").toInt();
            if (!indexInRange(index, settings->brew()->steamPitcherCount())) {
                result["error"] = "index out of range"; return result;
            }
            settings->brew()->removeSteamPitcherPreset(index);
            result["success"] = true;
            return result;
        }),
        // Switching the active pitcher is control, not settings — same call the
        // Steam page makes when the user taps a pitcher.
        McpRegistryHelpers::syncAction("select", "control",
        [settings, applySteamPitcher](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!settings) { result["error"] = "Settings unavailable"; return result; }
            if (missingIndex(args, QStringLiteral("select"), result)) return result;
            const int index = args.value("index").toInt();
            // SELECT accepts the built-in "Heater off" entry; the mutating verbs
            // above do not. `list` returns it one past the last real preset, so
            // a client that reads a row and selects it by position must be able
            // to select THAT row — the count-only check listed an entry it then
            // refused, which is a surface that contradicts itself. The sentinel
            // is accepted too, for callers that know it.
            const bool builtInHeaterOff = settings->brew()->isHeaterOffPitcher(index);
            if (!builtInHeaterOff && !indexInRange(index, settings->brew()->steamPitcherCount())) {
                result["error"] = "index out of range"; return result;
            }
            const int selection = builtInHeaterOff ? SettingsBrew::HeaterOffPitcherIndex : index;
            // Store the selection here as well as inside the shared select. The
            // write is idempotent (setSelectedSteamCup early-returns when
            // unchanged), and it keeps the tool's reported selectedIndex TRUE in
            // a context with no controller — headless, or a test — rather than
            // reporting a selection it merely delegated. What must not be
            // duplicated is the RULE (which values, which vetoes); a single
            // idempotent assignment is not that.
            settings->brew()->setSelectedSteamCup(selection);
            applySteamPitcher(selection);
            result["success"] = true;
            result["selectedIndex"] = selection;
            return result;
        }),
    };

    registry->registerActionTool(
        "steam_pitcher",
        "Steam pitcher presets: list, add, update, delete, select. A preset carries name, "
        "durationSec, flowMlPerSec and temperatureC; `select` makes one active and sends it to "
        "the machine. Indexes come from `list` and shift after a delete. Field meanings, the "
        "\"Off\" preset, and pitcherWeightG/calibMilkG: get_agent_file topic \"steam_pitcher\".",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"index", QJsonObject{{"type", "integer"}, {"description", "Preset index from `list` (update, delete, select)"}}},
            {"name", QJsonObject{{"type", "string"}, {"description", "Display name (required for add)"}}},
            {"durationSec", QJsonObject{{"type", "integer"}, {"description", "Steam duration in seconds (add default 30)"}}},
            {"flowMlPerSec", QJsonObject{{"type", "number"}, {"description", "Steam flow rate in mL/s (add default 1.5)"}}},
            {"temperatureC", QJsonObject{{"type", "number"}, {"description", "Steam temperature in °C (add defaults to the global steam temperature)"}}},
            {"disabled", QJsonObject{{"type", "boolean"}, {"description", "add only: create an \"Off\" preset (heater off); other fields ignored"}}}
        }}},
        pitcherActions);

    // ---------------------------------------------------------------------
    // Hot water vessel presets — same five verbs, same shape
    // ---------------------------------------------------------------------

    const QVector<McpToolAction> vesselActions{
        McpRegistryHelpers::syncAction("list", "read",
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
        }),
        McpRegistryHelpers::syncAction("add", "settings",
        [settings, applyWaterVessel](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!settings) { result["error"] = "Settings unavailable"; return result; }
            const QString name = args.value("name").toString().trimmed();
            if (name.isEmpty()) { result["error"] = "name is required"; return result; }
            if (settings->brew()->waterVesselNameTaken(name)) {
                result["error"] = "a water vessel named \"" + name + "\" already exists";
                return result;
            }
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
        }),
        McpRegistryHelpers::syncAction("update", "settings",
        [settings, applyWaterVessel](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!settings) { result["error"] = "Settings unavailable"; return result; }
            if (missingIndex(args, QStringLiteral("update"), result)) return result;
            const int index = args.value("index").toInt();
            const QVariantList list = settings->brew()->waterVesselPresets();
            if (!indexInRange(index, list.size())) { result["error"] = "index out of range"; return result; }
            const QVariantMap existing = settings->brew()->getWaterVesselPreset(index);
            // A blank name is rejected here for the same reason action=add
            // rejects it: recipes snapshot the vessel BY NAME, so a nameless
            // preset is one nothing can refer to afterwards.
            const QString name = args.contains("name") ? args.value("name").toString().trimmed()
                                                       : existing.value("name").toString();
            if (name.isEmpty()) { result["error"] = "name cannot be empty"; return result; }
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
            if (settings->brew()->waterVesselNameTaken(name, index)) {
                result["error"] = "a water vessel named \"" + name + "\" already exists";
                return result;
            }
            settings->brew()->updateWaterVesselPreset(index, name, volume, mode, flowRate, temp);
            // If we edited the active vessel, re-apply so the live hot water settings
            // (and the machine) reflect the change immediately.
            if (index == settings->brew()->selectedWaterVessel()) applyWaterVessel(index);
            result["success"] = true;
            return result;
        }),
        McpRegistryHelpers::syncAction("delete", "settings",
        [settings](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!settings) { result["error"] = "Settings unavailable"; return result; }
            if (missingIndex(args, QStringLiteral("delete"), result)) return result;
            const int index = args.value("index").toInt();
            if (!indexInRange(index, settings->brew()->waterVesselPresets().size())) {
                result["error"] = "index out of range"; return result;
            }
            settings->brew()->removeWaterVesselPreset(index);
            result["success"] = true;
            return result;
        }),
        McpRegistryHelpers::syncAction("select", "control",
        [settings, applyWaterVessel](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!settings) { result["error"] = "Settings unavailable"; return result; }
            if (missingIndex(args, QStringLiteral("select"), result)) return result;
            const int index = args.value("index").toInt();
            if (!indexInRange(index, settings->brew()->waterVesselPresets().size())) {
                result["error"] = "index out of range"; return result;
            }
            settings->brew()->setSelectedWaterCup(index);
            applyWaterVessel(index);
            result["success"] = true;
            result["selectedIndex"] = index;
            return result;
        }),
    };

    registry->registerActionTool(
        "water_vessel",
        "Hot water vessel presets: list, add, update, delete, select. A preset carries name, "
        "volumeMl, mode (\"weight\" or \"volume\"), flowMlPerSec and temperatureC; `select` makes "
        "one active and sends it to the machine. Indexes come from `list` and shift after a "
        "delete. Field meanings: get_agent_file topic \"water_vessel\".",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"index", QJsonObject{{"type", "integer"}, {"description", "Preset index from `list` (update, delete, select)"}}},
            {"name", QJsonObject{{"type", "string"}, {"description", "Display name (required for add)"}}},
            {"volumeMl", QJsonObject{{"type", "integer"}, {"description", "Target volume in mL (add default 200)"}}},
            {"mode", QJsonObject{{"type", "string"}, {"description", "\"weight\" or \"volume\" (add default weight)"}}},
            {"flowMlPerSec", QJsonObject{{"type", "number"}, {"description", "Hot water flow rate in mL/s (add default 4.0)"}}},
            {"temperatureC", QJsonObject{{"type", "number"}, {"description", "Hot water temperature in °C (add defaults to the global hot water temperature)"}}}
        }}},
        vesselActions);
}
