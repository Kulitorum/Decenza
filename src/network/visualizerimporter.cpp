#include "visualizerimporter.h"
#include "../controllers/maincontroller.h"
#include "../core/settings.h"
#include "../core/profilestorage.h"
#include "../profile/recipeparams.h"
#include "../profile/recipegenerator.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>

VisualizerImporter::VisualizerImporter(MainController* controller, Settings* settings, QObject* parent)
    : QObject(parent)
    , m_controller(controller)
    , m_settings(settings)
    , m_networkManager(new QNetworkAccessManager(this))
{
}

QString VisualizerImporter::authHeader() const {
    if (!m_settings) return QString();

    QString username = m_settings->value("visualizer/username", "").toString();
    QString password = m_settings->value("visualizer/password", "").toString();

    if (username.isEmpty() || password.isEmpty()) {
        return QString();
    }

    QString credentials = username + ":" + password;
    QByteArray base64 = credentials.toUtf8().toBase64();
    return "Basic " + QString::fromLatin1(base64);
}

QString VisualizerImporter::extractShotId(const QString& url) const {
    QRegularExpression re(R"(visualizer\.coffee/(?:api/)?shots/([a-f0-9-]{36}))");
    QRegularExpressionMatch match = re.match(url);

    if (match.hasMatch()) {
        return match.captured(1);
    }

    return QString();
}

void VisualizerImporter::importFromShotId(const QString& shotId) {
    if (shotId.isEmpty()) {
        m_lastError = "No shot ID provided";
        emit lastErrorChanged();
        emit importFailed(m_lastError);
        return;
    }

    if (m_importing) {
        return;
    }

    m_importing = true;
    m_requestType = RequestType::None;
    emit importingChanged();

    QString url = QString(VISUALIZER_PROFILE_API).arg(shotId);
    qDebug() << "Fetching Visualizer profile from:" << url;

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onFetchFinished(reply);
    });
}

void VisualizerImporter::importFromShotIdWithName(const QString& shotId, const QString& customName) {
    if (shotId.isEmpty() || customName.isEmpty()) {
        m_lastError = "Shot ID and name are required";
        emit lastErrorChanged();
        emit importFailed(m_lastError);
        return;
    }

    if (m_importing) {
        return;
    }

    m_importing = true;
    m_requestType = RequestType::RenamedImport;
    m_customImportName = customName;
    emit importingChanged();

    QString url = QString(VISUALIZER_PROFILE_API).arg(shotId);
    qDebug() << "Fetching Visualizer profile for renamed import:" << url << "as" << customName;

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onFetchFinished(reply);
    });
}

void VisualizerImporter::importFromShareCode(const QString& shareCode) {
    QString code = shareCode.trimmed();

    if (code.isEmpty()) {
        m_lastError = "No share code provided";
        emit lastErrorChanged();
        emit importFailed(m_lastError);
        return;
    }

    if (m_importing) {
        return;
    }

    m_importing = true;
    m_requestType = RequestType::ShareCode;
    emit importingChanged();

    QString url = QString(VISUALIZER_SHARED_API).arg(code);
    qDebug() << "Fetching Visualizer shot from share code:" << url;

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QString auth = authHeader();
    if (!auth.isEmpty()) {
        request.setRawHeader("Authorization", auth.toUtf8());
    }

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onFetchFinished(reply);
    });
}

void VisualizerImporter::fetchSharedShots() {
    if (m_fetching) {
        return;
    }

    QString auth = authHeader();
    if (auth.isEmpty()) {
        m_lastError = "Visualizer credentials not configured";
        emit lastErrorChanged();
        emit importFailed(m_lastError);
        return;
    }

    m_fetching = true;
    m_requestType = RequestType::FetchList;
    emit fetchingChanged();

    QString url = "https://visualizer.coffee/api/shots/shared?code=";
    qDebug() << "Fetching user's shared shots...";

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", auth.toUtf8());

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onFetchFinished(reply);
    });
}

