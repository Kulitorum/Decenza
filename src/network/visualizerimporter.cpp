#include "core/diagnosticlogging.h"
#include "visualizerimporter.h"
#include "../controllers/maincontroller.h"
#include "../core/settings.h"
#include "../core/profilestorage.h"
#include "../history/shothistorystorage.h"
#include "../history/shotfileparser.h"
#include "../core/translationmanager.h"
#include "visualizershotlist.h"
#include "../profile/profilesavehelper.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QDir>
#include <QUrl>
#include <QDebug>
#include <QScopedValueRollback>

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
    connect(m_saveHelper, &ProfileSaveHelper::importSuccess, this, [this](const QString& title) {
        if (m_resolutionLog) m_resolutionLog->finish("success", "profileSaved");
        emit importSuccess(title);
    });
    connect(m_saveHelper, &ProfileSaveHelper::importFailed, this, [this](const QString& error) {
        if (m_resolutionLog) m_resolutionLog->problem("save", "saveFailed");
        emit importFailed(error);
    });
    connect(m_saveHelper, &ProfileSaveHelper::duplicateFound, this, &VisualizerImporter::duplicateFound);
}

QString VisualizerImporter::tr_(const char* key, const char* fallback) const {
    return translateOrFallback(m_translationManager, key, fallback);
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
    const auto op = Log::begin(Log::Emitter::Importer, "profileImport", 0, 0, shotId);
    if (shotId.isEmpty()) {
        m_lastError = tr_("visualizer.error.noShotId", "No shot ID provided");
        emit lastErrorChanged();
        op->finish("rejected", "missingInput");
        emit importFailed(m_lastError);
        return;
    }

    if (m_importing) {
        op->finish("rejected", "busy");
        return;
    }

    m_importing = true;
    m_requestType = RequestType::None;
    emit importingChanged();

    QString url = QString(VISUALIZER_PROFILE_API).arg(shotId);
    op->detail("fetch", "dispatch");

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, op]() {
        onFetchFinished(reply, op);
    });
}

void VisualizerImporter::importFromShotIdWithName(const QString& shotId, const QString& customName) {
    const auto op = Log::begin(Log::Emitter::Importer, "profileImport", 0, 0, shotId);
    if (shotId.isEmpty() || customName.isEmpty()) {
        m_lastError = tr_("visualizer.error.shotIdAndName", "Shot ID and name are required");
        emit lastErrorChanged();
        op->finish("rejected", "missingInput");
        emit importFailed(m_lastError);
        return;
    }

    if (m_importing) {
        op->finish("rejected", "busy");
        return;
    }

    m_importing = true;
    m_requestType = RequestType::RenamedImport;
    m_customImportName = customName;
    emit importingChanged();

    QString url = QString(VISUALIZER_PROFILE_API).arg(shotId);
    op->detail("fetch", "dispatch");

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, op]() {
        onFetchFinished(reply, op);
    });
}

void VisualizerImporter::importFromShareCode(const QString& shareCode) {
    const auto op = Log::begin(Log::Emitter::Importer, "profileImport", 0, 0, QString());
    QString code = shareCode.trimmed();

    if (code.isEmpty()) {
        m_lastError = tr_("visualizer.error.noShareCode", "No share code provided");
        emit lastErrorChanged();
        op->finish("rejected", "missingInput");
        emit importFailed(m_lastError);
        return;
    }

    if (m_importing) {
        op->finish("rejected", "busy");
        return;
    }

    m_importing = true;
    m_requestType = RequestType::ShareCode;
    emit importingChanged();

    QString url = QString(VISUALIZER_SHARED_API).arg(code);
    op->detail("fetch", "dispatch");

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QString auth = authHeader();
    if (!auth.isEmpty()) {
        request.setRawHeader("Authorization", auth.toUtf8());
    }

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, op]() {
        onFetchFinished(reply, op);
    });
}

