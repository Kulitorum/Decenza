// TODO: Move disk I/O (saveProfile, saveProfileAs, deleteProfile) to background thread
// per CLAUDE.md design principle. Current tool handler architecture (synchronous
// QJsonObject return) prevents this. Requires refactoring McpToolHandler to support
// async responses.

#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../controllers/profilemanager.h"
#include "../core/settings.h"
#include "../core/settings_app.h"
#include "../profile/profile.h"
#include "../profile/recipeparams.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

void registerProfileTools(McpToolRegistry* registry, ProfileManager* profileManager)
{
    // profiles_list
    registry->registerTool(
        "profiles_list",
        "List available profiles with optional filters. Returns filename, title, editorType, "
        "readOnly, and category (parsed from the title's slash prefix — Tea, Cleaning, "
        "Pour over basket, Test, Visualizer, or null for espresso recipes).",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"editorType", QJsonObject{
                    {"type", "string"},
                    {"enum", QJsonArray{"pressure", "flow", "dflow", "aflow", "advanced"}},
                    {"description", "Filter by editor type."}
                }},
                {"nameContains", QJsonObject{
                    {"type", "string"},
                    {"description", "Substring match against title or filename (case-insensitive)."}
                }},
                {"excludeCategories", QJsonObject{
                    {"type", "array"},
                    {"items", QJsonObject{{"type", "string"}}},
                    {"description", "Title-prefix categories to skip, e.g. [\"Tea\", \"Cleaning\", \"Pour over basket\", \"Test\", \"Visualizer\"]."}
                }},
                {"readOnly", QJsonObject{
                    {"type", "boolean"},
                    {"description", "true: only built-in (read-only) profiles. false: only user-writable."}
                }}
            }}
        },
        [profileManager](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!profileManager) {
                result["error"] = "Controller not available";
                return result;
            }

            const QString editorTypeFilter = args.value("editorType").toString();
            const QString nameContains = args.value("nameContains").toString().toLower();
            const QJsonArray excludeArr = args.value("excludeCategories").toArray();
            // Lowercase exclusion set so "tea"/"Tea"/"TEA" all match — LLMs
            // don't always reproduce capitalization from documented examples
            // verbatim, and the cost of a permissive match here is nil.
            QSet<QString> excludeCategoriesLower;
            for (const auto& v : excludeArr) excludeCategoriesLower.insert(v.toString().toLower());
            const bool hasReadOnlyFilter = args.contains("readOnly");
            const bool readOnlyFilter = args.value("readOnly").toBool();

            // Derive the slash-prefix category from the title. Profiles
            // titled "Tea/Some Variant" map to category "Tea"; titles
            // without a slash get null (treated as Espresso recipes).
            auto categoryOf = [](const QString& title) -> QString {
                const qsizetype slash = title.indexOf('/');
                // Built-in profile titles use "Foo / Bar" with whitespace
                // around the slash ("D-Flow / default", "A-Flow / ...",
                // also produced by profiles_create), so trim to keep
                // callers matching bare "Tea", "D-Flow", etc.
                return slash > 0 ? title.left(slash).trimmed() : QString();
            };

            QJsonArray profiles;
            QJsonArray links;
            QVariantList all = profileManager->availableProfiles();
            for (const QVariant& v : all) {
                QVariantMap pm = v.toMap();
                const QString filename = pm["name"].toString();
                const QString title = pm["title"].toString();
                const QString editorType = pm["editorType"].toString();
                const bool readOnly = pm["readOnly"].toBool();
                const QString category = categoryOf(title);

                if (!editorTypeFilter.isEmpty() && editorType != editorTypeFilter) continue;
                if (!nameContains.isEmpty() &&
                    !title.toLower().contains(nameContains) &&
                    !filename.toLower().contains(nameContains)) continue;
                if (!excludeCategoriesLower.isEmpty() && excludeCategoriesLower.contains(category.toLower())) continue;
                if (hasReadOnlyFilter && readOnly != readOnlyFilter) continue;

                QJsonObject p;
                p["filename"] = filename;
                p["title"] = title;
                p["editorType"] = editorType;
                p["readOnly"] = readOnly;
                p["category"] = category.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(category);
                profiles.append(p);

                QJsonObject link;
                link["uri"] = QStringLiteral("decenza://profiles/") + filename;
                link["title"] = title;
                link["mimeType"] = "application/json";
                links.append(link);
            }
            result["profiles"] = profiles;
            result["count"] = profiles.size();
            result["_resourceLinks"] = links;
            return result;
        },
        "read", McpTierCore);



    // profiles_get_active
    registry->registerTool(
        "profiles_get_active",
        "Get the currently active profile name and details",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [profileManager](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!profileManager) {
                result["error"] = "Controller not available";
                return result;
            }

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
        "read", McpTierCore);

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
            if (!profileManager) {
                result["error"] = "Controller not available";
                return result;
            }

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
        "read", McpTierCore);

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
            if (!profileManager) {
                result["error"] = "Controller not available";
                return result;
            }

            result["filename"] = profileManager->baseProfileName();
            QVariantMap profile = profileManager->getCurrentProfile();
            if (!profile.isEmpty())
                result["title"] = profile["title"].toString();

            // Use the same authoritative method the app uses to determine editor type
            QString editorType = profileManager->currentEditorType();
            result["editorType"] = editorType;

            // Always PAIRED, and reported exactly once. Every profile holds a dose
            // whether one was set or not (the read default is 18 g), so a bare figure
            // would tell a caller there is a recommendation when there is not — all
            // eight built-ins that used to carry a recipe block sit at exactly that
            // default with the flag off.
            //
            // Emitted on EVERY editor type. This used to skip the advanced branch,
            // whose profile-JSON spread already carried `recommended_dose` /
            // `has_recommended_dose`, on the grounds that those were the spellings
            // its own edit path accepted — emitting both would have handed an agent
            // two names for one field with no way to tell which the writer honours.
            // No editor type accepts either raw name now, and `dose` is the one way
            // to write this field everywhere (dose-source-precedence), so the pair
            // below is the single reported spelling — the advanced spread drops the
            // snake_case names rather than duplicating them.
            result["recommendedDoseG"] = profileManager->profileRecommendedDose();
            result["hasRecommendedDose"] = profileManager->profileHasRecommendedDose();

            if (editorType == "advanced") {
                // Advanced editor: show full profile data with frames
                // (same data ProfileEditorPage uses via getCurrentProfile())
                QJsonObject profileJson = QJsonObject::fromVariantMap(profile);
                for (auto it = profileJson.begin(); it != profileJson.end(); ++it) {
                    // The snake_case dose pair is dropped from the spread: it is
                    // the same field the camelCase pair above already reports,
                    // and only `dose` can write it now. Leaving both in would
                    // show four keys for two fields and invite a reader to send
                    // back the spelling that no longer does anything.
                    if (it.key() == "title"                       // already set above
                        || it.key() == "recommended_dose"
                        || it.key() == "has_recommended_dose")
                        continue;
                    result[it.key()] = it.value();
                }
            } else {
                // Recipe editors (dflow, aflow, pressure, flow): show RecipeParams
                // filtered to only the fields the editor displays
                QVariantMap params = profileManager->getOrConvertRecipeParams();
                RecipeParams recipe = RecipeParams::fromVariantMap(params);
                QJsonObject recipeJson = recipe.toJson();

                // Common fields shown by all recipe editors
                QStringList common = {"targetWeight", "targetVolume", "editorType"};
                for (const QString& key : common) {
                    if (recipeJson.contains(key))
                        result[key] = recipeJson[key];
                }

                if (editorType == "dflow" || editorType == "aflow") {
                    // D-Flow/A-Flow editor fields.
                    // const char*, not const QString&: binding a QString
                    // reference to a braced list of literals constructs a
                    // temporary per element, for every key in the list rather
                    // than only the ones present. GCC's -Wall flags it
                    // (-Wrange-loop-construct); clang's does not, which is why
                    // the macOS warning measurement never saw these five.
                    // fillPressure / fillFlow / fillTimeout / infuseEnabled are
                    // deliberately absent: neither plugin exposes them, and writing
                    // them rewrote frame fields the plugins preserve. See
                    // RecipeParams.
                    for (const char* key : {"fillTemperature",
                                                "infusePressure", "infuseTime", "infuseWeight", "infuseVolume",
                                                "pourTemperature", "pourPressure", "pourFlow"}) {
                        if (recipeJson.contains(key))
                            result[key] = recipeJson[key];
                    }
                    if (editorType == "aflow") {
                        // A-Flow-only fields
                        for (const char* key : {"rampTime", "rampDownEnabled", "flowExtractionUp", "secondFillEnabled"}) {
                            if (recipeJson.contains(key))
                                result[key] = recipeJson[key];
                        }
                    }
                } else {
                    // Pressure/Flow editor fields
                    for (const char* key : {"preinfusionTime", "preinfusionFlowRate", "preinfusionStopPressure",
                                                "holdTime", "simpleDeclineTime",
                                                "tempStart", "tempPreinfuse", "tempHold", "tempDecline"}) {
                        if (recipeJson.contains(key))
                            result[key] = recipeJson[key];
                    }
                    if (editorType == "pressure") {
                        for (const char* key : {"espressoPressure", "pressureEnd", "limiterValue", "limiterRange"}) {
                            if (recipeJson.contains(key))
                                result[key] = recipeJson[key];
                        }
                    } else {
                        // flow
                        for (const char* key : {"holdFlow", "flowEnd", "limiterValue", "limiterRange"}) {
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
                {"infusePressure", "infusePressureBar"},
                {"pourPressure", "pourPressureBar"},
                {"espressoPressure", "espressoPressureBar"},
                {"pressureEnd", "pressureEndBar"},
                {"preinfusionStopPressure", "preinfusionStopPressureBar"},
                // mL/s
                {"pourFlow", "pourFlowMlPerSec"},
                {"holdFlow", "holdFlowMlPerSec"},
                {"flowEnd", "flowEndMlPerSec"},
                {"preinfusionFlowRate", "preinfusionFlowRateMlPerSec"},
                // s
                {"infuseTime", "infuseTimeSec"},
                {"preinfusionTime", "preinfusionTimeSec"},
                {"holdTime", "holdTimeSec"},
                {"simpleDeclineTime", "simpleDeclineTimeSec"},
                {"rampTime", "rampTimeSec"},
                // g
                {"infuseWeight", "infuseWeightG"},
                {"targetWeight", "targetWeightG"},
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
        "read", McpTierCore);

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
                {"dose", QJsonObject{{"type", "number"}, {"description", "Recommended dose, grams 0-100; 0 clears it. Must be a number — a string is rejected"}}},
                {"fillTemperature", QJsonObject{{"type", "number"}, {"description", "Fill water temperature (Celsius)"}}},
                {"infusePressure", QJsonObject{{"type", "number"}, {"description", "Soak pressure (bar)"}}},
                {"infuseTime", QJsonObject{{"type", "number"}, {"description", "Soak duration (seconds, 0=no soak)"}}},
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
                // No `has_recommended_dose` / `recommended_dose` here: `dose`
                // above is the one spelling of the per-profile dose on every
                // editor type (dose-source-precedence). The field is still
                // reported by profiles_get_params, as `recommendedDoseG` +
                // `hasRecommendedDose` — a reader needs the enabled flag.
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

            QStringList ignoredKeys;      // see the recipe path below
            QStringList retiredKeys;      // recognised once, now replaced by `dose`

            QJsonObject remaining = args;

            // The retired spellings. `recommended_dose` / `has_recommended_dose`
            // used to be accepted on the advanced branch — the whole profile map
            // is passed straight through there — so the same profile field had
            // two names depending on editor type. Removing the second name is
            // what retires the collision between them; adjudicating it was the
            // previous attempt, and because the two spellings differ — `dose` is
            // set-and-enable, `recommended_dose` writes the value and leaves the
            // flag untouched — it stored a dose whose recommendation stayed
            // disabled on every profile that ships with the flag off, which is
            // all eight built-ins. No reader acts on that state.
            //
            // Stripped explicitly rather than just dropped from the schema:
            // nothing validates incoming keys against it, so on the advanced
            // branch they would otherwise still be applied by the map loop.
            for (const QString& retired : {QStringLiteral("recommended_dose"),
                                           QStringLiteral("has_recommended_dose")}) {
                if (remaining.contains(retired)) {
                    remaining.remove(retired);
                    retiredKeys << retired;
                }
            }

            // `dose` is handled here for BOTH paths, before anything else looks at
            // the incoming keys.
            //
            // It used to write RecipeParams::dose, which lived in the profile's recipe
            // block and was read by nothing. That block is gone and so is the field, so
            // an unhandled `dose` would fall through to the currentParams membership
            // check below and be reported IGNORED — the one outcome worth avoiding,
            // since the parameter has always been accepted. It now writes the
            // per-profile dose that IS consumed: recommended_dose plus its enabled
            // flag, which reach the advanced editor's control, dialing_get_context and
            // the AI advisor.
            //
            // Clamped because RecipeParams::clamp() bounded this to [0, 100] and
            // Profile::setRecommendedDose is a bare assignment.
            //
            // VALIDATED, not coerced. `dose` is the one key that does not travel
            // through toVariant() into a QVariantMap — it is read straight as a
            // double — and QJsonValue::toDouble() answers 0 for anything that is
            // not a JSON number, which setCurrentProfileRecommendedDose reads as
            // "clear the recommendation". A stringified number is NOT the
            // exposure: the registry's normalizeArguments already coerces "18"
            // to 18 off the schema's declared type. What survives that is a
            // value no parse can rescue — "heavy", null, an object — and those
            // would silently DELETE the profile's dose and report success.
            // Refuse them, and say when a legal value had to be clamped rather
            // than echoing the caller's number back as if it had been stored.
            bool doseApplied = false;
            if (remaining.contains(QStringLiteral("dose"))) {
                const QJsonValue raw = remaining.take(QStringLiteral("dose"));
                if (!raw.isDouble()) {
                    result["success"] = false;
                    result["error"] = QStringLiteral(
                        "'dose' must be a number in grams (0-100); 0 clears the "
                        "recommendation. Received a value that is not numeric.");
                    return result;
                }
                const double d = qBound(0.0, raw.toDouble(), 100.0);
                if (!qFuzzyCompare(1.0 + d, 1.0 + raw.toDouble())) {
                    result["adjustedFields"] = QJsonArray{QStringLiteral("dose")};
                    result["adjustedNote"] =
                        QStringLiteral("dose %1 is outside 0-100 g and was clamped to %2.")
                            .arg(raw.toDouble()).arg(d);
                }
                profileManager->setCurrentProfileRecommendedDose(d);
                doseApplied = true;
            }

            // Nothing left to apply — every key in the call was a retired
            // spelling. Falling through would still run uploadProfile(), which
            // sets m_profileModified and rewrites _current.json: a fully
            // rejected edit would dirty the loaded profile and then tell the
            // caller to profiles_save the modification it never made. Stop here
            // and report the rejection.
            int actionableKeys = 0;
            for (auto it = remaining.begin(); it != remaining.end(); ++it)
                if (it.key() != QLatin1String("confirmed"))
                    actionableKeys++;
            if (!doseApplied && actionableKeys == 0 && !retiredKeys.isEmpty()) {
                result["success"] = false;
                result["retiredFields"] = QJsonArray::fromStringList(retiredKeys);
                result["error"] =
                    QStringLiteral("Nothing was changed: %1 no longer set the per-profile dose. "
                                   "Use 'dose' (grams, 0-100), which sets the value and enables "
                                   "the recommendation, on every editor type.")
                        .arg(retiredKeys.join(QStringLiteral(", ")));
                return result;
            }

            if (editorType == "advanced") {
                // Advanced path: use uploadProfile() — same as ProfileEditorPage
                QVariantMap profileData = profileManager->getCurrentProfile();
                for (auto it = remaining.begin(); it != remaining.end(); ++it) {
                    if (it.key() == "confirmed") continue;
                    profileData[it.key()] = it.value().toVariant();
                }
                profileManager->uploadProfile(profileData);
                profileManager->uploadCurrentProfile();  // MCP is one-shot, upload immediately
            } else {
                // Recipe path: use uploadRecipeProfile() — same as RecipeEditorPage/SimpleProfileEditorPage
                QVariantMap currentParams = profileManager->getOrConvertRecipeParams();
                // Nothing validates incoming keys against the declared schema, so
                // an unrecognised one lands here, is dropped by
                // RecipeParams::fromVariantMap, and used to still draw a
                // success:true. fillPressure, fillFlow, fillTimeout and
                // infuseEnabled were all valid and effective before they were
                // removed, so a client written against the older schema would
                // believe its edit took effect. Report them instead.
                for (auto it = remaining.begin(); it != remaining.end(); ++it) {
                    if (it.key() == "confirmed") continue;
                    if (!currentParams.contains(it.key())) ignoredKeys << it.key();
                    currentParams[it.key()] = it.value().toVariant();
                }
                profileManager->uploadRecipeProfile(currentParams);
                profileManager->uploadCurrentProfile();  // MCP is one-shot, upload immediately
            }

            result["success"] = true;
            if (!retiredKeys.isEmpty()) {
                // These are not typos, so "unrecognised" alone would send the
                // caller hunting for one. Name the replacement instead.
                result["retiredFields"] = QJsonArray::fromStringList(retiredKeys);
                result["retiredNote"] =
                    QStringLiteral("%1 no longer set the per-profile dose. Use 'dose' "
                                   "(grams, 0-100), which sets the value and enables the "
                                   "recommendation, on every editor type.")
                        .arg(retiredKeys.join(QStringLiteral(", ")));
            }
            // Both caveats ride on `message`, not only on their own keys: a
            // client that reads `message` and `success` and skips the siblings
            // — which is most of them — would otherwise be told a clean
            // "Profile updated" for a call that dropped half its arguments.
            QStringList caveats;
            if (!ignoredKeys.isEmpty())
                caveats << QStringLiteral("%1 unrecognised field(s) were IGNORED: %2")
                               .arg(ignoredKeys.size()).arg(ignoredKeys.join(QStringLiteral(", ")));
            if (!retiredKeys.isEmpty())
                caveats << QStringLiteral("%1 no longer set the per-profile dose (use 'dose')")
                               .arg(retiredKeys.join(QStringLiteral(", ")));
            result["message"] = caveats.isEmpty()
                ? QStringLiteral("Profile updated and uploaded to machine. "
                                 "Call profiles_save to persist.")
                : QStringLiteral("Profile updated and uploaded to machine, but %1. "
                                 "Call profiles_save to persist.")
                      .arg(caveats.join(QStringLiteral("; ")));
            if (!ignoredKeys.isEmpty())
                result["ignoredFields"] = QJsonArray::fromStringList(ignoredKeys);
            result["modified"] = true;
            result["editorType"] = editorType;
            return result;
        },
        "settings", McpTierCore);

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
                // Leading-underscore names are reserved for internal files
                // (e.g. _current.json). ProfileStorage::listProfiles filters
                // them out, so a profile saved with such a name would be
                // invisible to profiles_list/refreshProfiles even though
                // the write succeeded — the worst kind of silent failure.
                if (filename.startsWith(QLatin1Char('_'))) {
                    result["error"] = "filename cannot start with underscore "
                        "(reserved for internal files). Use a name like '"
                        + filename.mid(1) + "' instead.";
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
        "settings", McpTierCore);

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
        "settings", McpTierCore);

    // profiles_rename
    registry->registerTool(
        "profiles_rename",
        "Rename a user or downloaded profile in place. Changes only the profile's display title and "
        "keeps the same filename, so favorites, auto-load, and the selected list stay intact. "
        "Built-in profiles are read-only and cannot be renamed — use profiles_save with a new "
        "filename/title to make an editable copy instead.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"filename", QJsonObject{{"type", "string"}, {"description", "Profile filename to rename (without .json)"}}},
                {"title", QJsonObject{{"type", "string"}, {"description", "New display title for the profile"}}},
                {"confirmed", QJsonObject{{"type", "boolean"}, {"description", "Set to true after user confirms this action in chat"}}}
            }},
            {"required", QJsonArray{"filename", "title"}}
        },
        [profileManager](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!profileManager) {
                result["error"] = "Controller not available";
                return result;
            }

            QString filename = args["filename"].toString();
            QString newTitle = args["title"].toString().trimmed();
            if (filename.isEmpty()) {
                result["error"] = "filename is required";
                return result;
            }
            if (newTitle.isEmpty()) {
                result["error"] = "title is required";
                return result;
            }

            if (!profileManager->profileExists(filename)) {
                result["error"] = "Profile not found: " + filename;
                return result;
            }

            if (profileManager->isBuiltInFilename(filename)) {
                result["error"] = "Cannot rename built-in profile '" + filename +
                    "'. Built-in profiles are read-only — use profiles_save with a new "
                    "filename/title to make an editable copy instead.";
                result["filename"] = filename;
                return result;
            }

            // Tool handlers run on the main thread (via ShotServer), so call directly
            bool success = profileManager->renameProfile(filename, newTitle);
            if (success) {
                result["success"] = true;
                result["message"] = "Profile renamed to: " + newTitle;
                result["filename"] = filename;
                result["title"] = newTitle;
            } else {
                result["error"] = "Failed to rename profile: " + filename;
            }
            return result;
        },
        "settings", McpTierCore);

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
        "settings", McpTierCore);
}
