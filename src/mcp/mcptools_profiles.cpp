#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../controllers/maincontroller.h"
#include "../profile/profile.h"

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

            result["filename"] = mainController->currentProfileName();
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
}
