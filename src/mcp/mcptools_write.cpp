// TODO: Move SQL queries to background thread per CLAUDE.md design principle.
// Current tool handler architecture (synchronous QJsonObject return) prevents this.
// Requires refactoring McpToolHandler to support async responses.

#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../history/shothistorystorage.h"
#include "../controllers/maincontroller.h"
#include "../core/settings.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QAtomicInt>

static QAtomicInt s_mcpWriteConnCounter{0};

void registerWriteTools(McpToolRegistry* registry, MainController* mainController,
                        ShotHistoryStorage* shotHistory, Settings* settings)
{
    // shots_update — replaces shots_set_feedback with full metadata editing (same as QML)
    registry->registerTool(
        "shots_update",
        "Update any metadata field on a shot. Supports all fields the QML shot editor can change: "
        "enjoyment, notes, dose, yield, bean info, grinder info, barista, TDS, EY.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"shotId", QJsonObject{{"type", "integer"}, {"description", "Shot ID"}}},
                {"enjoyment", QJsonObject{{"type", "integer"}, {"description", "Enjoyment rating 0-100"}}},
                {"notes", QJsonObject{{"type", "string"}, {"description", "Tasting notes"}}},
                {"doseWeight", QJsonObject{{"type", "number"}, {"description", "Dose weight in grams"}}},
                {"drinkWeight", QJsonObject{{"type", "number"}, {"description", "Yield/drink weight in grams"}}},
                {"beanBrand", QJsonObject{{"type", "string"}, {"description", "Bean brand"}}},
                {"beanType", QJsonObject{{"type", "string"}, {"description", "Bean type/name"}}},
                {"roastLevel", QJsonObject{{"type", "string"}, {"description", "Roast level"}}},
                {"roastDate", QJsonObject{{"type", "string"}, {"description", "Roast date (YYYY-MM-DD)"}}},
                {"grinderBrand", QJsonObject{{"type", "string"}, {"description", "Grinder brand"}}},
                {"grinderModel", QJsonObject{{"type", "string"}, {"description", "Grinder model"}}},
                {"grinderBurrs", QJsonObject{{"type", "string"}, {"description", "Grinder burrs"}}},
                {"grinderSetting", QJsonObject{{"type", "string"}, {"description", "Grinder setting"}}},
                {"barista", QJsonObject{{"type", "string"}, {"description", "Barista name"}}},
                {"drinkTds", QJsonObject{{"type", "number"}, {"description", "TDS measurement"}}},
                {"drinkEy", QJsonObject{{"type", "number"}, {"description", "Extraction yield percentage"}}}
            }},
            {"required", QJsonArray{"shotId"}}
        },
        [shotHistory](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!shotHistory || !shotHistory->isReady()) {
                result["error"] = "Shot history not available";
                return result;
            }

            qint64 shotId = args["shotId"].toInteger();
            if (shotId <= 0) {
                result["error"] = "Valid shotId is required";
                return result;
            }

            // Map MCP parameter names to DB column names
            QVariantMap metadata;
            if (args.contains("enjoyment"))
                metadata["enjoyment"] = qBound(0, args["enjoyment"].toInt(), 100);
            if (args.contains("notes"))
                metadata["espresso_notes"] = args["notes"].toString();
            if (args.contains("doseWeight"))
                metadata["dose_weight"] = args["doseWeight"].toDouble();
            if (args.contains("drinkWeight"))
                metadata["drink_weight"] = args["drinkWeight"].toDouble();
            if (args.contains("beanBrand"))
                metadata["bean_brand"] = args["beanBrand"].toString();
            if (args.contains("beanType"))
                metadata["bean_type"] = args["beanType"].toString();
            if (args.contains("roastLevel"))
                metadata["roast_level"] = args["roastLevel"].toString();
            if (args.contains("roastDate"))
                metadata["roast_date"] = args["roastDate"].toString();
            if (args.contains("grinderBrand"))
                metadata["grinder_brand"] = args["grinderBrand"].toString();
            if (args.contains("grinderModel"))
                metadata["grinder_model"] = args["grinderModel"].toString();
            if (args.contains("grinderBurrs"))
                metadata["grinder_burrs"] = args["grinderBurrs"].toString();
            if (args.contains("grinderSetting"))
                metadata["grinder_setting"] = args["grinderSetting"].toString();
            if (args.contains("barista"))
                metadata["barista"] = args["barista"].toString();
            if (args.contains("drinkTds"))
                metadata["drink_tds"] = args["drinkTds"].toDouble();
            if (args.contains("drinkEy"))
                metadata["drink_ey"] = args["drinkEy"].toDouble();

            if (metadata.isEmpty()) {
                result["error"] = "Provide at least one field to update";
                return result;
            }

            const QString dbPath = shotHistory->databasePath();
            const QString connName = QString("mcp_update_%1").arg(s_mcpWriteConnCounter.fetchAndAddRelaxed(1));

            bool ok = false;
            {
                QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
                db.setDatabaseName(dbPath);
                if (db.open())
                    ok = ShotHistoryStorage::updateShotMetadataStatic(db, shotId, metadata);
            }
            QSqlDatabase::removeDatabase(connName);

            if (ok) {
                result["success"] = true;
                QStringList fields;
                for (auto it = metadata.begin(); it != metadata.end(); ++it)
                    fields << it.key();
                result["updated"] = QJsonArray::fromStringList(fields);
                result["message"] = "Shot " + QString::number(shotId) + " updated";
            } else {
                result["error"] = "Failed to update shot " + QString::number(shotId);
            }
            return result;
        },
        "control");

    // shots_delete
    registry->registerTool(
        "shots_delete",
        "Delete a shot by ID. This is permanent and cannot be undone.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"shotId", QJsonObject{{"type", "integer"}, {"description", "Shot ID to delete"}}},
                {"confirmed", QJsonObject{{"type", "boolean"}, {"description", "Set to true after user confirms this action in chat"}}}
            }},
            {"required", QJsonArray{"shotId"}}
        },
        [shotHistory](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!shotHistory || !shotHistory->isReady()) {
                result["error"] = "Shot history not available";
                return result;
            }

            // Debug: log what we received
            qDebug() << "shots_delete args:" << QJsonDocument(args).toJson(QJsonDocument::Compact);
            qDebug() << "shots_delete shotId type:" << args["shotId"].type() << "value:" << args["shotId"];

            qint64 shotId = args["shotId"].toInteger();
            if (shotId <= 0)
                shotId = static_cast<qint64>(args["shotId"].toDouble());
            if (shotId <= 0) {
                result["error"] = "Valid shotId is required (got: " +
                    QString::fromUtf8(QJsonDocument(QJsonObject{{"shotId", args["shotId"]}}).toJson(QJsonDocument::Compact)) + ")";
                return result;
            }

            // requestDeleteShot is async — queues the delete on a background thread
            shotHistory->requestDeleteShot(shotId);

            result["success"] = true;
            result["message"] = "Shot " + QString::number(shotId) + " deletion queued";
            return result;
        },
        "settings");

    // profiles_set_active
    registry->registerTool(
        "profiles_set_active",
        "Load and activate a profile by filename",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"filename", QJsonObject{{"type", "string"}, {"description", "Profile filename to activate"}}},
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

            QMetaObject::invokeMethod(mainController, [mainController, filename]() {
                mainController->loadProfile(filename);
            }, Qt::QueuedConnection);

            result["success"] = true;
            result["message"] = "Profile activation queued: " + filename;
            return result;
        },
        "settings");

    // settings_set
    registry->registerTool(
        "settings_set",
        "Update espresso settings: temperature, target weight, steam/water settings, DYE metadata. "
        "For temperature and weight changes, also use this instead of the removed dialing_apply_change.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"espressoTemperature", QJsonObject{{"type", "number"}, {"description", "Brew temperature in Celsius"}}},
                {"targetWeight", QJsonObject{{"type", "number"}, {"description", "Target shot weight in grams"}}},
                {"steamTemperature", QJsonObject{{"type", "number"}, {"description", "Steam temperature in Celsius"}}},
                {"waterTemperature", QJsonObject{{"type", "number"}, {"description", "Hot water temperature in Celsius"}}},
                {"waterVolume", QJsonObject{{"type", "integer"}, {"description", "Hot water volume in ml"}}},
                {"dyeBeanBrand", QJsonObject{{"type", "string"}, {"description", "Bean brand"}}},
                {"dyeBeanType", QJsonObject{{"type", "string"}, {"description", "Bean type/name"}}},
                {"dyeRoastLevel", QJsonObject{{"type", "string"}, {"description", "Roast level"}}},
                {"dyeGrinderSetting", QJsonObject{{"type", "string"}, {"description", "Grinder setting"}}},
                {"dyeBeanWeight", QJsonObject{{"type", "number"}, {"description", "Dose weight in grams"}}},
                {"confirmed", QJsonObject{{"type", "boolean"}, {"description", "Set to true after user confirms this action in chat"}}}
            }}
        },
        [mainController, settings](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!settings) {
                result["error"] = "Settings not available";
                return result;
            }

            QStringList updated;

            // Temperature and weight changes use the same code path as the QML editors:
            // recipe profiles → uploadRecipeProfile(), advanced → uploadProfile()
            bool needsProfileUpdate = args.contains("espressoTemperature") || args.contains("targetWeight");
            if (needsProfileUpdate && mainController) {
                QString editorType = mainController->currentEditorType();
                if (editorType == "advanced") {
                    // Advanced path: use uploadProfile() — same as ProfileEditorPage
                    QVariantMap profileData = mainController->getCurrentProfile();
                    if (args.contains("espressoTemperature")) {
                        profileData["espresso_temperature"] = args["espressoTemperature"].toDouble();
                        updated << "espressoTemperature";
                    }
                    if (args.contains("targetWeight")) {
                        profileData["target_weight"] = args["targetWeight"].toDouble();
                        updated << "targetWeight";
                    }
                    mainController->uploadProfile(profileData);
                } else {
                    // Recipe path: use uploadRecipeProfile() — same as RecipeEditorPage/SimpleProfileEditorPage
                    QVariantMap currentParams = mainController->getOrConvertRecipeParams();
                    if (args.contains("espressoTemperature")) {
                        double v = args["espressoTemperature"].toDouble();
                        // Set all temperature fields uniformly (matching QML editor behavior)
                        currentParams["fillTemperature"] = v;
                        currentParams["pourTemperature"] = v;
                        currentParams["tempStart"] = v;
                        currentParams["tempPreinfuse"] = v;
                        currentParams["tempHold"] = v;
                        currentParams["tempDecline"] = v;
                        updated << "espressoTemperature";
                    }
                    if (args.contains("targetWeight")) {
                        currentParams["targetWeight"] = args["targetWeight"].toDouble();
                        updated << "targetWeight";
                    }
                    mainController->uploadRecipeProfile(currentParams);
                }
            }

            if (args.contains("steamTemperature")) {
                double v = args["steamTemperature"].toDouble();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setSteamTemperature(v); }, Qt::QueuedConnection);
                updated << "steamTemperature";
            }
            if (args.contains("waterTemperature")) {
                double v = args["waterTemperature"].toDouble();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setWaterTemperature(v); }, Qt::QueuedConnection);
                updated << "waterTemperature";
            }
            if (args.contains("waterVolume")) {
                int v = args["waterVolume"].toInt();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setWaterVolume(v); }, Qt::QueuedConnection);
                updated << "waterVolume";
            }
            if (args.contains("dyeBeanBrand")) {
                QString v = args["dyeBeanBrand"].toString();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setDyeBeanBrand(v); }, Qt::QueuedConnection);
                updated << "dyeBeanBrand";
            }
            if (args.contains("dyeBeanType")) {
                QString v = args["dyeBeanType"].toString();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setDyeBeanType(v); }, Qt::QueuedConnection);
                updated << "dyeBeanType";
            }
            if (args.contains("dyeRoastLevel")) {
                QString v = args["dyeRoastLevel"].toString();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setDyeRoastLevel(v); }, Qt::QueuedConnection);
                updated << "dyeRoastLevel";
            }
            if (args.contains("dyeGrinderSetting")) {
                QString v = args["dyeGrinderSetting"].toString();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setDyeGrinderSetting(v); }, Qt::QueuedConnection);
                updated << "dyeGrinderSetting";
            }
            if (args.contains("dyeBeanWeight")) {
                double v = args["dyeBeanWeight"].toDouble();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setDyeBeanWeight(v); }, Qt::QueuedConnection);
                updated << "dyeBeanWeight";
            }

            if (updated.isEmpty()) {
                result["error"] = "No valid settings provided";
                return result;
            }

            result["success"] = true;
            result["updated"] = QJsonArray::fromStringList(updated);
            return result;
        },
        "settings");

    // dialing_suggest_change
    registry->registerTool(
        "dialing_suggest_change",
        "Suggest a parameter change to the user with rationale (shown as a notification in the app)",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"parameter", QJsonObject{{"type", "string"}, {"description", "What to change: grind, dose, yield, temperature, profile"}}},
                {"suggestion", QJsonObject{{"type", "string"}, {"description", "The suggested change (e.g., 'Grind 2 clicks finer')"}}},
                {"rationale", QJsonObject{{"type", "string"}, {"description", "Why this change is recommended"}}}
            }},
            {"required", QJsonArray{"parameter", "suggestion", "rationale"}}
        },
        [](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            result["parameter"] = args["parameter"].toString();
            result["suggestion"] = args["suggestion"].toString();
            result["rationale"] = args["rationale"].toString();
            result["status"] = "suggestion_displayed";
            return result;
        },
        "control");
}
