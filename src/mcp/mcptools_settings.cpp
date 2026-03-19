#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../core/settings.h"

#include <QJsonObject>
#include <QJsonArray>

void registerSettingsReadTools(McpToolRegistry* registry, Settings* settings)
{
    // settings_get
    registry->registerTool(
        "settings_get",
        "Read espresso settings: temperature, target weight, steam settings, hot water settings, DYE (bean/grinder) metadata",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"keys", QJsonObject{
                    {"type", "array"},
                    {"items", QJsonObject{{"type", "string"}}},
                    {"description", "Optional list of setting keys to return. If empty, returns all."}
                }}
            }}
        },
        [settings](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!settings) return result;

            QJsonArray keys = args["keys"].toArray();
            bool returnAll = keys.isEmpty();

            auto include = [&](const QString& key) {
                if (returnAll) return true;
                for (const auto& k : keys) {
                    if (k.toString() == key) return true;
                }
                return false;
            };

            // Espresso
            if (include("espressoTemperature")) result["espressoTemperature"] = settings->espressoTemperature();
            if (include("targetWeight")) result["targetWeight"] = settings->targetWeight();
            if (include("lastUsedRatio")) result["lastUsedRatio"] = settings->lastUsedRatio();

            // Steam
            if (include("steamTemperature")) result["steamTemperature"] = settings->steamTemperature();
            if (include("steamTimeout")) result["steamTimeout"] = settings->steamTimeout();
            if (include("steamFlow")) result["steamFlow"] = settings->steamFlow();
            if (include("steamDisabled")) result["steamDisabled"] = settings->steamDisabled();
            if (include("keepSteamHeaterOn")) result["keepSteamHeaterOn"] = settings->keepSteamHeaterOn();

            // Hot water
            if (include("waterTemperature")) result["waterTemperature"] = settings->waterTemperature();
            if (include("waterVolume")) result["waterVolume"] = settings->waterVolume();
            if (include("waterVolumeMode")) result["waterVolumeMode"] = settings->waterVolumeMode();

            // DYE metadata (current bean/grinder)
            if (include("dyeBeanBrand")) result["dyeBeanBrand"] = settings->dyeBeanBrand();
            if (include("dyeBeanType")) result["dyeBeanType"] = settings->dyeBeanType();
            if (include("dyeRoastDate")) result["dyeRoastDate"] = settings->dyeRoastDate();
            if (include("dyeRoastLevel")) result["dyeRoastLevel"] = settings->dyeRoastLevel();
            if (include("dyeGrinderBrand")) result["dyeGrinderBrand"] = settings->dyeGrinderBrand();
            if (include("dyeGrinderModel")) result["dyeGrinderModel"] = settings->dyeGrinderModel();
            if (include("dyeGrinderBurrs")) result["dyeGrinderBurrs"] = settings->dyeGrinderBurrs();
            if (include("dyeGrinderSetting")) result["dyeGrinderSetting"] = settings->dyeGrinderSetting();
            if (include("dyeBeanWeight")) result["dyeBeanWeight"] = settings->dyeBeanWeight();
            if (include("dyeDrinkWeight")) result["dyeDrinkWeight"] = settings->dyeDrinkWeight();

            // Current profile
            if (include("currentProfile")) result["currentProfile"] = settings->currentProfile();

            return result;
        },
        "read");
}
