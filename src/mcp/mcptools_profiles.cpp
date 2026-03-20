#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../controllers/maincontroller.h"
#include "../profile/profile.h"
#include "../profile/recipeparams.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

void registerProfileTools(McpToolRegistry* registry, MainController* mainController)
{
    // profiles_list
    registry->registerTool(
        "profiles_list",
        "List all available profiles with their names and filenames",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [mainController](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!mainController) return result;

            QJsonArray profiles;
            QVariantList all = mainController->availableProfiles();
            for (const QVariant& v : all) {
                QVariantMap pm = v.toMap();
                QJsonObject p;
                p["filename"] = pm["name"].toString();  // "name" is the filename key in availableProfiles()
                p["title"] = pm["title"].toString();
                profiles.append(p);
            }
            result["profiles"] = profiles;
            result["count"] = profiles.size();
            return result;
        },
        "read");

    // profiles_get_active
    registry->registerTool(
        "profiles_get_active",
        "Get the currently active profile name and details",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [mainController](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!mainController) return result;

            result["filename"] = mainController->baseProfileName();
            result["modified"] = mainController->isProfileModified();

            QVariantMap profile = mainController->getCurrentProfile();
            if (!profile.isEmpty()) {
                result["title"] = profile["title"].toString();
                result["editorType"] = profile["editorType"].toString();
                result["targetWeight"] = mainController->profileTargetWeight();
                result["targetTemperature"] = mainController->profileTargetTemperature();
                if (mainController->profileHasRecommendedDose())
                    result["recommendedDose"] = mainController->profileRecommendedDose();
            }
            return result;
        },
        "read");

    // profiles_get_detail
    registry->registerTool(
        "profiles_get_detail",
        "Get full profile JSON by filename",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"filename", QJsonObject{{"type", "string"}, {"description", "Profile filename (without .json extension)"}}}
            }},
            {"required", QJsonArray{"filename"}}
        },
        [mainController](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!mainController) return result;

            QString filename = args["filename"].toString();
            if (filename.isEmpty()) {
                result["error"] = "filename is required";
                return result;
            }

            QVariantMap profile = mainController->getProfileByFilename(filename);
            if (profile.isEmpty()) {
                result["error"] = "Profile not found: " + filename;
                return result;
            }

            // Convert QVariantMap to QJsonObject
            result = QJsonObject::fromVariantMap(profile);
            return result;
        },
        "read");

    // profiles_get_params
    registry->registerTool(
        "profiles_get_params",
        "Get the current profile's editable recipe parameters, tailored to its editor type "
        "(dflow, aflow, pressure, flow). Returns all parameters that can be passed to profiles_edit_params. "
        "Use this to read the current profile state before making changes.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [mainController](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!mainController) return result;

            QVariantMap params = mainController->getOrConvertRecipeParams();
            result["filename"] = mainController->baseProfileName();

            QVariantMap profile = mainController->getCurrentProfile();
            if (!profile.isEmpty())
                result["title"] = profile["title"].toString();

            // Convert RecipeParams to JSON
            RecipeParams recipe = RecipeParams::fromVariantMap(params);
            QJsonObject recipeJson = recipe.toJson();
            // Merge recipe fields into result
            for (auto it = recipeJson.begin(); it != recipeJson.end(); ++it)
                result[it.key()] = it.value();

            return result;
        },
        "read");

    // profiles_edit_params
    registry->registerTool(
        "profiles_edit_params",
        "Edit the current profile's recipe parameters and regenerate frames. "
        "Only provide the fields you want to change — unspecified fields keep their current values. "
        "Triggers frame regeneration and uploads the updated profile to the machine. "
        "Fields by editor type: "
        "ALL: targetWeight, targetVolume, dose. "
        "D-Flow/A-Flow: fillTemperature, fillPressure, fillFlow, fillTimeout, infuseEnabled, infusePressure, "
        "infuseTime, infuseWeight, pourTemperature, pourPressure, pourFlow, rampTime. "
        "A-Flow only: rampDownEnabled, flowExtractionUp, secondFillEnabled. "
        "Pressure/Flow: preinfusionTime, preinfusionFlowRate, preinfusionStopPressure, holdTime, "
        "espressoPressure, holdFlow, simpleDeclineTime, pressureEnd, flowEnd, limiterValue, limiterRange. "
        "Per-step temps: tempStart, tempPreinfuse, tempHold, tempDecline.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                // Core
                {"targetWeight", QJsonObject{{"type", "number"}, {"description", "Stop at weight (grams)"}}},
                {"targetVolume", QJsonObject{{"type", "number"}, {"description", "Stop at volume (mL, 0=disabled)"}}},
                {"dose", QJsonObject{{"type", "number"}, {"description", "Input dose for ratio display (grams)"}}},
                // Fill phase
                {"fillTemperature", QJsonObject{{"type", "number"}, {"description", "Fill water temperature (Celsius)"}}},
                {"fillPressure", QJsonObject{{"type", "number"}, {"description", "Fill pressure (bar)"}}},
                {"fillFlow", QJsonObject{{"type", "number"}, {"description", "Fill flow rate (mL/s)"}}},
                {"fillTimeout", QJsonObject{{"type", "number"}, {"description", "Max fill duration (seconds)"}}},
                // Infuse phase
                {"infuseEnabled", QJsonObject{{"type", "boolean"}, {"description", "Enable infuse/soak phase"}}},
                {"infusePressure", QJsonObject{{"type", "number"}, {"description", "Soak pressure (bar)"}}},
                {"infuseTime", QJsonObject{{"type", "number"}, {"description", "Soak duration (seconds)"}}},
                {"infuseWeight", QJsonObject{{"type", "number"}, {"description", "Weight to exit infuse (grams, 0=disabled)"}}},
                // Pour phase
                {"pourTemperature", QJsonObject{{"type", "number"}, {"description", "Pour water temperature (Celsius)"}}},
                {"pourPressure", QJsonObject{{"type", "number"}, {"description", "Pressure limit/cap (bar)"}}},
                {"pourFlow", QJsonObject{{"type", "number"}, {"description", "Extraction flow setpoint (mL/s)"}}},
                {"rampTime", QJsonObject{{"type", "number"}, {"description", "Transition ramp duration (seconds)"}}},
                // A-Flow specific
                {"rampDownEnabled", QJsonObject{{"type", "boolean"}, {"description", "A-Flow: split pressure ramp into up+decline"}}},
                {"flowExtractionUp", QJsonObject{{"type", "boolean"}, {"description", "A-Flow: flow ramps up during extraction"}}},
                {"secondFillEnabled", QJsonObject{{"type", "boolean"}, {"description", "A-Flow: add 2nd fill+pause before pressure ramp"}}},
                // Simple profile params
                {"preinfusionTime", QJsonObject{{"type", "number"}, {"description", "Preinfusion duration (seconds)"}}},
                {"preinfusionFlowRate", QJsonObject{{"type", "number"}, {"description", "Preinfusion flow rate (mL/s)"}}},
                {"preinfusionStopPressure", QJsonObject{{"type", "number"}, {"description", "Exit preinfusion at this pressure (bar)"}}},
                {"holdTime", QJsonObject{{"type", "number"}, {"description", "Hold phase duration (seconds)"}}},
                {"espressoPressure", QJsonObject{{"type", "number"}, {"description", "Pressure setpoint for pressure profiles (bar)"}}},
                {"holdFlow", QJsonObject{{"type", "number"}, {"description", "Flow setpoint for flow profiles (mL/s)"}}},
                {"simpleDeclineTime", QJsonObject{{"type", "number"}, {"description", "Decline phase duration (seconds, 0=disabled)"}}},
                {"pressureEnd", QJsonObject{{"type", "number"}, {"description", "End pressure for pressure decline (bar)"}}},
                {"flowEnd", QJsonObject{{"type", "number"}, {"description", "End flow for flow decline (mL/s)"}}},
                {"limiterValue", QJsonObject{{"type", "number"}, {"description", "Flow limiter (pressure) / Pressure limiter (flow)"}}},
                {"limiterRange", QJsonObject{{"type", "number"}, {"description", "Limiter P/I range"}}},
                // Per-step temperatures
                {"tempStart", QJsonObject{{"type", "number"}, {"description", "Start temperature (Celsius)"}}},
                {"tempPreinfuse", QJsonObject{{"type", "number"}, {"description", "Preinfusion temperature (Celsius)"}}},
                {"tempHold", QJsonObject{{"type", "number"}, {"description", "Hold temperature (Celsius)"}}},
                {"tempDecline", QJsonObject{{"type", "number"}, {"description", "Decline temperature (Celsius)"}}},
                // Confirmation
                {"confirmed", QJsonObject{{"type", "boolean"}, {"description", "Set to true after user confirms this action in chat"}}}
            }}
        },
        [mainController](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!mainController) {
                result["error"] = "Controller not available";
                return result;
            }

            // Read current params
            QVariantMap currentParams = mainController->getOrConvertRecipeParams();

            // Merge provided fields on top of current values
            for (auto it = args.begin(); it != args.end(); ++it) {
                if (it.key() == "confirmed")
                    continue;  // Don't merge confirmation flag into recipe params
                currentParams[it.key()] = it.value().toVariant();
            }

            // Upload on main thread — this validates, regenerates frames, and uploads to machine
            QMetaObject::invokeMethod(mainController, [mainController, currentParams]() {
                mainController->uploadRecipeProfile(currentParams);
            }, Qt::QueuedConnection);

            result["success"] = true;
            result["message"] = "Profile parameters updated — frames regenerated and uploaded to machine. Profile is now modified but not saved to disk. Call profiles_save to persist.";
            result["modified"] = true;
            result["editorType"] = currentParams["editorType"].toString();
            if (args.contains("targetWeight"))
                result["targetWeight"] = args["targetWeight"].toDouble();
            if (args.contains("targetVolume"))
                result["targetVolume"] = args["targetVolume"].toDouble();
            return result;
        },
        "settings");

    // profiles_save
    registry->registerTool(
        "profiles_save",
        "Save the current (modified) profile to disk. Without calling this, edits from profiles_edit_params "
        "are active on the machine but will be lost if another profile is loaded. "
        "Saves under the current filename by default, or provide a new filename/title to Save As a copy.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"filename", QJsonObject{{"type", "string"}, {"description", "New filename for Save As (without .json). Omit to save in place."}}},
                {"title", QJsonObject{{"type", "string"}, {"description", "New title for Save As. Required when filename is provided."}}},
                {"confirmed", QJsonObject{{"type", "boolean"}, {"description", "Set to true after user confirms this action in chat"}}}
            }}
        },
        [mainController](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!mainController) {
                result["error"] = "Controller not available";
                return result;
            }

            bool isSaveAs = args.contains("filename");
            if (isSaveAs) {
                QString filename = args["filename"].toString();
                QString title = args["title"].toString();
                if (filename.isEmpty()) {
                    result["error"] = "filename cannot be empty";
                    return result;
                }
                if (title.isEmpty()) {
                    result["error"] = "title is required for Save As";
                    return result;
                }

                // Tool handlers run on the main thread (via ShotServer), so call directly
                bool success = mainController->saveProfileAs(filename, title);

                if (success) {
                    result["success"] = true;
                    result["message"] = "Profile saved as: " + title;
                    result["filename"] = filename;
                } else {
                    result["error"] = "Failed to save profile as: " + filename;
                }
            } else {
                // Save in place under base filename (currentProfileName() includes * prefix when modified)
                QString currentFilename = mainController->baseProfileName();
                if (currentFilename.isEmpty()) {
                    result["error"] = "No active profile to save";
                    return result;
                }

                bool success = mainController->saveProfile(currentFilename);

                if (success) {
                    result["success"] = true;
                    result["message"] = "Profile saved: " + currentFilename;
                    result["filename"] = currentFilename;
                } else {
                    result["error"] = "Failed to save profile: " + currentFilename;
                }
            }
            return result;
        },
        "settings");

    // profiles_delete
    registry->registerTool(
        "profiles_delete",
        "Delete a user or downloaded profile. For built-in profiles, this removes any local overrides "
        "and reverts to the original built-in version (the profile itself cannot be deleted). "
        "After deletion, the profile list is refreshed. If the deleted profile was the active one, "
        "call profiles_set_active to switch to another.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"filename", QJsonObject{{"type", "string"}, {"description", "Profile filename to delete (without .json)"}}},
                {"confirmed", QJsonObject{{"type", "boolean"}, {"description", "Set to true after user confirms this action in chat"}}}
            }},
            {"required", QJsonArray{"filename"}}
        },
        [mainController](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!mainController) {
                result["error"] = "Controller not available";
                return result;
            }

            QString filename = args["filename"].toString();
            if (filename.isEmpty()) {
                result["error"] = "filename is required";
                return result;
            }

            if (!mainController->profileExists(filename)) {
                result["error"] = "Profile not found: " + filename;
                return result;
            }

            bool deleted = mainController->deleteProfile(filename);
            if (deleted) {
                result["success"] = true;
                result["message"] = "Profile deleted: " + filename;
            } else {
                // Built-in profiles return false but local overrides are still cleaned up
                result["success"] = true;
                result["message"] = "Local overrides removed — profile reverted to built-in version: " + filename;
                result["reverted"] = true;
            }
            result["filename"] = filename;
            return result;
        },
        "settings");
}