void VisualizerImporter::fetchSharedShots() {
    const auto op = Log::begin(Log::Emitter::Importer, "sharedShots", 0, 0, QString());
    if (m_fetching) {
        op->finish("rejected", "busy");
        return;
    }

    QString auth = authHeader();
    if (auth.isEmpty()) {
        m_lastError = tr_("visualizer.error.credentialsMissing", "Visualizer credentials not configured");
        emit lastErrorChanged();
        op->finish("rejected", "missingCredentials");
        emit importFailed(m_lastError);
        return;
    }

    m_fetching = true;
    m_requestType = RequestType::FetchList;
    emit fetchingChanged();

    QString url = "https://visualizer.coffee/api/shots/shared?code=";
    op->detail("fetch", "dispatch");

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", auth.toUtf8());

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, op]() {
        onFetchFinished(reply, op);
    });
}

void VisualizerImporter::importSelectedShots(const QStringList& shotIds, bool overwriteExisting) {
    const auto op = Log::begin(Log::Emitter::Importer, "batchImport", 0, 0, QString());
    if (shotIds.isEmpty()) {
        op->finishBatch(0, 0, 0, 0);
        emit batchImportComplete(0, 0, 0);
        return;
    }

    if (m_importing) {
        op->finish("rejected", "busy");
        return;
    }

    m_importing = true;
    m_requestType = RequestType::BatchImport;
    m_batchShotIds = shotIds;
    m_batchOverwrite = overwriteExisting;
    m_batchImported = 0;
    m_batchSkipped = 0;
    m_batchFailed = 0;
    emit importingChanged();

    op->detail("fetch", "dispatch");

    // Start fetching first profile
    QString shotId = m_batchShotIds.takeFirst();
    QString url = QString(VISUALIZER_PROFILE_API).arg(shotId);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, shotId, op]() {
        onProfileFetchFinished(reply, shotId, op);
    });
}