bool VisualizerImporter::compareProfileFrames(const Profile& a, const Profile& b) const {
    const auto& stepsA = a.steps();
    const auto& stepsB = b.steps();

    if (stepsA.size() != stepsB.size()) {
        return false;
    }

    for (int i = 0; i < stepsA.size(); i++) {
        const ProfileFrame& fa = stepsA[i];
        const ProfileFrame& fb = stepsB[i];

        // Compare all frame parameters that affect extraction
        if (qAbs(fa.temperature - fb.temperature) > 0.1) return false;
        if (fa.sensor != fb.sensor) return false;
        if (fa.pump != fb.pump) return false;
        if (fa.transition != fb.transition) return false;
        if (qAbs(fa.pressure - fb.pressure) > 0.1) return false;
        if (qAbs(fa.flow - fb.flow) > 0.1) return false;
        if (qAbs(fa.seconds - fb.seconds) > 0.1) return false;
        if (qAbs(fa.volume - fb.volume) > 0.1) return false;

        // Exit conditions
        if (fa.exitIf != fb.exitIf) return false;
        if (fa.exitIf) {
            if (fa.exitType != fb.exitType) return false;
            if (qAbs(fa.exitPressureOver - fb.exitPressureOver) > 0.1) return false;
            if (qAbs(fa.exitPressureUnder - fb.exitPressureUnder) > 0.1) return false;
            if (qAbs(fa.exitFlowOver - fb.exitFlowOver) > 0.1) return false;
            if (qAbs(fa.exitFlowUnder - fb.exitFlowUnder) > 0.1) return false;
        }

        // Limiter
        if (qAbs(fa.maxFlowOrPressure - fb.maxFlowOrPressure) > 0.1) return false;
        if (qAbs(fa.maxFlowOrPressureRange - fb.maxFlowOrPressureRange) > 0.1) return false;
    }

    return true;
}

Profile VisualizerImporter::loadLocalProfile(const QString& filename) const {
    ProfileStorage* storage = m_controller->profileStorage();

    // Try profile storage first
    if (storage && storage->isConfigured() && storage->profileExists(filename)) {
        QString content = storage->readProfile(filename);
        if (!content.isEmpty()) {
            return Profile::loadFromJsonString(content);
        }
    }

    // Try local downloaded folder
    QString localPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    localPath += "/profiles/downloaded/" + filename + ".json";
    if (QFile::exists(localPath)) {
        return Profile::loadFromFile(localPath);
    }

    // Try built-in profiles
    QString builtinPath = ":/profiles/" + filename + ".json";
    if (QFile::exists(builtinPath)) {
        return Profile::loadFromFile(builtinPath);
    }

    return Profile();  // Empty/invalid profile
}

QVariantMap VisualizerImporter::checkProfileStatus(const QString& profileTitle, const Profile* incomingProfile) {
    QVariantMap result;
    result["exists"] = false;
    result["identical"] = false;
    result["source"] = "";
    result["filename"] = "";

    QString filename = m_controller->titleToFilename(profileTitle);
    result["filename"] = filename;

    ProfileStorage* storage = m_controller->profileStorage();
    QString foundPath;
    QString source;

    // Check in profile storage
    if (storage && storage->isConfigured() && storage->profileExists(filename)) {
        result["exists"] = true;
        source = "D";  // Downloaded/Visualizer
    }

    // Check local downloaded folder
    if (!result["exists"].toBool()) {
        QString localPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        localPath += "/profiles/downloaded/" + filename + ".json";
        if (QFile::exists(localPath)) {
            result["exists"] = true;
            source = "D";
            foundPath = localPath;
        }
    }

    // Check built-in profiles
    if (!result["exists"].toBool()) {
        QString builtinPath = ":/profiles/" + filename + ".json";
        if (QFile::exists(builtinPath)) {
            result["exists"] = true;
            source = "B";
            foundPath = builtinPath;
        }
    }

    result["source"] = source;

    // If exists and we have incoming profile, compare frames
    if (result["exists"].toBool() && incomingProfile && incomingProfile->isValid()) {
        Profile localProfile = loadLocalProfile(filename);
        if (localProfile.isValid()) {
            bool identical = compareProfileFrames(*incomingProfile, localProfile);
            result["identical"] = identical;
            qDebug() << "Profile" << profileTitle << "comparison:" << (identical ? "identical" : "different");
        }
    }

    return result;
}

