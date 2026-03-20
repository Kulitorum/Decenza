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
    // shots_set_feedback
    registry->registerTool(
        "shots_set_feedback",
        "Record enjoyment rating (0-100) and/or tasting notes for a shot",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"shotId", QJsonObject{{"type", "integer"}, {"description", "Shot ID"}}},
                {"enjoyment", QJsonObject{{"type", "integer"}, {"description", "Enjoyment rating 0-100"}}},
                {"notes", QJsonObject{{"type", "string"}, {"description", "Tasting notes"}}}
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

            QVariantMap metadata;
            if (args.contains("enjoyment"))
                metadata["enjoyment"] = qBound(0, args["enjoyment"].toInt(), 100);
            if (args.contains("notes"))
                metadata["espresso_notes"] = args["notes"].toString();

            if (metadata.isEmpty()) {
                result["error"] = "Provide enjoyment and/or notes";
                return result;
            }

            const QString dbPath = shotHistory->databasePath();
            const QString connName = QString("mcp_feedback_%1").arg(s_mcpWriteConnCounter.fetchAndAddRelaxed(1));

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
                result["message"] = "Feedback saved for shot " + QString::number(shotId);
            } else {
                result["error"] = "Failed to update shot " + QString::number(shotId);
            }
            return result;
        },
        "control");

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
        "Update espresso settings: temperature, target weight, steam/water settings, DYE metadata",
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
        [settings](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!settings) {
                result["error"] = "Settings not available";
                return result;
            }

            QStringList updated;

            // Dispatch to main thread for each setting
            if (args.contains("espressoTemperature")) {
                double v = args["espressoTemperature"].toDouble();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setEspressoTemperature(v); }, Qt::QueuedConnection);
                updated << "espressoTemperature";
            }
            if (args.contains("targetWeight")) {
                double v = args["targetWeight"].toDouble();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setTargetWeight(v); }, Qt::QueuedConnection);
                updated << "targetWeight";
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

    // dialing_apply_change
    registry->registerTool(
        "dialing_apply_change",
        "Apply a dial-in change: adjust grind setting, target weight, temperature, switch profile, or update bean metadata",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"grinderSetting", QJsonObject{{"type", "string"}, {"description", "New grinder setting"}}},
                {"targetWeight", QJsonObject{{"type", "number"}, {"description", "New target weight in grams"}}},
                {"espressoTemperature", QJsonObject{{"type", "number"}, {"description", "New brew temperature in Celsius"}}},
                {"profileFilename", QJsonObject{{"type", "string"}, {"description", "Switch to this profile"}}},
                {"dyeBeanBrand", QJsonObject{{"type", "string"}, {"description", "Update bean brand"}}},
                {"dyeBeanType", QJsonObject{{"type", "string"}, {"description", "Update bean type"}}},
                {"dyeRoastLevel", QJsonObject{{"type", "string"}, {"description", "Update roast level"}}},
                {"confirmed", QJsonObject{{"type", "boolean"}, {"description", "Set to true after user confirms this action in chat"}}}
            }}
        },
        [mainController, settings](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            QStringList applied;

            if (settings) {
                if (args.contains("grinderSetting")) {
                    QString v = args["grinderSetting"].toString();
                    QMetaObject::invokeMethod(settings, [settings, v]() { settings->setDyeGrinderSetting(v); }, Qt::QueuedConnection);
                    applied << "grinderSetting";
                }
                if (args.contains("targetWeight")) {
                    double v = args["targetWeight"].toDouble();
                    QMetaObject::invokeMethod(settings, [settings, v]() { settings->setTargetWeight(v); }, Qt::QueuedConnection);
                    applied << "targetWeight";
                }
                if (args.contains("espressoTemperature")) {
                    double v = args["espressoTemperature"].toDouble();
                    QMetaObject::invokeMethod(settings, [settings, v]() { settings->setEspressoTemperature(v); }, Qt::QueuedConnection);
                    applied << "espressoTemperature";
                }
                if (args.contains("dyeBeanBrand")) {
                    QString v = args["dyeBeanBrand"].toString();
                    QMetaObject::invokeMethod(settings, [settings, v]() { settings->setDyeBeanBrand(v); }, Qt::QueuedConnection);
                    applied << "dyeBeanBrand";
                }
                if (args.contains("dyeBeanType")) {
                    QString v = args["dyeBeanType"].toString();
                    QMetaObject::invokeMethod(settings, [settings, v]() { settings->setDyeBeanType(v); }, Qt::QueuedConnection);
                    applied << "dyeBeanType";
                }
                if (args.contains("dyeRoastLevel")) {
                    QString v = args["dyeRoastLevel"].toString();
                    QMetaObject::invokeMethod(settings, [settings, v]() { settings->setDyeRoastLevel(v); }, Qt::QueuedConnection);
                    applied << "dyeRoastLevel";
                }
            }

            if (mainController && args.contains("profileFilename")) {
                QString filename = args["profileFilename"].toString();
                if (mainController->profileExists(filename)) {
                    QMetaObject::invokeMethod(mainController, [mainController, filename]() {
                        mainController->loadProfile(filename);
                    }, Qt::QueuedConnection);
                    applied << "profileFilename";
                } else {
                    result["profileError"] = "Profile not found: " + filename;
                }
            }

            if (applied.isEmpty() && !result.contains("profileError")) {
                result["error"] = "No valid changes provided";
                return result;
            }

            result["success"] = true;
            result["applied"] = QJsonArray::fromStringList(applied);
            return result;
        },
        "settings");
}