void VisualizerImporter::onFetchFinished(QNetworkReply* reply, const Op& op) {
    op->response("fetch", reply);
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        m_importing = false;
        m_fetching = false;
        m_requestType = RequestType::None;
        emit importingChanged();
        emit fetchingChanged();

        if (statusCode == 401) {
            m_lastError = tr_("visualizer.error.credentialsInvalid", "Invalid Visualizer credentials");
        } else {
            m_lastError = tr_("visualizer.error.network", "Network error: %1").arg(reply->errorString());
        }

        emit lastErrorChanged();
        op->finish("failed", "requestFailed");
        emit importFailed(m_lastError);
        return;
    }

    QByteArray data = reply->readAll();
    op->detail("interpret", "responseReceived");

    // Check if response is TCL format instead of JSON
    // TCL profiles start with "profile_" while JSON starts with "{" or "["
    QString dataStr = QString::fromUtf8(data).trimmed();
    bool isTclFormat = dataStr.startsWith("profile_") || dataStr.startsWith("advanced_shot");

    if (isTclFormat) {
        // Handle TCL format response (some Visualizer profiles return TCL instead of JSON)

        Profile profile = Profile::loadFromTclString(dataStr);

        if (!profile.isValid() || profile.steps().isEmpty()) {

            m_importing = false;
            m_requestType = RequestType::None;
            emit importingChanged();
            QString title = profile.title();
            if (title.isEmpty()) title = tr_("visualizer.error.thisProfile", "This profile");
            m_lastError = tr_("visualizer.error.profileUnavailable",
                              "%1 is not available - the shot was uploaded without complete profile data. Try the built-in profiles or import from a different source.").arg(title);
            emit lastErrorChanged();
            op->finish("failed", "invalidProfile");
            emit importFailed(m_lastError);
            return;
        }

        m_importing = false;
        m_requestType = RequestType::None;
        emit importingChanged();

        // Save the TCL profile
        QString filename = m_saveHelper->titleToFilename(profile.title());
        ProfileSaveHelper::SaveResult result = saveImportedProfile(profile, filename, op);
        if (result == ProfileSaveHelper::SaveResult::Saved) {
            emit importSuccess(profile.title());
        } else if (result == ProfileSaveHelper::SaveResult::Failed) {
            m_lastError = tr_("visualizer.error.saveFailed", "Failed to save profile");
            emit lastErrorChanged();
            op->finish("failed", "saveFailed");
            emit importFailed(m_lastError);
        }
        // PendingResolution means duplicate dialog shown, waiting for user
        return;
    }

    op->detail("interpret", "parseResponse");
    data = sanitizeVisualizerJson(data);

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        m_importing = false;
        m_fetching = false;
        m_requestType = RequestType::None;
        emit importingChanged();
        emit fetchingChanged();
        m_lastError = tr_("visualizer.error.jsonParse", "JSON parse error: %1 (at position %2)")
            .arg(parseError.errorString())
            .arg(parseError.offset);

        emit lastErrorChanged();
        op->finish("failed", "invalidResponse");
        emit importFailed(m_lastError);
        return;
    }

    // Handle FetchList request - store shots and fetch profile details
    if (m_requestType == RequestType::FetchList) {
        if (!doc.isArray()) {
            m_fetching = false;
            emit fetchingChanged();
            m_lastError = tr_("visualizer.error.expectedArray", "Expected array of shared shots");
            emit lastErrorChanged();
            op->finish("failed", "expectedArray");
            emit importFailed(m_lastError);
            return;
        }

        QJsonArray array = doc.array();

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
            fetchProfileDetailsForShots(op);
        } else {
            m_fetching = false;
            emit fetchingChanged();
            m_sharedShots = m_pendingShots;
            op->finishBatch(0, 0, 0, 0);
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
            m_lastError = tr_("visualizer.error.noSharedShots", "No shared shots found");
            emit lastErrorChanged();
            op->finish("failed", "noSharedShots");
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
        m_lastError = json["error"].toString(tr_("visualizer.error.unknownServer", "Unknown error"));

        emit lastErrorChanged();
        op->finish("failed", "serverRejected");
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
            m_lastError = tr_("visualizer.error.shareMissingId", "Share code response missing shot ID");
            emit lastErrorChanged();
            op->finish("failed", "missingReturnedShotId");
            emit importFailed(m_lastError);
            return;
        }

        op->set("remoteId", shotId);
        op->detail("profileFetch", "dispatch");
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

        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QNetworkReply* profileReply = m_networkManager->get(request);
        connect(profileReply, &QNetworkReply::finished, this, [this, profileReply, op]() {
            onFetchFinished(profileReply, op);
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
        m_lastError = tr_("visualizer.error.invalidProfile", "Invalid profile: %1")
                          .arg(profile.validationErrors().join(", "));

        emit lastErrorChanged();
        op->finish("failed", "invalidProfile");
        emit importFailed(m_lastError);
        return;
    }

    // For renamed imports, save with the custom name (with duplicate detection via helper)
    if (isRenamedImport && !customName.isEmpty()) {
        profile.setTitle(customName);

        QString filename = m_saveHelper->titleToFilename(customName);
        ProfileSaveHelper::SaveResult result = saveImportedProfile(profile, filename, op);
        if (result == ProfileSaveHelper::SaveResult::Saved) {
            emit importSuccess(customName);
            fetchSharedShots();
        } else if (result == ProfileSaveHelper::SaveResult::Failed) {
            m_lastError = tr_("visualizer.error.saveFailed", "Failed to save profile");
            emit lastErrorChanged();
            op->finish("failed", "saveFailed");
            emit importFailed(m_lastError);
        }
        // PendingResolution: duplicate dialog shown via helper signal, waiting for user
        return;
    }

    QString filename = m_saveHelper->titleToFilename(profile.title());
    ProfileSaveHelper::SaveResult result = saveImportedProfile(profile, filename, op);
    if (result == ProfileSaveHelper::SaveResult::Saved) {
        emit importSuccess(profile.title());
        // Refresh shared shots list to update status
        fetchSharedShots();
    } else if (result == ProfileSaveHelper::SaveResult::Failed) {
        m_lastError = tr_("visualizer.error.saveFailed", "Failed to save profile");
        emit lastErrorChanged();
        op->finish("failed", "saveFailed");
        emit importFailed(m_lastError);
    }
}