void VisualizerImporter::importSelectedShots(const QStringList& shotIds, bool overwriteExisting) {
    if (shotIds.isEmpty()) {
        emit batchImportComplete(0, 0);
        return;
    }

    if (m_importing) {
        return;
    }

    m_importing = true;
    m_requestType = RequestType::BatchImport;
    m_batchShotIds = shotIds;
    m_batchOverwrite = overwriteExisting;
    m_batchImported = 0;
    m_batchSkipped = 0;
    emit importingChanged();

    qDebug() << "Starting batch import of" << shotIds.size() << "profiles";

    // Start fetching first profile
    QString shotId = m_batchShotIds.takeFirst();
    QString url = QString(VISUALIZER_PROFILE_API).arg(shotId);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onProfileFetchFinished(reply);
    });
}

void VisualizerImporter::onFetchFinished(QNetworkReply* reply) {
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        m_importing = false;
        m_fetching = false;
        m_requestType = RequestType::None;
        emit importingChanged();
        emit fetchingChanged();

        if (statusCode == 401) {
            m_lastError = "Invalid Visualizer credentials";
        } else {
            m_lastError = QString("Network error: %1").arg(reply->errorString());
        }
        qWarning() << "Visualizer request failed:" << m_lastError;
        emit lastErrorChanged();
        emit importFailed(m_lastError);
        return;
    }

    QByteArray data = reply->readAll();
    qDebug() << "Visualizer API response:" << data.left(2000);

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        m_importing = false;
        m_fetching = false;
        m_requestType = RequestType::None;
        emit importingChanged();
        emit fetchingChanged();
        m_lastError = QString("JSON parse error: %1").arg(parseError.errorString());
        qWarning() << "Visualizer request failed:" << m_lastError;
        emit lastErrorChanged();
        emit importFailed(m_lastError);
        return;
    }

    // Handle FetchList request - store shots and fetch profile details
    if (m_requestType == RequestType::FetchList) {
        if (!doc.isArray()) {
            m_fetching = false;
            emit fetchingChanged();
            m_lastError = "Expected array of shared shots";
            emit lastErrorChanged();
            emit importFailed(m_lastError);
            return;
        }

        QJsonArray array = doc.array();
        qDebug() << "Received" << array.size() << "shared shots, fetching profile details...";

        m_pendingShots.clear();

        for (const auto& shotVal : array) {
            QJsonObject shot = shotVal.toObject();
            QVariantMap shotData;

            shotData["id"] = shot["id"].toString();
            shotData["profile_title"] = shot["profile_title"].toString();
            shotData["profile_url"] = shot["profile_url"].toString();
            shotData["duration"] = shot["duration"].toDouble();
            shotData["bean_brand"] = shot["bean_brand"].toString();
            shotData["bean_type"] = shot["bean_type"].toString();
            shotData["user_name"] = shot["user_name"].toString();
            shotData["start_time"] = shot["start_time"].toString();
            shotData["bean_weight"] = shot["bean_weight"].toString();
            shotData["drink_weight"] = shot["drink_weight"].toString();
            shotData["grinder_model"] = shot["grinder_model"].toString();
            shotData["grinder_setting"] = shot["grinder_setting"].toString();

            // Initial status check (without frame comparison yet)
            QVariantMap status = checkProfileStatus(shot["profile_title"].toString());
            shotData["exists"] = status["exists"];
            shotData["identical"] = false;  // Will be updated after fetching profile
            shotData["source"] = status["source"];
            shotData["filename"] = status["filename"];
            shotData["selected"] = false;

            m_pendingShots.append(shotData);
        }

        // Start fetching profile details for comparison
        if (!m_pendingShots.isEmpty()) {
            fetchProfileDetailsForShots();
        } else {
            m_fetching = false;
            emit fetchingChanged();
            m_sharedShots = m_pendingShots;
            emit sharedShotsChanged();
        }
        return;
    }

    // Handle ShareCode request
    QJsonObject json;

    if (doc.isArray()) {
        QJsonArray array = doc.array();
        if (array.isEmpty()) {
            m_importing = false;
            m_requestType = RequestType::None;
            emit importingChanged();
            m_lastError = "No shared shots found";
            emit lastErrorChanged();
            emit importFailed(m_lastError);
            return;
        }
        json = array.first().toObject();
    } else {
        json = doc.object();
    }

    if (json.contains("error")) {
        m_importing = false;
        m_requestType = RequestType::None;
        emit importingChanged();
        m_lastError = json["error"].toString("Unknown error");
        qWarning() << "Visualizer API error:" << m_lastError;
        emit lastErrorChanged();
        emit importFailed(m_lastError);
        return;
    }

    // If we're fetching from share code, get the profile
    if (m_requestType == RequestType::ShareCode) {
        QString shotId = json["id"].toString();
        if (shotId.isEmpty()) {
            m_importing = false;
            m_requestType = RequestType::None;
            emit importingChanged();
            m_lastError = "Share code response missing shot ID";
            emit lastErrorChanged();
            emit importFailed(m_lastError);
            return;
        }

        qDebug() << "Got shot ID from share code:" << shotId << "- fetching profile...";
        m_requestType = RequestType::FetchProfile;

        QString url = QString(VISUALIZER_PROFILE_API).arg(shotId);
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QNetworkReply* profileReply = m_networkManager->get(request);
        connect(profileReply, &QNetworkReply::finished, this, [this, profileReply]() {
            onFetchFinished(profileReply);
        });
        return;
    }

    // We have the profile data - parse and save
    bool isRenamedImport = (m_requestType == RequestType::RenamedImport);
    QString customName = m_customImportName;

    m_importing = false;
    m_requestType = RequestType::None;
    m_customImportName.clear();
    emit importingChanged();

    Profile profile = parseVisualizerProfile(json);

    if (!profile.isValid()) {
        m_lastError = "Invalid profile: " + profile.validationErrors().join(", ");
        qWarning() << "Visualizer import failed:" << m_lastError;
        emit lastErrorChanged();
        emit importFailed(m_lastError);
        return;
    }

    // For renamed imports, use the custom name and save directly
    if (isRenamedImport && !customName.isEmpty()) {
        profile.setTitle(customName);

        // Save to downloaded folder (not ProfileStorage) so it's tagged correctly
        QString downloadedPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/profiles/downloaded";
        QDir().mkpath(downloadedPath);

        QString filename = m_controller->titleToFilename(customName);
        QString fullPath = downloadedPath + "/" + filename + ".json";

        if (profile.saveToFile(fullPath)) {
            qDebug() << "Successfully imported renamed profile to downloaded folder:" << customName;
            if (m_controller) {
                m_controller->refreshProfiles();
            }
            emit importSuccess(customName);
            // Refresh shared shots list to update status
            fetchSharedShots();
        } else {
            m_lastError = "Failed to save profile";
            emit lastErrorChanged();
            emit importFailed(m_lastError);
        }
        return;
    }

    int result = saveImportedProfile(profile);
    if (result == 1) {
        qDebug() << "Successfully imported profile:" << profile.title();
        emit importSuccess(profile.title());
        // Refresh shared shots list to update status
        fetchSharedShots();
    } else if (result == -1) {
        m_lastError = "Failed to save profile";
        emit lastErrorChanged();
        emit importFailed(m_lastError);
    }
}

