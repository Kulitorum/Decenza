#include "visualizerimporter.h"
#include "../controllers/maincontroller.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>

VisualizerImporter::VisualizerImporter(MainController* controller, QObject* parent)
    : QObject(parent)
    , m_controller(controller)
    , m_networkManager(new QNetworkAccessManager(this))
{
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, &VisualizerImporter::onFetchFinished);
}

QString VisualizerImporter::extractShotId(const QString& url) const {
    // Match Visualizer shot URLs:
    // https://visualizer.coffee/shots/UUID
    // https://visualizer.coffee/shots/UUID/anything
    // Also handle API URLs:
    // https://visualizer.coffee/api/shots/UUID/profile.json

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
        return;  // Already importing
    }

    m_importing = true;
    m_fetchingFromShareCode = false;
    emit importingChanged();

    QString url = QString(VISUALIZER_PROFILE_API).arg(shotId);
    qDebug() << "Fetching Visualizer profile from:" << url;

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    m_networkManager->get(request);
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
        return;  // Already importing
    }

    m_importing = true;
    m_fetchingFromShareCode = true;
    emit importingChanged();

    QString url = QString(VISUALIZER_SHARED_API).arg(code);
    qDebug() << "Fetching Visualizer shot from share code:" << url;

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    m_networkManager->get(request);
}

void VisualizerImporter::onFetchFinished(QNetworkReply* reply) {
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        m_importing = false;
        m_fetchingFromShareCode = false;
        emit importingChanged();
        m_lastError = QString("Network error: %1").arg(reply->errorString());
        qWarning() << "Visualizer import failed:" << m_lastError;
        emit lastErrorChanged();
        emit importFailed(m_lastError);
        return;
    }

    QByteArray data = reply->readAll();
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        m_importing = false;
        m_fetchingFromShareCode = false;
        emit importingChanged();
        m_lastError = QString("JSON parse error: %1").arg(parseError.errorString());
        qWarning() << "Visualizer import failed:" << m_lastError;
        emit lastErrorChanged();
        emit importFailed(m_lastError);
        return;
    }

    QJsonObject json = doc.object();

    // Check for API error response
    if (json.contains("error")) {
        m_importing = false;
        m_fetchingFromShareCode = false;
        emit importingChanged();
        m_lastError = json["error"].toString("Unknown error");
        qWarning() << "Visualizer API error:" << m_lastError;
        emit lastErrorChanged();
        emit importFailed(m_lastError);
        return;
    }

    // If we were fetching from share code, we got shot data - now fetch the profile
    if (m_fetchingFromShareCode) {
        QString shotId = json["id"].toString();
        if (shotId.isEmpty()) {
            m_importing = false;
            m_fetchingFromShareCode = false;
            emit importingChanged();
            m_lastError = "Share code response missing shot ID";
            emit lastErrorChanged();
            emit importFailed(m_lastError);
            return;
        }

        qDebug() << "Got shot ID from share code:" << shotId << "- fetching profile...";
        m_fetchingFromShareCode = false;  // Next response will be the profile

        // Fetch the actual profile
        QString url = QString(VISUALIZER_PROFILE_API).arg(shotId);
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        m_networkManager->get(request);
        return;  // Don't finish yet - wait for profile response
    }

    // We have the profile data - parse and save
    m_importing = false;
    emit importingChanged();

    Profile profile = parseVisualizerProfile(json);

    if (!profile.isValid()) {
        m_lastError = "Invalid profile: " + profile.validationErrors().join(", ");
        qWarning() << "Visualizer import failed:" << m_lastError;
        emit lastErrorChanged();
        emit importFailed(m_lastError);
        return;
    }

    // saveImportedProfile returns:
    // 1 = saved successfully
    // 0 = waiting for user decision (duplicate found)
    // -1 = failed to save
    int result = saveImportedProfile(profile);
    if (result == 1) {
        qDebug() << "Successfully imported profile:" << profile.title();
        emit importSuccess(profile.title());
    } else if (result == -1) {
        m_lastError = "Failed to save profile";
        emit lastErrorChanged();
        emit importFailed(m_lastError);
    }
    // result == 0 means duplicate found, waiting for user decision (don't emit anything yet)
}

Profile VisualizerImporter::parseVisualizerProfile(const QJsonObject& json) {
    Profile profile;

    // Metadata - Visualizer uses same field names but values might be strings
    profile.setTitle(json["title"].toString("Imported Profile"));
    profile.setAuthor(json["author"].toString());
    profile.setNotes(json["notes"].toString());
    profile.setBeverageType(json["beverage_type"].toString("espresso"));

    // Profile type - Visualizer uses "legacy_profile_type"
    QString profileType = json["legacy_profile_type"].toString();
    if (profileType.isEmpty()) {
        profileType = json["profile_type"].toString("settings_2c");
    }
    profile.setProfileType(profileType);

    // Target values - Visualizer stores as strings
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

    // Parse steps
    QJsonArray stepsArray = json["steps"].toArray();
    for (const auto& stepVal : stepsArray) {
        ProfileFrame frame = parseVisualizerStep(stepVal.toObject());
        profile.addStep(frame);
    }

    // Set espresso temperature from first step if not set
    if (!profile.steps().isEmpty()) {
        profile.setEspressoTemperature(profile.steps().first().temperature);
    }

    // Calculate preinfuse frame count (frames with exit conditions at start)
    int preinfuseCount = 0;
    for (const auto& step : profile.steps()) {
        if (step.exitIf) {
            preinfuseCount++;
        } else {
            break;  // Stop counting after first non-exit frame
        }
    }
    profile.setPreinfuseFrameCount(preinfuseCount);

    qDebug() << "Parsed Visualizer profile:" << profile.title()
             << "with" << profile.steps().size() << "steps";

    return profile;
}