void VisualizerImporter::onProfileFetchFinished(QNetworkReply* reply, const QString& remoteId, const Op& op) {
    op->set("remoteId", remoteId);
    op->response("profileFetch", reply);
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        op->problem("profileFetch", "requestFailed");
        m_batchSkipped++;

        // Continue with next profile
        if (!m_batchShotIds.isEmpty()) {
            QString shotId = m_batchShotIds.takeFirst();
            QString url = QString(VISUALIZER_PROFILE_API).arg(shotId);
            QNetworkRequest request(url);
            request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            QNetworkReply* nextReply = m_networkManager->get(request);
            connect(nextReply, &QNetworkReply::finished, this, [this, nextReply, shotId, op]() {
                onProfileFetchFinished(nextReply, shotId, op);
            });
        } else {
            // Done with batch
            m_importing = false;
            m_requestType = RequestType::None;
            op->set("remoteId", QString());
            op->finishBatch(m_batchImported + m_batchSkipped + m_batchFailed, m_batchImported, m_batchSkipped, m_batchFailed);
            emit importingChanged();
            emit batchImportComplete(m_batchImported, m_batchSkipped, m_batchFailed);
            if (m_controller) {
                m_controller->profileManager()->refreshProfiles();
            }
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
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            op->detail("interpret", "invalidResponse");
        } else if (!doc.isObject()) {
            op->detail("interpret", "invalidResponse");
        } else {
            profile = parseVisualizerProfile(doc.object());
        }
    }

    if (!profile.isValid() || profile.steps().isEmpty()) {
        // Every other skip and failure in this loop names the profile; this one
        // used to increment a counter and say nothing, so a batch that quietly
        // dropped items gave the user an aggregate number and no way to find out
        // which or why.
        op->problem("interpret", "invalidProfile");
        m_batchSkipped++;
    } else {
        QString filename = m_saveHelper->titleToFilename(profile.title());
        ProfileStorage* storage = m_controller ? m_controller->profileStorage() : nullptr;

        bool exists = false;
        if (storage && storage->isConfigured()) {
            exists = storage->profileExists(filename);
        }
        if (!exists) {
            QString localPath = ProfileSaveHelper::downloadedProfilesPath() + "/" + filename + ".json";
            exists = QFile::exists(localPath);
        }

        if (exists && !m_batchOverwrite) {
            op->detail("save", "existingProfileSkipped");
            m_batchSkipped++;
        } else {
            // Save the profile
            bool saved = false;
            if (storage && storage->isConfigured()) {
                saved = storage->writeProfile(filename, profile.toJsonString());
            }
            if (!saved) {
                QString localPath = ProfileSaveHelper::downloadedProfilesPath();
                if (!localPath.isEmpty()) {
                    saved = profile.saveToFile(localPath + "/" + filename + ".json");
                }
            }

            if (saved) {
                op->detail("save", "profileSaved");
                m_batchImported++;
            } else {
                op->problem("save", "saveFailed");
                m_batchFailed++;
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
        connect(nextReply, &QNetworkReply::finished, this, [this, nextReply, shotId, op]() {
            onProfileFetchFinished(nextReply, shotId, op);
        });
    } else {
        m_importing = false;
        m_requestType = RequestType::None;
        op->set("remoteId", QString());
        op->finishBatch(m_batchImported + m_batchSkipped + m_batchFailed, m_batchImported, m_batchSkipped, m_batchFailed);
        emit importingChanged();
        emit batchImportComplete(m_batchImported, m_batchSkipped, m_batchFailed);
        if (m_controller) {
            m_controller->profileManager()->refreshProfiles();
        }
    }
}

Profile VisualizerImporter::parseVisualizerProfile(const QJsonObject& json) {
    // Use the unified Profile::fromJson() which handles both de1app v2 and legacy formats,
    // including string-encoded numbers, nested exit/limiter objects, recipe params, etc.
    Profile profile = Profile::fromJson(QJsonDocument(json));

    // Override default title for imports (fromJson defaults to "Default")
    if (profile.title() == "Default" || profile.title().isEmpty()) {
        profile.setTitle(json["title"].toString("Imported Profile"));
    }

    // There is deliberately NO frame-generation safety net here for a payload that
    // arrives with no steps.
    //
    // One used to call regenerateFromRecipe() on the theory that a profile carrying
    // a recipe block but no frames could be rebuilt from the block. Two things
    // retired it. First, fabricating a profile from unestablished parameters is
    // finding REC-1 — it produced a complete default 88 °C / 20 s / 4 g profile from
    // a broken download. Second, a stored block is no longer read into RecipeParams
    // at all, so regenerateFromRecipe() would refuse regardless: it is guarded on
    // hasRecipeParams(), which nothing sets from JSON any more.
    //
    // A payload with no steps is simply broken, and is rejected by the
    // isValid()/steps().isEmpty() checks in both callers of this function.

    return profile;
}

void VisualizerImporter::fetchProfileDetailsForShots(const Op& op) {
    m_pendingProfileFetches = static_cast<int>(m_pendingShots.size());

    for (int i = 0; i < m_pendingShots.size(); i++) {
        QVariantMap shot = m_pendingShots[i].toMap();
        QString shotId = shot["id"].toString();
        QString url = QString(VISUALIZER_PROFILE_API).arg(shotId);

        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QNetworkReply* reply = m_networkManager->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, i, shotId, op]() {
            onProfileDetailsFetched(reply, i, shotId, op);
        });
    }
}