void VisualizerImporter::onProfileFetchFinished(QNetworkReply* reply) {
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "Failed to fetch profile:" << reply->errorString();
        m_batchSkipped++;

        // Continue with next profile
        if (!m_batchShotIds.isEmpty()) {
            QString shotId = m_batchShotIds.takeFirst();
            QString url = QString(VISUALIZER_PROFILE_API).arg(shotId);
            QNetworkRequest request(url);
            request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            QNetworkReply* nextReply = m_networkManager->get(request);
            connect(nextReply, &QNetworkReply::finished, this, [this, nextReply]() {
                onProfileFetchFinished(nextReply);
            });
        } else {
            // Done with batch
            m_importing = false;
            m_requestType = RequestType::None;
            emit importingChanged();
            emit batchImportComplete(m_batchImported, m_batchSkipped);
            m_controller->refreshProfiles();
        }
        return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);

    if (doc.isNull() || !doc.isObject()) {
        m_batchSkipped++;
    } else {
        Profile profile = parseVisualizerProfile(doc.object());

        if (profile.isValid()) {
            QString filename = m_controller->titleToFilename(profile.title());
            ProfileStorage* storage = m_controller->profileStorage();

            bool exists = false;
            if (storage && storage->isConfigured()) {
                exists = storage->profileExists(filename);
            }
            if (!exists) {
                QString localPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
                localPath += "/profiles/downloaded/" + filename + ".json";
                exists = QFile::exists(localPath);
            }

            if (exists && !m_batchOverwrite) {
                qDebug() << "Skipping existing profile:" << profile.title();
                m_batchSkipped++;
            } else {
                // Save the profile
                bool saved = false;
                if (storage && storage->isConfigured()) {
                    saved = storage->writeProfile(filename, profile.toJsonString());
                }
                if (!saved) {
                    QString localPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
                    localPath += "/profiles/downloaded";
                    QDir().mkpath(localPath);
                    saved = profile.saveToFile(localPath + "/" + filename + ".json");
                }

                if (saved) {
                    qDebug() << "Imported profile:" << profile.title();
                    m_batchImported++;
                } else {
                    m_batchSkipped++;
                }
            }
        } else {
            m_batchSkipped++;
        }
    }

    // Continue with next profile or finish
    if (!m_batchShotIds.isEmpty()) {
        QString shotId = m_batchShotIds.takeFirst();
        QString url = QString(VISUALIZER_PROFILE_API).arg(shotId);
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        QNetworkReply* nextReply = m_networkManager->get(request);
        connect(nextReply, &QNetworkReply::finished, this, [this, nextReply]() {
            onProfileFetchFinished(nextReply);
        });
    } else {
        m_importing = false;
        m_requestType = RequestType::None;
        emit importingChanged();
        emit batchImportComplete(m_batchImported, m_batchSkipped);
        m_controller->refreshProfiles();
    }
}

