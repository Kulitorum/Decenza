// TODO: Move disk I/O (saveProfile, saveProfileAs, deleteProfile) to background thread
// per CLAUDE.md design principle. Current tool handler architecture (synchronous
// QJsonObject return) prevents this. Requires refactoring McpToolHandler to support
// async responses.

#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../controllers/profilemanager.h"
#include "../profile/profile.h"
#include "../profile/recipeparams.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

void registerProfileTools(McpToolRegistry* registry, ProfileManager* profileManager)
{
    // profiles_list
    registry->registerTool(
        "profiles_list",
        "List all available profiles with their names and filenames",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [profileManager](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!profileManager) return result;

            QJsonArray profiles;
            QJsonArray links;
            QVariantList all = profileManager->availableProfiles();
            for (const QVariant& v : all) {
                QVariantMap pm = v.toMap();
                QJsonObject p;
                const QString filename = pm["name"].toString();
                p["filename"] = filename;
                p["title"] = pm["title"].toString();
                p["editorType"] = pm["editorType"].toString();
                p["readOnly"] = pm["readOnly"].toBool();
                profiles.append(p);

                QJsonObject link;
                link["uri"] = QStringLiteral("decenza://profiles/") + filename;
                link["title"] = pm["title"].toString();
                link["mimeType"] = "application/json";
                links.append(link);
            }
            result["profiles"] = profiles;
            result["count"] = profiles.size();
            result["_resourceLinks"] = links;
            return result;
        },
        "read");

    // profiles_get_active
    registry->registerTool(
        "profiles_get_active",
        "Get the currently active profile name and details",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [profileManager](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!profileManager) return result;

            const QString filename = profileManager->baseProfileName();
            result["filename"] = filename;
            result["modified"] = profileManager->isProfileModified();
            result["readOnly"] = profileManager->isCurrentProfileReadOnly();

            QVariantMap profile = profileManager->getCurrentProfile();
            QString profileTitle;
            if (!profile.isEmpty()) {
                profileTitle = profile["title"].toString();
                result["title"] = profileTitle;
                result["editorType"] = profileManager->currentEditorType();
                result["targetWeightG"] = profileManager->profileTargetWeight();
                result["targetTemperatureC"] = profileManager->profileTargetTemperature();
                if (profileManager->profileHasRecommendedDose())
                    result["recommendedDoseG"] = profileManager->profileRecommendedDose();
            }

            if (!filename.isEmpty()) {
                QJsonObject link;
                link["uri"] = QStringLiteral("decenza://profiles/") + filename;
                link["title"] = profileTitle.isEmpty() ? filename : profileTitle;
                link["mimeType"] = "application/json";
                result["_resourceLinks"] = QJsonArray{ link };
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
        [profileManager](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!profileManager) return result;

            QString filename = args["filename"].toString();
            if (filename.isEmpty()) {
                result["error"] = "filename is required";
                return result;
            }

            QVariantMap profile = profileManager->getProfileByFilename(filename);
            if (profile.isEmpty()) {
                result["error"] = "Profile not found: " + filename;
                return result;
            }

            // Convert QVariantMap to QJsonObject
            result = QJsonObject::fromVariantMap(profile);

            QJsonObject link;
            link["uri"] = QStringLiteral("decenza://profiles/") + filename;
            link["title"] = profile["title"].toString().isEmpty()
                ? filename : profile["title"].toString();
            link["mimeType"] = "application/json";
            result["_resourceLinks"] = QJsonArray{ link };
            return result;
        },
        "read");

    // profiles_get_params
    registry->registerTool(
        "profiles_get_params",
        "Get the current profile's editable parameters as shown in the app's editor. "
        "The fields returned depend on editorType: "
        "dflow/aflow: recipe params (fill, infuse, pour phases). "
        "pressure/flow: simple profile params (preinfusion, hold, decline, per-step temps). "
        "advanced: full profile data with individual frame/step details (same as the advanced editor).",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [profileManager](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!profileManager) return result;

            result["filename"] = profileManager->baseProfileName();
            QVariantMap profile = profileManager->getCurrentProfile();
            if (!profile.isEmpty())
                result["title"] = profile["title"].toString();

            // Use the same authoritative method the app uses to determine editor type
            QString editorType = profileManager->currentEditorType();
            result["editorType"] = editorType;

            if (editorType == "advanced") {
                // Advanced editor: show full profile data with frames
                // (same data ProfileEditorPage uses via getCurrentProfile())
                QJsonObject profileJson = QJsonObject::fromVariantMap(profile);
                for (auto it = profileJson.begin(); it != profileJson.end(); ++it) {
                    if (it.key() != "title")  // already set above
                        result[it.key()] = it.value();
                }
            } else {
                // Recipe editors (dflow, aflow, pressure, flow): show RecipeParams
                // filtered to only the fields the editor displays
                QVariantMap params = profileManager->getOrConvertRecipeParams();
                RecipeParams recipe = RecipeParams::fromVariantMap(params);
                QJsonObject recipeJson = recipe.toJson();

                // Common fields shown by all recipe editors
                QStringList common = {"targetWeight", "targetVolume", "dose", "editorType"};
                for (const QString& key : common) {
                    if (recipeJson.contains(key))
                        result[key] = recipeJson[key];
                }

                if (editorType == "dflow" || editorType == "aflow") {
                    // D-Flow/A-Flow editor fields
                    for (const QString& key : {"fillTemperature", "fillPressure", "fillFlow", "fillTimeout",
                                                "infuseEnabled", "infusePressure", "infuseTime", "infuseWeight", "infuseVolume",
                                                "pourTemperature", "pourPressure", "pourFlow"}) {
                        if (recipeJson.contains(key))
                            result[key] = recipeJson[key];
                    }
                    if (editorType == "aflow") {
                        // A-Flow-only fields
                        for (const QString& key : {"rampTime", "rampDownEnabled", "flowExtractionUp", "secondFillEnabled"}) {
                            if (recipeJson.contains(key))
                                result[key] = recipeJson[key];
                        }
                    }
                } else {
                    // Pressure/Flow editor fields
                    for (const QString& key : {"preinfusionTime", "preinfusionFlowRate", "preinfusionStopPressure",
                                                "holdTime", "simpleDeclineTime",
                                                "tempStart", "tempPreinfuse", "tempHold", "tempDecline"}) {
                        if (recipeJson.contains(key))
                            result[key] = recipeJson[key];
                    }
                    if (editorType == "pressure") {
                        for (const QString& key : {"espressoPressure", "pressureEnd", "limiterValue", "limiterRange"}) {
                            if (recipeJson.contains(key))
                                result[key] = recipeJson[key];
                        }
                    } else {
                        // flow
                        for (const QString& key : {"holdFlow", "flowEnd", "limiterValue", "limiterRange"}) {
                            if (recipeJson.contains(key))
                                result[key] = recipeJson[key];
                        }
                    }
                }
            }

            // Per #992: also emit unit/scale-suffixed aliases so AI agents
            // see the same convention as profiles_get_active and other read
            // tools (per CLAUDE.md MCP convention). The un-suffixed names
            // are kept because they are also the write keys for
            // profiles_edit_params — both forms now round-trip.
            static const QPair<const char*, const char*> suffixAliases[] = {
                // °C
                {"tempStart", "tempStartC"},
                {"tempPreinfuse", "tempPreinfuseC"},
                {"tempHold", "tempHoldC"},
                {"tempDecline", "tempDeclineC"},
                {"fillTemperature", "fillTemperatureC"},
                {"pourTemperature", "pourTemperatureC"},
                // bar
                {"fillPressure", "fillPressureBar"},
                {"infusePressure", "infusePressureBar"},
                {"pourPressure", "pourPressureBar"},
                {"espressoPressure", "espressoPressureBar"},
                {"pressureEnd", "pressureEndBar"},
                {"preinfusionStopPressure", "preinfusionStopPressureBar"},
                // mL/s
                {"fillFlow", "fillFlowMlPerSec"},
                {"pourFlow", "pourFlowMlPerSec"},
                {"holdFlow", "holdFlowMlPerSec"},
                {"flowEnd", "flowEndMlPerSec"},
                {"preinfusionFlowRate", "preinfusionFlowRateMlPerSec"},
                // s
                {"fillTimeout", "fillTimeoutSec"},
                {"infuseTime", "infuseTimeSec"},
                {"preinfusionTime", "preinfusionTimeSec"},
                {"holdTime", "holdTimeSec"},
                {"simpleDeclineTime", "simpleDeclineTimeSec"},
                {"rampTime", "rampTimeSec"},
                // g
                {"infuseWeight", "infuseWeightG"},
                {"targetWeight", "targetWeightG"},
                {"dose", "doseG"},
                // mL
                {"infuseVolume", "infuseVolumeMl"},
                {"targetVolume", "targetVolumeMl"},
            };
            for (const auto& pair : suffixAliases) {
                const QString src = QString::fromLatin1(pair.first);
                if (result.contains(src))
                    result[QString::fromLatin1(pair.second)] = result.value(src);
            }

            return result;
        },
        "read");

    // profiles_edit_params
    registry->registerTool(
        "profiles_edit_params",
        "Edit the current profile's parameters using the same code path as the app's editor. "
        "Only provide fields you want to change — unspecified fields keep their current values. "
        "For dflow/aflow/pressure/flow profiles: accepts recipe params, regenerates frames via uploadRecipeProfile(). "
        "For advanced profiles: accepts profile-level fields and a 'steps' array of frame objects via uploadProfile(). "
        "Call profiles_get_params first to see which fields are available for the current editor type.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                // Recipe params (dflow/aflow/pressure/flow)
                {"targetWeight", QJsonObject{{"type", "number"}, {"description", "Stop at weight (grams)"}}},
                {"targetVolume", QJsonObject{{"type", "number"}, {"description", "Stop at volume (mL, 0=disabled)"}}},
                {"dose", QJsonObject{{"type", "number"}, {"description", "Input dose for ratio display (grams)"}}},
                {"fillTemperature", QJsonObject{{"type", "number"}, {"description", "Fill water temperature (Celsius)"}}},
                {"fillPressure", QJsonObject{{"type", "number"}, {"description", "Fill pressure (bar)"}}},
                {"fillFlow", QJsonObject{{"type", "number"}, {"description", "Fill flow rate (mL/s)"}}},
                {"fillTimeout", QJsonObject{{"type", "number"}, {"description", "Max fill duration (seconds)"}}},
                {"infuseEnabled", QJsonObject{{"type", "boolean"}, {"description", "Enable infuse/soak phase"}}},
                {"infusePressure", QJsonObject{{"type", "number"}, {"description", "Soak pressure (bar)"}}},
                {"infuseTime", QJsonObject{{"type", "number"}, {"description", "Soak duration (seconds)"}}},
                {"infuseWeight", QJsonObject{{"type", "number"}, {"description", "Weight to exit infuse (grams, 0=disabled)"}}},
                {"infuseVolume", QJsonObject{{"type", "number"}, {"description", "Max volume during infuse (mL)"}}},
                {"pourTemperature", QJsonObject{{"type", "number"}, {"description", "Pour water temperature (Celsius)"}}},
                {"pourPressure", QJsonObject{{"type", "number"}, {"description", "Pressure limit/cap (bar)"}}},
                {"pourFlow", QJsonObject{{"type", "number"}, {"description", "Extraction flow setpoint (mL/s)"}}},
                {"rampTime", QJsonObject{{"type", "number"}, {"description", "A-Flow: ramp duration (seconds)"}}},
                {"rampDownEnabled", QJsonObject{{"type", "boolean"}, {"description", "A-Flow: split pressure ramp into up+decline"}}},
                {"flowExtractionUp", QJsonObject{{"type", "boolean"}, {"description", "A-Flow: flow ramps up during extraction"}}},
                {"secondFillEnabled", QJsonObject{{"type", "boolean"}, {"description", "A-Flow: add 2nd fill+pause before pressure ramp"}}},
                {"preinfusionTime", QJsonObject{{"type", "number"}, {"description", "Preinfusion duration (seconds)"}}},
                {"preinfusionFlowRate", QJsonObject{{"type", "number"}, {"description", "Preinfusion flow rate (mL/s)"}}},
                {"preinfusionStopPressure", QJsonObject{{"type", "number"}, {"description", "Exit preinfusion at this pressure (bar)"}}},
                {"holdTime", QJsonObject{{"type", "number"}, {"description", "Hold phase duration (seconds)"}}},
                {"espressoPressure", QJsonObject{{"type", "number"}, {"description", "Pressure setpoint (bar)"}}},
                {"holdFlow", QJsonObject{{"type", "number"}, {"description", "Flow setpoint (mL/s)"}}},
                {"simpleDeclineTime", QJsonObject{{"type", "number"}, {"description", "Decline phase duration (seconds)"}}},
                {"pressureEnd", QJsonObject{{"type", "number"}, {"description", "End pressure for decline (bar)"}}},
                {"flowEnd", QJsonObject{{"type", "number"}, {"description", "End flow for decline (mL/s)"}}},
                {"limiterValue", QJsonObject{{"type", "number"}, {"description", "Flow/pressure limiter"}}},
                {"limiterRange", QJsonObject{{"type", "number"}, {"description", "Limiter P/I range"}}},
                {"tempStart", QJsonObject{{"type", "number"}, {"description", "Start temperature (Celsius)"}}},
                {"tempPreinfuse", QJsonObject{{"type", "number"}, {"description", "Preinfusion temperature (Celsius)"}}},
                {"tempHold", QJsonObject{{"type", "number"}, {"description", "Hold temperature (Celsius)"}}},
                {"tempDecline", QJsonObject{{"type", "number"}, {"description", "Decline temperature (Celsius)"}}},
                // Advanced profile fields (only applied when editorType is "advanced")
                {"title", QJsonObject{{"type", "string"}, {"description", "Profile title (advanced editor only)"}}},
                {"author", QJsonObject{{"type", "string"}, {"description", "Profile author (advanced editor only)"}}},
                {"espresso_temperature", QJsonObject{{"type", "number"}, {"description", "Advanced: base espresso temperature (Celsius)"}}},
                {"target_weight", QJsonObject{{"type", "number"}, {"description", "Advanced: target weight (grams)"}}},
                {"target_volume", QJsonObject{{"type", "number"}, {"description", "Advanced: target volume (mL)"}}},
                {"notes", QJsonObject{{"type", "string"}, {"description", "Advanced: profile notes text"}}},
                {"has_recommended_dose", QJsonObject{{"type", "boolean"}, {"description", "Advanced: enable recommended dose"}}},
                {"recommended_dose", QJsonObject{{"type", "number"}, {"description", "Advanced: recommended dose (grams)"}}},
                {"tank_desired_water_temperature", QJsonObject{{"type", "number"}, {"description", "Advanced: tank water temperature (Celsius)"}}},
                {"maximum_flow_range_advanced", QJsonObject{{"type", "number"}, {"description", "Advanced: flow limiter range (mL/s)"}}},
                {"maximum_pressure_range_advanced", QJsonObject{{"type", "number"}, {"description", "Advanced: pressure limiter range (bar)"}}},
                {"preinfuse_frame_count", QJsonObject{{"type", "integer"}, {"description", "Advanced: number of preinfusion frames for BLE header (de1app defaults to 0)"}}},
                {"steps", QJsonObject{{"type", "array"}, {"description", "Advanced: array of frame objects (same format as profiles_get_params returns)"}}},
                // Confirmation
                {"confirmed", QJsonObject{{"type", "boolean"}, {"description", "Set to true after user confirms this action in chat"}}}
            }}
        },
        [profileManager](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!profileManager) {
                result["error"] = "Controller not available";
                return result;
            }

            // Use the same authoritative method the app uses to determine editor type
            QString editorType = profileManager->currentEditorType();

            if (editorType == "advanced") {
                // Advanced path: use uploadProfile() — same as ProfileEditorPage
                QVariantMap profileData = profileManager->getCurrentProfile();
                for (auto it = args.begin(); it != args.end(); ++it) {
                    if (it.key() == "confirmed") continue;
                    profileData[it.key()] = it.value().toVariant();
                }
                profileManager->uploadProfile(profileData);
                profileManager->uploadCurrentProfile();  // MCP is one-shot, upload immediately
            } else {
                // Recipe path: use uploadRecipeProfile() — same as RecipeEditorPage/SimpleProfileEditorPage
                QVariantMap currentParams = profileManager->getOrConvertRecipeParams();
                for (auto it = args.begin(); it != args.end(); ++it) {
                    if (it.key() == "confirmed") continue;
                    currentParams[it.key()] = it.value().toVariant();
                }
                profileManager->uploadRecipeProfile(currentParams);
                profileManager->uploadCurrentProfile();  // MCP is one-shot, upload immediately
            }

            result["success"] = true;
            result["message"] = "Profile updated and uploaded to machine. Call profiles_save to persist.";
            result["modified"] = true;
            result["editorType"] = editorType;
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
        [profileManager](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!profileManager) {
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
                if (profileManager->isBuiltInFilename(filename)) {
                    result["error"] = "Cannot save with filename '" + filename +
                        "' because it conflicts with a built-in profile. Choose a different name.";
                    return result;
                }

                // Tool handlers run on the main thread (via ShotServer), so call directly
                bool success = profileManager->saveProfileAs(filename, title);

                if (success) {
                    result["success"] = true;
                    result["message"] = "Profile saved as: " + title;
                    result["filename"] = filename;
                } else {
                    result["error"] = "Failed to save profile as: " + filename;
                }
            } else {
                // Save in place under base filename (currentProfileName() includes * prefix when modified)
                QString currentFilename = profileManager->baseProfileName();
                if (currentFilename.isEmpty()) {
                    result["error"] = "No active profile to save";
                    return result;
                }
                if (profileManager->isCurrentProfileReadOnly()) {
                    result["error"] = "Cannot save read-only profile in place. Use filename/title params for Save As with a new name.";
                    return result;
                }

                bool success = profileManager->saveProfile(currentFilename);

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
        [profileManager](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!profileManager) {
                result["error"] = "Controller not available";
                return result;
            }

            QString filename = args["filename"].toString();
            if (filename.isEmpty()) {
                result["error"] = "filename is required";
                return result;
            }

            if (!profileManager->profileExists(filename)) {
                result["error"] = "Profile not found: " + filename;
                return result;
            }

            if (filename == profileManager->baseProfileName()) {
                result["error"] = "Cannot delete the currently-active profile '" + filename +
                    "'. Call profiles_set_active with a different profile first, then retry.";
                result["filename"] = filename;
                return result;
            }

            bool deleted = profileManager->deleteProfile(filename);
            if (deleted) {
                result["success"] = true;
                result["message"] = "Profile deleted: " + filename;
            } else if (profileManager->profileExists(filename)) {
                // Profile still exists after delete — it's a built-in (can't be fully removed)
                result["success"] = true;
                result["message"] = "Local overrides removed — profile reverted to built-in version: " + filename;
                result["reverted"] = true;
            } else {
                result["error"] = "Failed to delete profile: " + filename;
            }
            result["filename"] = filename;
            return result;
        },
        "settings");

    // profiles_create
    registry->registerTool(
        "profiles_create",
        "Create a new blank profile with the given editor type and title. "
        "Uses the same creation functions as the QML UI. "
        "The new profile becomes active and can be edited via profiles_edit_params.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"editorType", QJsonObject{{"type", "string"}, {"description", "Editor type: dflow, aflow, pressure, flow, or advanced"}}},
                {"title", QJsonObject{{"type", "string"}, {"description", "Profile title"}}},
                {"confirmed", QJsonObject{{"type", "boolean"}, {"description", "Set to true after user confirms this action in chat"}}}
            }},
            {"required", QJsonArray{"editorType", "title"}}
        },
        [profileManager](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!profileManager) {
                result["error"] = "Controller not available";
                return result;
            }

            QString editorType = args["editorType"].toString();
            QString title = args["title"].toString();
            if (title.isEmpty()) {
                result["error"] = "title is required";
                return result;
            }

            // D-Flow/A-Flow profiles require title prefix for editor type detection
            // (matching QML RecipeEditorPage behavior which always prefixes)
            if (editorType == "dflow" && !title.startsWith("D-Flow")) {
                title = "D-Flow / " + title;
            } else if (editorType == "aflow" && !title.startsWith("A-Flow")) {
                title = "A-Flow / " + title;
            }

            // Route to the same creation functions as the QML UI
            if (editorType == "dflow") {
                profileManager->createNewRecipe(title);
            } else if (editorType == "aflow") {
                profileManager->createNewAFlowRecipe(title);
            } else if (editorType == "pressure") {
                profileManager->createNewPressureProfile(title);
            } else if (editorType == "flow") {
                profileManager->createNewFlowProfile(title);
            } else if (editorType == "advanced") {
                profileManager->createNewProfile(title);
            } else {
                result["error"] = "Invalid editorType: " + editorType + ". Must be dflow, aflow, pressure, flow, or advanced.";
                return result;
            }

            result["success"] = true;
            result["message"] = "Profile created: " + title;
            result["editorType"] = editorType;
            result["filename"] = profileManager->baseProfileName();
            return result;
        },
        "settings");
}