void VisualizerImporter::onProfileDetailsFetched(QNetworkReply* reply, int shotIndex, const QString& remoteId, const Op& op) {
    op->set("itemIndex", shotIndex);
    op->set("remoteId", remoteId);
    op->response("profileDetails", reply);
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
                QJsonParseError parseError;
                QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
                if (parseError.error != QJsonParseError::NoError) {
                    op->detail("interpret", "invalidResponse");
                } else if (!doc.isObject()) {
                    op->detail("interpret", "invalidResponse");
                } else {
                    profile = parseVisualizerProfile(doc.object());
                }
            }

            if (!profile.isValid()) op->problem("interpret", "invalidProfile");
            if (profile.isValid()) {
                // Check if profile has no frames (invalid)
                if (profile.steps().isEmpty()) {
                    shot["invalid"] = true;
                    shot["invalidReason"] = "Profile has no frames";
                    m_pendingShots[shotIndex] = shot;
                    op->problem("interpret", "noFrames");
                } else if (profile.isValid() && shot["exists"].toBool()) {
                    // Compare with local profile
                    QVariantMap status = m_saveHelper->checkProfileStatus(shot["profile_title"].toString(), &profile);
                    shot["identical"] = status["identical"];
                    m_pendingShots[shotIndex] = shot;

                    op->detail("compare", "profileCompared");
                } else if (!profile.isValid()) {
                    shot["invalid"] = true;
                    shot["invalidReason"] = profile.validationErrors().join(", ");
                    m_pendingShots[shotIndex] = shot;
                    op->problem("interpret", "invalidProfile");
                }
            }
        } else {
            op->problem("profileDetails", "requestFailed");
            shot["invalid"] = true;
            shot["invalidReason"] = "Failed to fetch profile";
            m_pendingShots[shotIndex] = shot;
        }
    }

    // All profile details fetched
    if (m_pendingProfileFetches <= 0) {
        op->set("remoteId", QString());
        op->finishBatch(m_pendingShots.size(), m_pendingShots.size() - op->problems, 0, op->problems);
        m_fetching = false;
        emit fetchingChanged();
        m_sharedShots = m_pendingShots;
        emit sharedShotsChanged();

    }
}