Profile VisualizerImporter::parseVisualizerProfile(const QJsonObject& json) {
    Profile profile;

    profile.setTitle(json["title"].toString("Imported Profile"));
    profile.setAuthor(json["author"].toString());
    profile.setNotes(json["notes"].toString());
    profile.setBeverageType(json["beverage_type"].toString("espresso"));

    QString profileType = json["legacy_profile_type"].toString();
    if (profileType.isEmpty()) {
        profileType = json["profile_type"].toString("settings_2c");
    }
    profile.setProfileType(profileType);

    QJsonValue targetWeight = json["target_weight"];
    if (targetWeight.isString()) {
        profile.setTargetWeight(targetWeight.toString().toDouble());
    } else {
        profile.setTargetWeight(targetWeight.toDouble(36.0));
    }

    QJsonValue targetVolume = json["target_volume"];
    if (targetVolume.isString()) {
        profile.setTargetVolume(targetVolume.toString().toDouble());
    } else {
        profile.setTargetVolume(targetVolume.toDouble(0.0));
    }

    QJsonArray stepsArray = json["steps"].toArray();
    for (const auto& stepVal : stepsArray) {
        ProfileFrame frame = parseVisualizerStep(stepVal.toObject());
        profile.addStep(frame);
    }

    // Handle recipe mode for simple profiles (2a/2b) that may not have pre-generated steps
    // If steps are empty but we have recipe data, generate frames from the recipe
    bool isRecipeMode = json["is_recipe_mode"].toBool(false);
    if (profile.steps().isEmpty() && isRecipeMode && json.contains("recipe")) {
        qDebug() << "Profile has no steps but is recipe mode - generating frames from recipe";
        RecipeParams recipeParams = RecipeParams::fromJson(json["recipe"].toObject());
        QList<ProfileFrame> frames = RecipeGenerator::generateFrames(recipeParams);
        for (const auto& frame : frames) {
            profile.addStep(frame);
        }
        profile.setRecipeMode(true);
        profile.setRecipeParams(recipeParams);
    }

    if (!profile.steps().isEmpty()) {
        profile.setEspressoTemperature(profile.steps().first().temperature);
    }

    int preinfuseCount = 0;
    for (const auto& step : profile.steps()) {
        if (step.exitIf) {
            preinfuseCount++;
        } else {
            break;
        }
    }
    profile.setPreinfuseFrameCount(preinfuseCount);

    qDebug() << "Parsed Visualizer profile:" << profile.title()
             << "with" << profile.steps().size() << "steps";

    return profile;
}

