#include "visualizerimporter.h"
#include "../controllers/maincontroller.h"
#include "../core/settings.h"
#include "../core/profilestorage.h"
#include "../profile/profilesavehelper.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>

// Sanitize JSON to fix malformed numbers from Visualizer API
// Fixes: .5 -> 0.5, 9. -> 9.0
static QByteArray sanitizeVisualizerJson(const QByteArray& data)
{
    QString jsonStr = QString::fromUtf8(data);

    // Fix numbers starting with decimal point (e.g., .5 -> 0.5)
    jsonStr.replace(QRegularExpression(R"(([:,\[]\s*)\.(\d))"), "\\10.\\2");

    // Fix numbers ending with decimal point (e.g., 9. -> 9.0)
    jsonStr.replace(QRegularExpression(R"((\d)\.([,\]\s}]))"), "\\1.0\\2");

    return jsonStr.toUtf8();
}

VisualizerImporter::VisualizerImporter(QNetworkAccessManager* networkManager, MainController* controller, Settings* settings, QObject* parent)
    : QObject(parent)
    , m_controller(controller)
    , m_settings(settings)
    , m_networkManager(networkManager)
    , m_saveHelper(new ProfileSaveHelper(controller, this))
{
    Q_ASSERT(networkManager);

    // Forward helper signals to our own signals
    connect(m_saveHelper, &ProfileSaveHelper::importSuccess, this, &VisualizerImporter::importSuccess);
    connect(m_saveHelper, &ProfileSaveHelper::importFailed, this, &VisualizerImporter::importFailed);
    connect(m_saveHelper, &ProfileSaveHelper::duplicateFound, this, &VisualizerImporter::duplicateFound);
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

    // Check if response is TCL format instead of JSON
    // TCL profiles start with "profile_" while JSON starts with "{" or "["
    QString dataStr = QString::fromUtf8(data).trimmed();
    bool isTclFormat = dataStr.startsWith("profile_") || dataStr.startsWith("advanced_shot");

    if (isTclFormat) {
        // Handle TCL format response (some Visualizer profiles return TCL instead of JSON)
        qDebug() << "Detected TCL format profile from Visualizer";

        Profile profile = Profile::loadFromTclString(dataStr);

        if (!profile.isValid() || profile.steps().isEmpty()) {
            qWarning() << "TCL profile has no steps - shot was uploaded without complete profile data";
            m_importing = false;
            m_requestType = RequestType::None;
            emit importingChanged();
            QString title = profile.title();
            if (title.isEmpty()) title = "This profile";
            m_lastError = QString("%1 is not available - the shot was uploaded without complete profile data. Try the built-in profiles or import from a different source.").arg(title);
            emit lastErrorChanged();
            emit importFailed(m_lastError);
            return;
        }

        m_importing = false;
        m_requestType = RequestType::None;
        emit importingChanged();

        // Save the TCL profile
        QString filename = m_saveHelper->titleToFilename(profile.title());
        int result = m_saveHelper->saveProfile(profile, filename);
        if (result == 1) {
            qDebug() << "Successfully imported TCL profile:" << profile.title();
            emit importSuccess(profile.title());
        } else if (result == -1) {
            m_lastError = "Failed to save profile";
            emit lastErrorChanged();
            emit importFailed(m_lastError);
        }
        // result == 0 means duplicate dialog shown, waiting for user
        return;
    }

    data = sanitizeVisualizerJson(data);

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        m_importing = false;
        m_fetching = false;
        m_requestType = RequestType::None;
        emit importingChanged();
        emit fetchingChanged();
        m_lastError = QString("JSON parse error: %1 (at position %2)")
            .arg(parseError.errorString())
            .arg(parseError.offset);
        qWarning() << "Visualizer JSON parse failed:" << m_lastError;
        qWarning() << "JSON snippet around error:" << data.mid(qMax(0, parseError.offset - 50), 100);
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
            QVariantMap status = m_saveHelper->checkProfileStatus(shot["profile_title"].toString());
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

        // Use profile_url from shot metadata if available, otherwise construct from shot ID
        // Always request JSON format explicitly to get complete profile data
        QString url = json["profile_url"].toString();
        if (url.isEmpty()) {
            url = QString("https://visualizer.coffee/api/shots/%1/profile").arg(shotId);
        }
        // Add format=json to get structured JSON instead of TCL
        if (!url.contains("?")) {
            url += "?format=json";
        } else {
            url += "&format=json";
        }
        qDebug() << "Fetching profile from:" << url;
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

        QString downloadedPath = ProfileSaveHelper::downloadedProfilesPath();
        QString filename = m_saveHelper->titleToFilename(customName);
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

    QString filename = m_saveHelper->titleToFilename(profile.title());
    int result = m_saveHelper->saveProfile(profile, filename);
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

    QByteArray rawData = reply->readAll();
    QString dataStr = QString::fromUtf8(rawData).trimmed();
    bool isTclFormat = dataStr.startsWith("profile_") || dataStr.startsWith("advanced_shot");

    Profile profile;
    if (isTclFormat) {
        profile = Profile::loadFromTclString(dataStr);
    } else {
        QByteArray data = sanitizeVisualizerJson(rawData);
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isObject()) {
            profile = parseVisualizerProfile(doc.object());
        }
    }

    if (!profile.isValid() || profile.steps().isEmpty()) {
        m_batchSkipped++;
    } else {
        QString filename = m_saveHelper->titleToFilename(profile.title());
        ProfileStorage* storage = m_controller->profileStorage();

        bool exists = false;
        if (storage && storage->isConfigured()) {
            exists = storage->profileExists(filename);
        }
        if (!exists) {
            QString localPath = ProfileSaveHelper::downloadedProfilesPath() + "/" + filename + ".json";
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
                QString localPath = ProfileSaveHelper::downloadedProfilesPath();
                saved = profile.saveToFile(localPath + "/" + filename + ".json");
            }

            if (saved) {
                qDebug() << "Imported profile:" << profile.title();
                m_batchImported++;
            } else {
                m_batchSkipped++;
            }
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
    // Use the unified Profile::fromJson() which handles both de1app v2 and legacy formats,
    // including string-encoded numbers, nested exit/limiter objects, recipe mode, etc.
    Profile profile = Profile::fromJson(QJsonDocument(json));

    // Override default title for imports (fromJson defaults to "Default")
    if (profile.title() == "Default" || profile.title().isEmpty()) {
        profile.setTitle(json["title"].toString("Imported Profile"));
    }

    // Safety net: if profile is recipe mode with no steps, generate frames from recipe params
    if (profile.steps().isEmpty() && profile.isRecipeMode()) {
        profile.regenerateFromRecipe();
    }

    qDebug() << "Parsed Visualizer profile:" << profile.title()
             << "with" << profile.steps().size() << "steps";

    return profile;
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
            QByteArray rawData = reply->readAll();
            QString dataStr = QString::fromUtf8(rawData).trimmed();
            bool isTclFormat = dataStr.startsWith("profile_") || dataStr.startsWith("advanced_shot");

            Profile profile;
            if (isTclFormat) {
                profile = Profile::loadFromTclString(dataStr);
            } else {
                QByteArray data = sanitizeVisualizerJson(rawData);
                QJsonDocument doc = QJsonDocument::fromJson(data);
                if (doc.isObject()) {
                    profile = parseVisualizerProfile(doc.object());
                }
            }

            if (profile.isValid()) {
                // Check if profile has no frames (invalid)
                if (profile.steps().isEmpty()) {
                    shot["invalid"] = true;
                    shot["invalidReason"] = "Profile has no frames";
                    m_pendingShots[shotIndex] = shot;
                    qDebug() << "Profile" << shot["profile_title"].toString() << "has no frames - marked invalid";
                } else if (profile.isValid() && shot["exists"].toBool()) {
                    // Compare with local profile
                    QVariantMap status = m_saveHelper->checkProfileStatus(shot["profile_title"].toString(), &profile);
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

void VisualizerImporter::saveOverwrite() {
    m_saveHelper->saveOverwrite();
}

void VisualizerImporter::saveAsNew() {
    m_saveHelper->saveAsNew();
}

void VisualizerImporter::saveWithNewName(const QString& newTitle) {
    m_saveHelper->saveWithNewName(newTitle);
}