ProfileSaveHelper::SaveResult VisualizerImporter::saveImportedProfile(
    const Profile& profile, const QString& filename, const Op& op)
{
    op->detail("save", "saveProfile");
    // Preserve the older pending decision if the helper refuses a second import.
    if (!m_saveHelper->hasPending()) m_pendingImportLog = op;
    const auto result = m_saveHelper->saveProfile(profile, filename);
    if (result == ProfileSaveHelper::SaveResult::Saved) op->finish("success", "profileSaved");
    else if (result == ProfileSaveHelper::SaveResult::Failed) op->finish("failed", "saveFailed");
    else op->detail("duplicateDecision", "awaitingUser");
    if (op->terminal && m_pendingImportLog == op) m_pendingImportLog.reset();
    return result;
}

void VisualizerImporter::resolvePendingImport(const QString& action, const std::function<void()>& resolve)
{
    const auto op = m_pendingImportLog ? m_pendingImportLog : Log::begin(Log::Emitter::Importer, "profileImport");
    op->detail("duplicateDecision", action);
    QScopedValueRollback<Op> current(m_resolutionLog, op);
    const bool hadPending = m_saveHelper->hasPending();
    resolve();
    if (!hadPending) op->finish("rejected", "noPendingImport");
    else if (!m_saveHelper->hasPending() && !op->terminal) op->finish("failed", "saveFailed");
    // Invalid input can leave the helper pending so the user can correct it.
    if (op->terminal && m_pendingImportLog == op) m_pendingImportLog.reset();
}

void VisualizerImporter::saveOverwrite() {
    resolvePendingImport("overwrite", [this]() { m_saveHelper->saveOverwrite(); });
}

void VisualizerImporter::saveAsNew() {
    resolvePendingImport("saveAsNew", [this]() { m_saveHelper->saveAsNew(); });
}

void VisualizerImporter::saveWithNewName(const QString& newTitle) {
    resolvePendingImport("rename", [this, newTitle]() { m_saveHelper->saveWithNewName(newTitle); });
}

void VisualizerImporter::cancelPending() {
    const auto op = m_pendingImportLog;
    m_pendingImportLog.reset();
    m_saveHelper->cancelPending();
    if (op) op->finish("cancelled", "userCancelled");
}

// ---------------------------------------------------------------------------
// Recover shots from Visualizer (date-range history import)
// ---------------------------------------------------------------------------

ShotHistoryStorage* VisualizerImporter::recoveryHistory() const
{
#ifdef DECENZA_TESTING
    if (m_testHistory) return m_testHistory;
#endif
    return m_controller ? m_controller->shotHistory() : nullptr;
}

void VisualizerImporter::recoverShots(qint64 fromEpoch, qint64 toEpoch)
{
    const auto op = Log::begin(Log::Emitter::Importer, "recovery");
    if (m_recovering) {
        op->finish("rejected", "busy");
        return;
    }
    if (authHeader().isEmpty()) {
        op->finish("rejected", "missingCredentials");
        emit recoveryFailed(tr_("visualizer.error.connectFirst",
            "Connect your Visualizer account first (username and password)."));
        return;
    }
    if (!recoveryHistory()) {
        op->finish("rejected", "historyUnavailable");
        emit recoveryFailed(tr_("visualizer.error.historyUnavailable", "Shot history is not available."));
        return;
    }

    // Normalise the range (tolerate a swapped from/to).
    m_recoverFromEpoch = qMin(fromEpoch, toEpoch);
    m_recoverToEpoch   = qMax(fromEpoch, toEpoch);
    m_recoverQueue.clear();
    m_recoverTotal = 0;
    m_recoverImported = 0;
    m_recoverSkipped = 0;
    m_recoverFailed = 0;

    m_recovering = true;
    emit recoveringChanged();

    recoverFetchListPage(1, op);
}