ProfileFrame VisualizerImporter::parseVisualizerStep(const QJsonObject& json) {
    ProfileFrame frame;

    auto toDouble = [](const QJsonValue& val, double defaultVal = 0.0) -> double {
        if (val.isString()) {
            return val.toString().toDouble();
        }
        return val.toDouble(defaultVal);
    };

    frame.name = json["name"].toString();
    frame.temperature = toDouble(json["temperature"], 93.0);
    frame.sensor = json["sensor"].toString("coffee");
    frame.pump = json["pump"].toString("pressure");
    frame.transition = json["transition"].toString("fast");
    frame.pressure = toDouble(json["pressure"], 9.0);
    frame.flow = toDouble(json["flow"], 2.0);
    frame.seconds = toDouble(json["seconds"], 30.0);
    frame.volume = toDouble(json["volume"], 0.0);

    // Handle exit conditions - support both nested "exit" object format (Visualizer)
    // and flat field format (DE1 app native)
    QJsonObject exitObj = json["exit"].toObject();
    if (!exitObj.isEmpty()) {
        // Nested format: {"exit": {"type": "pressure", "condition": "over", "value": 4}}
        frame.exitIf = true;
        QString exitType = exitObj["type"].toString();
        QString condition = exitObj["condition"].toString();
        double value = toDouble(exitObj["value"]);

        frame.exitType = exitType + "_" + condition;

        if (exitType == "pressure") {
            if (condition == "over") {
                frame.exitPressureOver = value;
            } else {
                frame.exitPressureUnder = value;
            }
        } else if (exitType == "flow") {
            if (condition == "over") {
                frame.exitFlowOver = value;
            } else {
                frame.exitFlowUnder = value;
            }
        }
    } else if (json.contains("exit_if")) {
        // Flat format: {"exit_if": true, "exit_type": "pressure_over", "exit_pressure_over": 4}
        frame.exitIf = json["exit_if"].toBool(false);
        frame.exitType = json["exit_type"].toString();
        frame.exitPressureOver = toDouble(json["exit_pressure_over"]);
        frame.exitPressureUnder = toDouble(json["exit_pressure_under"]);
        frame.exitFlowOver = toDouble(json["exit_flow_over"]);
        frame.exitFlowUnder = toDouble(json["exit_flow_under"]);
    }

    // Handle weight exit (independent of other exit conditions)
    double weightExit = toDouble(json["weight"], 0.0);
    if (weightExit <= 0) {
        weightExit = toDouble(json["exit_weight"], 0.0);
    }
    if (weightExit > 0) {
        frame.exitWeight = weightExit;
    }

    // Handle limiter - support both nested "limiter" object and flat fields
    QJsonObject limiterObj = json["limiter"].toObject();
    if (!limiterObj.isEmpty()) {
        frame.maxFlowOrPressure = toDouble(limiterObj["value"]);
        frame.maxFlowOrPressureRange = toDouble(limiterObj["range"], 0.6);
    } else {
        // Flat format fields
        frame.maxFlowOrPressure = toDouble(json["max_flow_or_pressure"]);
        frame.maxFlowOrPressureRange = toDouble(json["max_flow_or_pressure_range"], 0.6);
    }

    return frame;
}