ProfileFrame VisualizerImporter::parseVisualizerStep(const QJsonObject& json) {
    ProfileFrame frame;

    // Helper to get value as double (handles string or number)
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

    // Exit conditions - Visualizer uses nested object: {type, value, condition}
    QJsonObject exitObj = json["exit"].toObject();
    if (!exitObj.isEmpty()) {
        frame.exitIf = true;
        QString exitType = exitObj["type"].toString();      // "pressure" or "flow"
        QString condition = exitObj["condition"].toString(); // "over" or "under"
        double value = toDouble(exitObj["value"]);

        // Convert to our format: "pressure_over", "pressure_under", "flow_over", "flow_under"
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
    }

    // Limiter - Visualizer uses nested object: {value, range}
    QJsonObject limiterObj = json["limiter"].toObject();
    if (!limiterObj.isEmpty()) {
        frame.maxFlowOrPressure = toDouble(limiterObj["value"]);
        frame.maxFlowOrPressureRange = toDouble(limiterObj["range"], 0.6);
    }

    return frame;
}

int VisualizerImporter::saveImportedProfile(const Profile& profile) {
    // Returns: 1 = saved, 0 = waiting for user (duplicate), -1 = failed

    // Get downloaded profiles directory path
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    path += "/profiles/downloaded";

    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    // Generate filename from title
    QString filename = m_controller->titleToFilename(profile.title());
    QString fullPath = path + "/" + filename + ".json";

    // Check if file already exists
    if (QFile::exists(fullPath)) {
        // Store pending profile and emit signal for user choice
        m_pendingProfile = profile;
        m_pendingPath = fullPath;
        qDebug() << "Duplicate profile found, waiting for user decision. Pending path:" << m_pendingPath;
        emit duplicateFound(profile.title(), fullPath);
        return 0;  // Waiting for user decision
    }

    if (profile.saveToFile(fullPath)) {
        m_controller->refreshProfiles();
        return 1;  // Success
    }

    qWarning() << "Failed to save imported profile to:" << fullPath;
    return -1;  // Failed
}

void VisualizerImporter::saveOverwrite() {
    qDebug() << "saveOverwrite called, pendingPath:" << m_pendingPath;
    if (m_pendingPath.isEmpty()) {
        qWarning() << "saveOverwrite: pendingPath is empty, cannot save!";
        return;
    }

    if (m_pendingProfile.saveToFile(m_pendingPath)) {
        m_controller->refreshProfiles();
        qDebug() << "saveOverwrite: Successfully saved to" << m_pendingPath;
        emit importSuccess(m_pendingProfile.title());
    } else {
        qWarning() << "saveOverwrite: Failed to save to" << m_pendingPath;
        emit importFailed("Failed to overwrite profile");
    }

    m_pendingPath.clear();
}

void VisualizerImporter::saveAsNew() {
    qDebug() << "saveAsNew called, pendingPath:" << m_pendingPath;
    if (m_pendingPath.isEmpty()) {
        qWarning() << "saveAsNew: pendingPath is empty, cannot save!";
        return;
    }

    // Find a unique filename by appending _1, _2, etc.
    QString basePath = m_pendingPath;
    basePath.chop(5);  // Remove ".json"

    int counter = 1;
    QString newPath;
    do {
        newPath = basePath + "_" + QString::number(counter++) + ".json";
    } while (QFile::exists(newPath));

    if (m_pendingProfile.saveToFile(newPath)) {
        m_controller->refreshProfiles();
        emit importSuccess(m_pendingProfile.title());
    } else {
        emit importFailed("Failed to save profile");
    }

    m_pendingPath.clear();
}

void VisualizerImporter::saveWithNewName(const QString& newTitle) {
    qDebug() << "saveWithNewName called, newTitle:" << newTitle << "pendingPath:" << m_pendingPath;
    if (m_pendingPath.isEmpty()) {
        qWarning() << "saveWithNewName: pendingPath is empty, cannot save!";
        return;
    }

    if (newTitle.isEmpty()) {
        emit importFailed("Profile name cannot be empty");
        m_pendingPath.clear();
        return;
    }

    // Update the profile title
    m_pendingProfile.setTitle(newTitle);

    // Generate new filename from the new title (save to downloaded folder)
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    path += "/profiles/downloaded";

    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QString filename = m_controller->titleToFilename(newTitle);
    QString newPath = path + "/" + filename + ".json";

    // Check if this name also exists
    if (QFile::exists(newPath)) {
        // Find unique name
        int counter = 1;
        QString basePath = path + "/" + filename;
        do {
            newPath = basePath + "_" + QString::number(counter++) + ".json";
        } while (QFile::exists(newPath));
    }

    if (m_pendingProfile.saveToFile(newPath)) {
        m_controller->refreshProfiles();
        qDebug() << "saveWithNewName: Successfully saved to" << newPath;
        emit importSuccess(m_pendingProfile.title());
    } else {
        qWarning() << "saveWithNewName: Failed to save to" << newPath;
        emit importFailed("Failed to save profile");
    }

    m_pendingPath.clear();
}