void VisualizerImporter::recoverFetchListPage(int page, const Op& op)
{
    // GET /api/shots?page=N&items=100 — authenticated => the user's own shots.
    // The page body is processed by the shared VisualizerShotList::processPage
    // (see visualizershotlist.h): it parses the response, filters each entry's
    // `clock` against the window, and returns a verdict (keep paging / done /
    // fail). The same helper drives the uploader's back-sync, so the paging /
    // early-stop / ceiling policy lives in exactly one place.
    constexpr int kMaxPages = 50;      // 50 * 100 = 5000 shots hard cap
    constexpr int kItemsPerPage = 100;

    QUrl url(QString::fromLatin1(VISUALIZER_SHOTS_LIST_API));
    url.setQuery(QString("page=%1&items=%2").arg(page).arg(kItemsPerPage));

    QNetworkRequest request(url);
    request.setRawHeader("Authorization", authHeader().toUtf8());
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, page, op]() {
        op->set("page", page);
        op->response("recoveryList", reply);
        reply->deleteLater();

        auto fail = [this, op](const QString& msg, const QString& reason) {
            op->finish("failed", reason);
            m_recovering = false;
            emit recoveringChanged();
            emit recoveryFailed(msg);
        };

        if (reply->error() != QNetworkReply::NoError) {
            const int sc = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            fail(tr_("visualizer.error.listFailed", "Could not list your shots (HTTP %1): %2")
                 .arg(sc).arg(reply->errorString()), "requestFailed");
            return;
        }

        using namespace VisualizerShotList;
        const PageResult pr = processPage(reply->readAll(), page, kMaxPages,
                                          m_recoverFromEpoch, m_recoverToEpoch);
        switch (pr.reason) {
        case FailReason::ParseError:
            fail(tr_("visualizer.error.listParse", "Shot list response parse error: %1").arg(pr.parseError), "invalidResponse");
            return;
        case FailReason::MissingPaging:
            fail(tr_("visualizer.error.listEnvelope",
                "Unexpected response from Visualizer (check your credentials)."), "missingPaging");
            return;
        case FailReason::PageCeiling:
            fail(tr_("visualizer.error.tooMany",
                "Too many shots to search through (over %1). "
                "Pick a more recent date range.")
                .arg(kMaxPages * kItemsPerPage), "pageLimit");
            return;
        case FailReason::None:
            break;
        }

        for (const Entry& e : pr.inWindow)
            m_recoverQueue.append({e.visualizerId, e.clockEpoch});

        if (pr.verdict == Verdict::Done) {
            m_recoverTotal = static_cast<int>(m_recoverQueue.size());
            emit recoveryProgress(m_recoverTotal, 0, 0, 0);
            if (m_recoverQueue.isEmpty())
                finishRecovery(op);
            else
                recoverNextShot(op);
            return;
        }
        recoverFetchListPage(page + 1, op);
    });
}

void VisualizerImporter::recoverNextShot(const Op& op)
{
    if (m_recoverQueue.isEmpty()) {
        finishRecovery(op);
        return;
    }

    m_recoverCurrent = m_recoverQueue.takeFirst();
    op->set("remoteId", m_recoverCurrent.visualizerId);
    op->set("shotId", 0);
    op->set("retry", 0);
    m_recoverAttempts = 0;
    recoverDownloadCurrent(op);
}