void VisualizerImporter::fetchProfileDetailsForShots() {
    m_pendingProfileFetches = m_pendingShots.size();

    for (int i = 0; i < m_pendingShots.size(); i++) {
        QVariantMap shot = m_pendingShots[i].toMap();
        QString shotId = shot["id"].toString();
        QString url = QString(VISUALIZER_PROFILE_API).arg(shotId);

        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QNetworkReply* reply = m_networkManager->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, i]() {
            onProfileDetailsFetched(reply, i);
        });
    }
}

void VisualizerImporter::onProfileDetailsFetched(QNetworkReply* reply, int shotIndex) {
    reply->deleteLater();
    m_pendingProfileFetches--;

    if (shotIndex >= 0 && shotIndex < m_pendingShots.size()) {
        QVariantMap shot = m_pendingShots[shotIndex].toMap();

        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);

            if (doc.isObject()) {
                Profile profile = parseVisualizerProfile(doc.object());

                // Check if profile has no frames (invalid)
                if (profile.steps().isEmpty()) {
                    shot["invalid"] = true;
                    shot["invalidReason"] = "Profile has no frames";
                    m_pendingShots[shotIndex] = shot;
                    qDebug() << "Profile" << shot["profile_title"].toString() << "has no frames - marked invalid";
                } else if (profile.isValid() && shot["exists"].toBool()) {
                    // Compare with local profile
                    QVariantMap status = checkProfileStatus(shot["profile_title"].toString(), &profile);
                    shot["identical"] = status["identical"];
                    m_pendingShots[shotIndex] = shot;

                    qDebug() << "Profile" << shot["profile_title"].toString()
                             << "- exists:" << shot["exists"].toBool()
                             << "identical:" << shot["identical"].toBool();
                } else if (!profile.isValid()) {
                    shot["invalid"] = true;
                    shot["invalidReason"] = profile.validationErrors().join(", ");
                    m_pendingShots[shotIndex] = shot;
                    qDebug() << "Profile" << shot["profile_title"].toString() << "invalid:" << shot["invalidReason"].toString();
                }
            }
        } else {
            qDebug() << "Failed to fetch profile details for shot" << shotIndex << ":" << reply->errorString();
            shot["invalid"] = true;
            shot["invalidReason"] = "Failed to fetch profile";
            m_pendingShots[shotIndex] = shot;
        }
    }

    // All profile details fetched
    if (m_pendingProfileFetches <= 0) {
        m_fetching = false;
        emit fetchingChanged();
        m_sharedShots = m_pendingShots;
        emit sharedShotsChanged();
        qDebug() << "All profile details fetched, ready for selection";
    }
}

int VisualizerImporter::saveImportedProfile(const Profile& profile) {
    QString filename = m_controller->titleToFilename(profile.title());

    // Always save downloaded profiles to the dedicated downloaded folder
    // (not ProfileStorage) so they're properly tagged as Downloaded source
    QString localPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    localPath += "/profiles/downloaded";
    QDir dir(localPath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QString fullPath = localPath + "/" + filename + ".json";

    // Check for duplicates
    if (QFile::exists(fullPath)) {
        m_pendingProfile = profile;
        m_pendingPath = filename;
        qDebug() << "Duplicate profile found, waiting for user decision. Filename:" << filename;
        emit duplicateFound(profile.title(), filename);
        return 0;
    }

    if (profile.saveToFile(fullPath)) {
        qDebug() << "Saved imported profile to downloaded folder:" << fullPath;
        m_controller->refreshProfiles();
        return 1;
    }

    qWarning() << "Failed to save imported profile:" << filename;
    return -1;
}

void VisualizerImporter::saveOverwrite() {
    qDebug() << "saveOverwrite called, pendingFilename:" << m_pendingPath;
    if (m_pendingPath.isEmpty()) {
        qWarning() << "saveOverwrite: pendingFilename is empty, cannot save!";
        return;
    }

    // Always save to downloaded folder (not ProfileStorage)
    QString localPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    localPath += "/profiles/downloaded/" + m_pendingPath + ".json";

    if (m_pendingProfile.saveToFile(localPath)) {
        qDebug() << "saveOverwrite: Successfully saved to downloaded folder:" << localPath;
        m_controller->refreshProfiles();
        emit importSuccess(m_pendingProfile.title());
    } else {
        qWarning() << "saveOverwrite: Failed to save:" << m_pendingPath;
        emit importFailed("Failed to overwrite profile");
    }

    m_pendingPath.clear();
}

void VisualizerImporter::saveAsNew() {
    qDebug() << "saveAsNew called, pendingFilename:" << m_pendingPath;
    if (m_pendingPath.isEmpty()) {
        qWarning() << "saveAsNew: pendingFilename is empty, cannot save!";
        return;
    }

    QString baseFilename = m_pendingPath;
    QString downloadedPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/profiles/downloaded";

    // Find unique filename
    int counter = 1;
    QString newFilename;
    do {
        newFilename = baseFilename + "_" + QString::number(counter++);
    } while (QFile::exists(downloadedPath + "/" + newFilename + ".json"));

    // Always save to downloaded folder
    QString fullPath = downloadedPath + "/" + newFilename + ".json";
    if (m_pendingProfile.saveToFile(fullPath)) {
        qDebug() << "saveAsNew: Successfully saved to downloaded folder:" << fullPath;
        m_controller->refreshProfiles();
        emit importSuccess(m_pendingProfile.title());
    } else {
        emit importFailed("Failed to save profile");
    }

    m_pendingPath.clear();
}

void VisualizerImporter::saveWithNewName(const QString& newTitle) {
    qDebug() << "saveWithNewName called, newTitle:" << newTitle << "pendingFilename:" << m_pendingPath;
    if (m_pendingPath.isEmpty()) {
        qWarning() << "saveWithNewName: pendingFilename is empty, cannot save!";
        return;
    }

    if (newTitle.isEmpty()) {
        emit importFailed("Profile name cannot be empty");
        m_pendingPath.clear();
        return;
    }

    m_pendingProfile.setTitle(newTitle);

    QString filename = m_controller->titleToFilename(newTitle);
    QString downloadedPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/profiles/downloaded";
    QDir dir(downloadedPath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    // Check for duplicates in downloaded folder only
    auto checkExists = [&](const QString& name) {
        return QFile::exists(downloadedPath + "/" + name + ".json");
    };

    if (checkExists(filename)) {
        int counter = 1;
        QString newFilename;
        do {
            newFilename = filename + "_" + QString::number(counter++);
        } while (checkExists(newFilename));
        filename = newFilename;
    }

    // Always save to downloaded folder
    QString fullPath = downloadedPath + "/" + filename + ".json";
    if (m_pendingProfile.saveToFile(fullPath)) {
        qDebug() << "saveWithNewName: Successfully saved to downloaded folder:" << fullPath;
        m_controller->refreshProfiles();
        emit importSuccess(m_pendingProfile.title());
    } else {
        qWarning() << "saveWithNewName: Failed to save:" << filename;
        emit importFailed("Failed to save profile");
    }

    m_pendingPath.clear();
}