void VisualizerImporter::recoverDownloadCurrent(const Op& op)
{
    // Step 1: download the full shot record (telemetry + metadata). A transient
    // network/server failure is retried a bounded number of times (mirrors the
    // uploader's transient-retry policy) so one blip mid-run doesn't permanently
    // drop an otherwise-recoverable shot; a definitive client error (4xx, e.g. a
    // deleted shot) is not retried. Retries re-fetch the SAME shot without
    // advancing the queue.
    QUrl url(QString(VISUALIZER_SHOT_DOWNLOAD_API).arg(m_recoverCurrent.visualizerId));
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", authHeader().toUtf8());
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, op]() {
        op->response("recoveryDownload", reply);
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            constexpr int kMaxAttempts = 3;   // 1 initial + up to 2 retries
            const int sc = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const bool transient = (sc == 0 || sc >= 500);  // transport error or 5xx
            m_recoverAttempts++;
            if (transient && m_recoverAttempts < kMaxAttempts) {
                op->set("retry", m_recoverAttempts);
                op->detail("recoveryDownload", "retry");
                recoverDownloadCurrent(op);   // retry the same shot
                return;
            }
            op->problem("recoveryDownload", "requestFailed");
            m_recoverFailed++;
            emit recoveryProgress(m_recoverTotal, m_recoverImported,
                                  m_recoverSkipped, m_recoverFailed);
            recoverNextShot(op);
            return;
        }
        const QByteArray shotBody = reply->readAll();

        // Step 2: fetch the profile (the download carries only a profile_url).
        // The profile is best-effort — a shot with no profile still imports.
        QUrl purl(QString::fromLatin1(VISUALIZER_PROFILE_API)
                      .arg(m_recoverCurrent.visualizerId));
        QNetworkRequest preq(purl);
        preq.setRawHeader("Authorization", authHeader().toUtf8());
        preq.setRawHeader("Accept", "application/json");
        preq.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                          QNetworkRequest::NoLessSafeRedirectPolicy);

        QNetworkReply* preply = m_networkManager->get(preq);
        connect(preply, &QNetworkReply::finished, this, [this, preply, shotBody, op]() {
            op->response("recoveryProfile", preply);
            preply->deleteLater();
            QString profileJson;
            if (preply->error() == QNetworkReply::NoError)
                profileJson = QString::fromUtf8(preply->readAll());
            else
                op->problem("recoveryProfile", "importingWithoutProfile");

            // Step 3: parse and insert (dedupes). Run the body through
            // sanitizeVisualizerJson first: visualizer.coffee can emit the
            // malformed number tokens (.5, 9.) that QJsonDocument rejects outright,
            // which would otherwise drop the whole shot (the sibling profile path
            // sanitizes for the same reason).

            // Emit progress and move to the next shot. Shared by every outcome so
            // the loop advances exactly once per shot whether it succeeded, was a
            // duplicate, or failed.
            auto advance = [this, op]() {
                emit recoveryProgress(m_recoverTotal, m_recoverImported,
                                      m_recoverSkipped, m_recoverFailed);
                recoverNextShot(op);
            };

            QJsonParseError perr{};
            const QJsonDocument doc = QJsonDocument::fromJson(sanitizeVisualizerJson(shotBody), &perr);
            if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
                op->problem("interpret", "invalidResponse");
                m_recoverFailed++;
                advance();
                return;
            }

            ShotFileParser::ParseResult res = ShotFileParser::parseVisualizerShot(
                doc.object(), profileJson, m_recoverCurrent.visualizerId,
                m_recoverCurrent.clockEpoch);
            if (!res.success) {
                op->problem("interpret", "invalidShot");
                m_recoverFailed++;
                advance();
                return;
            }

            // Insert on the DB worker thread (no DB I/O on the main thread) and
            // continue the loop from the completion callback. overwriteExisting
            // = false => existing shots are skipped (dedupe), which is exactly the
            // idempotent recovery we want.
            recoveryHistory()->importShotRecordAsync(
                res.record, false, [this, advance, op](qint64 shotId) {
                    if (shotId > 0)       m_recoverImported++;
                    else if (shotId == 0) m_recoverSkipped++;   // duplicate
                    else                  m_recoverFailed++;     // DB error
                    op->set("shotId", shotId > 0 ? shotId : 0);
                    if (shotId < 0) op->problem("save", "databaseWriteFailed");
                    else op->detail("save", shotId == 0 ? "existingShotSkipped" : "shotImported");
                    advance();
                });
        });
    });
}

void VisualizerImporter::finishRecovery(const Op& op)
{
    if (auto* history = recoveryHistory())
        history->refreshTotalShots();

    op->set("remoteId", QString()); op->set("shotId", 0);
    op->finishBatch(m_recoverTotal, m_recoverImported, m_recoverSkipped, m_recoverFailed);
    m_recovering = false;
    emit recoveringChanged();
    emit recoveryComplete(m_recoverTotal, m_recoverImported,
                          m_recoverSkipped, m_recoverFailed);
}
