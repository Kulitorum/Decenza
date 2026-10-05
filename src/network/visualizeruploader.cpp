// visualizer.coffee REST client.
//
// Authoritative API reference:   https://apidocs.visualizer.coffee
//   Spec source:                 OpenAPI 3.1 (current version 1.8.2).
//   Endpoints used here:         POST /api/shots/upload, PATCH /api/shots/{id},
//                                GET /api/shots[/{id}], /api/coffee_bags[/{id}]
//                                (GET, PATCH), /api/roasters[/{id}] (GET, POST, PATCH);
//                                VisualizerShotSync reads through makeApiJsonRequest.
//
// Schema conventions worth knowing before editing the JSON builders:
//   - Every editable scalar field is `nullable: true`. Use JSON `null`
//     to clear a value on PATCH; sending literal 0 / "" sets the field
//     to that *value* (e.g. `espresso_enjoyment: 0` displays as "Rated
//     0/100", not "Unrated"). The cupping scores (fragrance/aroma/etc.)
//     are integer 0-15 — 0 is a real score, confirming this convention.
//   - `bean_weight`, `drink_weight`, `drink_tds`, `drink_ey` are typed
//     `string` in the API schema, not number. Rails coerces, but the
//     contract is string + nullable.
//   - POST (create): omitting a field == leaving it unset on the new
//     resource. The CREATE-path builders here use skip-on-zero/empty,
//     which is the conventional Rails strong-params behavior.
//   - PATCH (update): omitting == "don't change"; `null` == "clear";
//     value == "set". `buildShotUpdateBody` writes the fields it is given
//     (null for unset, so a local clear carries — issue #1150) and omits
//     the rest (VisualizerSync::Field).

#include "core/diagnosticlogging.h"
#include "core/logfields.h"
#include "visualizeruploader.h"
#include "network/shotuploads.h"
#include "beanbase_blob.h"
#include "roastdate.h"
#include "tastecvamap.h"
#include "visualizernotes.h"
#include "visualizersync.h"
#include "visualizershotlist.h"
#include "../core/translationmanager.h"
#include "../history/coffeebagstorage.h"
#include "../core/dbutils.h"
#include "../history/shothistorystorage.h"
#include "../models/shotdatamodel.h"
#include "../core/settings.h"
#include "../core/settings_visualizer.h"
#include "../profile/profile.h"
#include "../profile/profilejson.h"
#include "../ble/de1device.h"
#include "version.h"
#include <QThread>
#include <QPointer>
#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSqlQuery>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QUrl>
#include <QHttpMultiPart>
#include <limits>
#include <algorithm>
#include <QDateTime>
#include <QTimer>
#include <QDebug>
#include <QUuid>
#include <QStandardPaths>
#include <QFile>
#include <QDir>
#include <QBuffer>
#include "shotpayloadhelpers.h"
#include "httpauth.h"

using VisualizerSync::grinderSettingWithRpm;

VisualizerUploader::VisualizerUploader(QNetworkAccessManager* networkManager, Settings* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_networkManager(networkManager)
{
    Q_ASSERT(networkManager);
    m_apiPaceClock.start();
}

void VisualizerUploader::paceApiRequest(QObject* context, std::function<void()> send)
{
    const qint64 now = m_apiPaceClock.elapsed();
    const qint64 slot = std::max(now, m_nextApiSlotMs);
    m_nextApiSlotMs = slot + kApiRequestIntervalMs;
    QTimer::singleShot(int(slot - now), context, std::move(send));
}

QString VisualizerUploader::tr_(const char* key, const char* fallback) const {
    return translateOrFallback(m_translationManager, key, fallback);
}

bool VisualizerUploader::isActive() const
{
    return m_settings->visualizer()->visualizerActive();
}

void VisualizerUploader::attemptSavedShot(qint64 shotId, Send how)
{
    if (shotId <= 0 || !m_storage) { finishAttempt({Outcome::NothingToSend, 0}); return; }
    const bool wasUploading = isUploading();
    m_jobShotId = shotId;
    m_jobHow = how;
    m_jobError.clear();
    m_jobSkipReason.clear();
    m_jobVisualizerId.clear();
    m_jobFields = 0;
    m_jobDirtySeq = -1;
    if (isUploading() != wasUploading) emit uploadingChanged();
    const QString dbPath = m_storage->databasePath();
    QPointer<VisualizerUploader> self(this);
    m_storage->runAfterQueuedWrites([self, dbPath, shotId, how]() {
        ShotProjection shot;
        quint32 dirty = 0;
        qint64 seq = 0;
        bool dirtyRead = false;
        withTempDb(dbPath, "viz_upload", [&](QSqlDatabase& db) {
            shot = ShotHistoryStorage::convertShotRecord(
                ShotHistoryStorage::loadShotRecordStatic(db, shotId, nullptr, Q_FUNC_INFO));
            dirtyRead = ShotHistoryStorage::readVisualizerDirtyStatic(db, shotId, &dirty, &seq);
        });
        QMetaObject::invokeMethod(qApp, [self, shot, shotId, how, dirty, seq, dirtyRead]() {
            if (!self) return;
            self->m_jobDirtySeq = dirtyRead ? seq : -1;
            bool sent = false;
            if (!shot.visualizerId.isEmpty()) {
                const quint32 fields = VisualizerSync::fieldsToSend(how == Send::UploadOrUpdate, dirtyRead, dirty);
                if (!dirtyRead && how == Send::UpdateOnly)
                    DIAG_WARN(VISUALIZER, "VisualizerUploader") << "shot" << shotId
                        << "edit state unreadable - updating every field, which can overwrite edits made on Visualizer";
                if (fields == 0) {
                    DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "shot" << shotId
                             << "has no edit Visualizer has not seen - nothing to update";
                } else {
                    self->m_jobVisualizerId = shot.visualizerId;
                    self->m_jobFields = fields;
                    sent = self->updateShotOnVisualizer(shot.visualizerId, shot, fields);
                }
            } else if (how == Send::UploadOrUpdate) {
                self->m_jobFields = VisualizerSync::kAllFields;
                sent = self->uploadShotFromHistory(shot);
            }
            if (!sent) self->endAttempt(shotId, Outcome::NothingToSend);
        }, Qt::QueuedConnection);
    });
}

bool VisualizerUploader::holdsShot(QSqlDatabase& db, qint64 shotId) const
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT visualizer_id FROM shots WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), shotId);
    return query.exec() && query.next() && !query.value(0).toString().isEmpty();
}

void VisualizerUploader::clearJobDirty()
{
    if (m_jobDirtySeq >= 0)
        m_storage->requestClearVisualizerDirty(m_jobShotId, m_jobFields, m_jobDirtySeq);
}

void VisualizerUploader::noteJobFailure(const QString& message, const QString& visualizerId)
{
    if (m_jobShotId != 0 && (visualizerId.isEmpty() || visualizerId == m_jobVisualizerId))
        m_jobError = message;
}

void VisualizerUploader::setUploading(bool uploading)
{
    const bool wasUploading = isUploading();
    m_uploading = uploading;
    if (isUploading() != wasUploading) emit uploadingChanged();
}

bool VisualizerUploader::jobAttemptMayRetry(Outcome outcome, const QString& visualizerId) const
{
    return outcome == Outcome::Transient && m_jobShotId != 0
           && (visualizerId.isEmpty() || visualizerId == m_jobVisualizerId);
}

void VisualizerUploader::endAttempt(qint64 shotId, Outcome outcome, int httpStatus)
{
    if (m_jobShotId == 0 || m_jobShotId != shotId) return;
    m_jobVisualizerId.clear();
    finishAttempt({outcome, httpStatus});
}

void VisualizerUploader::sendFinished(qint64 shotId, Attempt last)
{
    if (m_jobShotId != shotId) return;
    const QString error = m_jobError, skipReason = m_jobSkipReason;
    // A transient failure is announced once, when no attempt follows it.
    if (last.outcome == Outcome::Transient) {
        m_lastUploadStatus = tr_("visualizer.status.failed", "Failed: %1").arg(error);
        emit lastUploadStatusChanged();
        emit uploadFailed(error);
        DIAG_WARN(VISUALIZER, "VisualizerUploader") << QStringLiteral("shot %1 not uploaded after %2 attempts (HTTP %3: %4)")
            .arg(shotId).arg(ShotUploads::kAttempts).arg(last.httpStatus).arg(error);
    }
    m_jobVisualizerId.clear();
    m_jobError.clear();
    m_jobSkipReason.clear();
    m_jobFields = 0;
    m_jobDirtySeq = -1;
    const bool wasUploading = isUploading();
    m_jobShotId = 0;
    if (isUploading() != wasUploading) emit uploadingChanged();
    emit savedShotFinished(shotId, error, skipReason);
}

bool VisualizerUploader::uploadShotFromHistory(const ShotProjection& shotData)
{
    if (!shotData.isValid()) {
        const QString message = tr_("visualizer.upload.noShotData", "No shot data available");
        noteJobFailure(message);
        emit uploadFailed(message);
        return false;
    }
    if (!validateUpload(shotData))
        return false;

    // de1app sends its whole ::DE1 array here; these are the key fields, read at upload time.
    QJsonObject machineState;
    if (m_device) {
        if (!m_device->firmwareVersion().isEmpty())
            machineState["firmware_version"] = m_device->firmwareVersion();
        machineState["state"] = m_device->stateString();
        machineState["substate"] = m_device->subStateString();
        machineState["headless"] = m_device->isHeadless() ? 1 : 0;
    }
    m_uploadingDbShotId = shotData.id;
    ++m_shotPushGeneration[shotData.id];
    // Visualizer reads uploaded notes as Markdown, so escape them to read as
    // typed. Here, not in buildHistoryShotJson, which also writes local exports.
    ShotProjection forUpload = shotData;
    forUpload.espressoNotes = VisualizerNotes::escapeMarkdown(shotData.espressoNotes);
    sendUpload(buildHistoryShotJson(forUpload, false, machineState));
    return true;
}

// static
QJsonObject VisualizerUploader::buildShotUpdateBody(const ShotProjection& shotData, quint32 fields)
{
    // A PATCH leaves an omitted field alone, so only `fields` are written, and a
    // written field the user has unset locally goes as JSON null: the API marks
    // every editable field nullable, and a literal 0/"" would SET it ("Rated
    // 0/100" rather than Unrated — issue #1150, migration 16's back-sync).
    namespace VS = VisualizerSync;
    QJsonObject shotObj;
    auto setStr = [&](quint32 field, const char* apiField, const QString& s) {
        if (fields & field)
            shotObj[QLatin1StringView(apiField)] = s.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(s);
    };
    auto setDouble = [&](quint32 field, const char* apiField, double v) {
        if (fields & field)
            shotObj[QLatin1StringView(apiField)] = v > 0 ? QJsonValue(v) : QJsonValue(QJsonValue::Null);
    };

    setStr(VS::BeanBrand, "bean_brand", shotData.beanBrand);
    setStr(VS::BeanType, "bean_type", shotData.beanType);
    setStr(VS::RoastLevel, "roast_level", shotData.roastLevel);
    // The one field that can hold a legacy non-ISO display string.
    setStr(VS::RoastDate, "roast_date", RoastDate::toIso(shotData.roastDate));
    setDouble(VS::DoseWeight, "bean_weight", shotData.doseWeightG);
    setDouble(VS::FinalWeight, "drink_weight", shotData.finalWeightG);
    // No separate brand field in the API.
    setStr(VS::GrinderModel, "grinder_model",
           (shotData.grinderBrand.trimmed() + " " + shotData.grinderModel.trimmed()).trimmed());
    setStr(VS::GrinderSetting, "grinder_setting", VS::grinderSettingWithRpm(shotData.grinderSetting, shotData.rpm));
    setDouble(VS::DrinkTds, "drink_tds", shotData.drinkTdsPct);
    setDouble(VS::DrinkEy, "drink_ey", shotData.drinkEyPct);
    if (fields & VS::Enjoyment)
        shotObj["espresso_enjoyment"] = shotData.enjoyment0to100 > 0 ? QJsonValue(shotData.enjoyment0to100)
                                                                     : QJsonValue(QJsonValue::Null);
    // A PATCH reads notes as HTML, so plain newlines must become markup.
    setStr(VS::EspressoNotes, "espresso_notes", VisualizerNotes::plainToHtml(shotData.espressoNotes));
    setStr(VS::Barista, "barista", shotData.barista);
    setStr(VS::ProfileTitle, "profile_title", shotData.profileName);
    // Structured taste taps → CVA (add-ai-taste-intake). A tap cleared here
    // clears its scores (Taste is dirty only when the taps changed); the full
    // update never nulls them, which would wipe a hand-entered CVA score the
    // taps never knew about.
    if (fields & VS::Taste) {
        if (fields != VS::kAllFields) {
            if (shotData.tasteBalance.isEmpty()) {
                shotObj["acidity"] = QJsonValue(QJsonValue::Null);
                shotObj["bitterness"] = QJsonValue(QJsonValue::Null);
            }
            if (shotData.tasteBody.isEmpty())
                shotObj["mouthfeel"] = QJsonValue(QJsonValue::Null);
        }
        applyTasteCvaMapping(shotObj, shotData.tasteBalance, shotData.tasteBody);
    }

    // Canonical bean linkage (5C): the Visualizer canonical UUID from the shot's
    // Bean Base snapshot, so the shot clusters by bean there too (accepted for
    // ALL users; the server back-fills bean fields from the canonical record).
    // Never sent as null: the user may have linked the bag in Visualizer's UI.
    if ((fields & VS::CanonicalBean) && !shotData.beanBaseJson.isEmpty()) {
        // A NON-EMPTY blob that fails to parse is corruption, not "unlinked".
        if (!QJsonDocument::fromJson(shotData.beanBaseJson.toUtf8()).isObject())
            DIAG_WARN(VISUALIZER, "VisualizerUploader") << "corrupt beanBaseJson on shot" << shotData.id;
        const QString canonicalId = BeanBaseBlob::canonicalId(shotData.beanBaseJson);
        // Withheld when the record names a different coffee than the shot: the
        // server rewrites bean_brand/bean_type from it on link, which would
        // rename the shot (see canonicalIdentityConflicts).
        if (BeanBaseBlob::canonicalIdentityConflicts(shotData.beanBaseJson,
                                                     {shotData.beanBrand, shotData.beanType})) {
            DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "canonical link withheld -" << shotData.beanBrand
                     << "/" << shotData.beanType
                     << "is not what the linked canonical record is named";
        } else if (!canonicalId.isEmpty()) {
            shotObj["canonical_coffee_bag_id"] = canonicalId;
        }
    }
    return shotObj;
}

bool VisualizerUploader::updateShotOnVisualizer(const QString& visualizerId, const ShotProjection& shotData,
                                                quint32 fields)
{
    if (visualizerId.isEmpty()) {
        emit uploadFailed(tr_("visualizer.error.noVizId", "No visualizer ID for update"));
        emit updateFailed(visualizerId, false, "No visualizer ID for update");
        return false;
    }

    // Check credentials
    QString username = m_settings->value("visualizer/username", "").toString();
    QString password = m_settings->value("visualizer/password", "").toString();

    if (username.isEmpty() || password.isEmpty()) {
        m_lastUploadStatus = tr_("visualizer.upload.noCredentials", "No credentials configured");
        emit lastUploadStatusChanged();
        noteJobFailure(tr_("visualizer.upload.credentialsMissing", "Visualizer credentials not configured"), visualizerId);
        emit uploadFailed(tr_("visualizer.upload.credentialsMissing", "Visualizer credentials not configured"));
        emit updateFailed(visualizerId, false, "Visualizer credentials not configured");
        return false;
    }

    setUploading(true);
    m_lastUploadStatus = tr_("visualizer.status.updating", "Updating...");
    emit lastUploadStatusChanged();

    QJsonObject root;
    root["shot"] = buildShotUpdateBody(shotData, fields);
    ++m_shotPushGeneration[shotData.id];

    QJsonDocument doc(root);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

    DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "Updating shot" << DecenzaLog::field(visualizerId);

    // Build PATCH request
    QUrl url(QString(VISUALIZER_SHOTS_API_URL) + visualizerId);
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", authHeader().toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    // Use QBuffer for sendCustomRequest to ensure Content-Type is preserved
    QBuffer* buffer = new QBuffer();
    buffer->setData(jsonData);
    buffer->open(QIODevice::ReadOnly);

    QNetworkReply* reply = m_networkManager->sendCustomRequest(request, "PATCH", buffer);
    buffer->setParent(reply);  // Auto-delete buffer when reply is deleted
    connect(reply, &QNetworkReply::finished, this, [this, reply, visualizerId]() {
        onUpdateFinished(reply, visualizerId);
    });
    return true;
}

void VisualizerUploader::onUpdateFinished(QNetworkReply* reply, const QString& visualizerId)
{
    setUploading(false);

    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray response = reply->readAll();

    Outcome outcome = Outcome::Sent;
    if (reply->error() == QNetworkReply::NoError) {
        m_lastUploadStatus = tr_("visualizer.status.updateSuccess", "Update successful");
        emit lastUploadStatusChanged();
        emit updateSuccess(visualizerId);
        DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "Update successful for shot" << visualizerId;
        if (visualizerId == m_jobVisualizerId)
            clearJobDirty();
    } else {
        const QString errorMsg = statusCode == 404
            ? tr_("visualizer.error.shotNotFound", "Shot not found on Visualizer")
            : apiErrorMessage(statusCode, response, reply->errorString());
        // 404: the shot is gone from Visualizer, so it no longer holds it (below).
        outcome = statusCode == 404 ? Outcome::NothingToSend : responseOutcome(statusCode, statusCode == 0);
        noteJobFailure(errorMsg, visualizerId);
        if (jobAttemptMayRetry(outcome, visualizerId)) {
            DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << QStringLiteral("Update attempt failed: remoteShotId=%1 httpStatus=%2 networkError=%3")
                .arg(DecenzaLog::field(visualizerId)).arg(statusCode).arg(int(reply->error()));
        } else {
            m_lastUploadStatus = tr_("visualizer.status.failed", "Failed: %1").arg(errorMsg);
            emit lastUploadStatusChanged();
            emit uploadFailed(errorMsg);
            DIAG_WARN(VISUALIZER, "VisualizerUploader") << QStringLiteral("Update failed: remoteShotId=%1 httpStatus=%2 networkError=%3")
                .arg(DecenzaLog::field(visualizerId)).arg(statusCode).arg(int(reply->error()));
        }
        // The migration-16 back-sync, which PATCHes outside ShotUploads, retries
        // on a later boot unless the shot is gone.
        emit updateFailed(visualizerId, statusCode == 404, errorMsg);
    }

    reply->deleteLater();
    if (m_jobVisualizerId.isEmpty() || visualizerId != m_jobVisualizerId) return;
    if (statusCode == 404) {
        // Deleted on visualizer.coffee: drop the dead link, and upload the shot again
        // within this attempt if the job was an upload. The clear is queued before the re-read.
        m_storage->requestClearStaleVisualizerLink(m_jobShotId, visualizerId);
        if (m_jobHow == Send::UploadOrUpdate) {
            attemptSavedShot(m_jobShotId, Send::UploadOrUpdate);
            return;
        }
    }
    endAttempt(m_jobShotId, outcome, statusCode);
}

void VisualizerUploader::connectAccount(const QString& username, const QString& password)
{
    if (m_connecting || username.trimmed().isEmpty() || password.isEmpty()) return;
    // Re-detect Coffee Management on the next upload — the user may have
    // toggled it (or switched accounts) since the last probe.
    setCmState(CmState::Unknown);

    QNetworkRequest request(QUrl("https://visualizer.coffee/api/shots?items=1"));
    request.setRawHeader("Authorization", basicAuthHeader(username, password));
    request.setTransferTimeout(15000);
    m_connecting = true;
    emit connectingChanged();

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, username, password]() {
        reply->deleteLater();
        m_connecting = false;
        emit connectingChanged();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // The shot list's {"data":[...]}; a captive portal or proxy page is not it.
        const bool isShotList = reply->error() == QNetworkReply::NoError
            && QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("data")).isArray();
        if (isShotList) {
            m_settings->visualizer()->setVisualizerUsername(username.trimmed());
            m_settings->visualizer()->setVisualizerPassword(password);
            m_settings->visualizer()->setVisualizerEnabled(true);
            DIAG_INFO(VISUALIZER, "VisualizerUploader") << "account connected";
            emit accountConnectFinished(AccountLink::Error::None);
        } else if (status == 401 || status == 403) {
            DIAG_INFO(VISUALIZER, "VisualizerUploader") << "connect rejected: username or password not accepted";
            emit accountConnectFinished(AccountLink::Error::Rejected);
        } else if (reply->error() == QNetworkReply::NoError) {
            DIAG_WARN(VISUALIZER, "VisualizerUploader") << "connect failed: HTTP" << status << "answer is not a shot list";
            emit accountConnectFinished(AccountLink::Error::ServerError);
        } else {
            DIAG_WARN(VISUALIZER, "VisualizerUploader") << "connect failed: HTTP" << status << reply->errorString();
            emit accountConnectFinished(status == 0 ? AccountLink::Error::Unreachable : AccountLink::Error::ServerError);
        }
    });
}

void VisualizerUploader::disconnectAccount()
{
    m_settings->visualizer()->setVisualizerUsername(QString());
    m_settings->visualizer()->setVisualizerPassword(QString());
    DIAG_INFO(VISUALIZER, "VisualizerUploader") << "account disconnected";
}

void VisualizerUploader::onUploadFinished(QNetworkReply* reply)
{
    const qint64 diagnosticShotId = m_uploadingDbShotId; // Result signals may start another upload.
    setUploading(false);

    // Save response to debug file
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray response = reply->readAll();

    QString responseFile = uploadDebugFilePath(QStringLiteral("last_upload_response.txt"));
    QFile file(responseFile);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QString("HTTP Status: %1\n\n").arg(statusCode).toUtf8());
        file.write(response);
        file.close();
    }

    Outcome outcome = Outcome::Sent;
    if (reply->error() == QNetworkReply::NoError) {
        QJsonDocument doc = QJsonDocument::fromJson(response);
        QJsonObject obj = doc.object();

        QString shotId = obj["id"].toString();
        if (!shotId.isEmpty()) {
            m_lastShotUrl = QString(VISUALIZER_SHOT_URL) + shotId;
            m_lastUploadStatus = tr_("visualizer.status.uploadSuccess", "Upload successful");
            emit lastShotUrlChanged();
            emit lastUploadStatusChanged();
            emit uploadSuccess(shotId, m_lastShotUrl);
            // Authoritative C++ writeback path: carry the originating
            // local shots.id so MainController can persist the link
            // regardless of which (if any) UI page is alive.
            emit uploadSucceededForShot(m_uploadingDbShotId, shotId, m_lastShotUrl);
            if (m_uploadingDbShotId == m_jobShotId)
                clearJobDirty();
            DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "Upload successful, ID:" << shotId
                     << "for local shot" << diagnosticShotId;
            // Coffee Management: the server auto-links the shot to its bag on
            // upload; read that link back and enrich the bag's descriptive fields.
            syncCoffeeBagAfterUpload(m_uploadingDbShotId, shotId);
        } else {
            // A 200 with no parseable shot id is a failure, not a success: the
            // local shot row never gets its visualizer_id and the bag sync
            // chain never runs. Surface it so the user sees an error and a
            // retry path, instead of a benign-looking "completed" status.
            // Nothing was stored, as with a 2xx that is not Decent's API answer: try again.
            outcome = Outcome::Transient;
            const QString message = tr_("visualizer.error.noShotIdReturned", "Upload returned no shot id (unexpected response)");
            DIAG_WARN(VISUALIZER, "VisualizerUploader") << QStringLiteral("Upload failed: reason=missingShotId shotId=%1 httpStatus=%2")
                .arg(diagnosticShotId).arg(statusCode);
            noteJobFailure(message);
        }
    } else {
        outcome = responseOutcome(statusCode, statusCode == 0);
        const QString errorMsg = apiErrorMessage(statusCode, response, reply->errorString());
        noteJobFailure(errorMsg);
        if (jobAttemptMayRetry(outcome)) {
            DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << QStringLiteral("Upload attempt failed: shotId=%1 httpStatus=%2 networkError=%3")
                .arg(diagnosticShotId).arg(statusCode).arg(int(reply->error()));
        } else {
            m_lastUploadStatus = tr_("visualizer.status.failed", "Failed: %1").arg(errorMsg);
            emit lastUploadStatusChanged();
            emit uploadFailed(errorMsg);
            DIAG_WARN(VISUALIZER, "VisualizerUploader") << QStringLiteral("Upload failed: shotId=%1 httpStatus=%2 networkError=%3")
                .arg(diagnosticShotId).arg(statusCode).arg(int(reply->error()));
        }
    }

    // Clear the per-upload id on every terminal outcome (success,
    // no-id, or failure) so a subsequent upload can't inherit a stale
    // correlation. ShotUploads never overlaps uploads.
    m_uploadingDbShotId = 0;
    reply->deleteLater();
    endAttempt(diagnosticShotId, outcome, statusCode);
}

void VisualizerUploader::fetchShotListSince(qint64 windowStartEpoch)
{
    const QString username = m_settings->value("visualizer/username", "").toString();
    const QString password = m_settings->value("visualizer/password", "").toString();
    if (username.isEmpty() || password.isEmpty()) {
        emit shotListFailed("Visualizer credentials not configured");
        return;
    }
    fetchShotListPage(1, windowStartEpoch, QVariantList());
}

void VisualizerUploader::fetchShotListPage(int page, qint64 windowStartEpoch,
                                           QVariantList accumulated)
{
    // GET /api/shots?page=N&items=100 — authenticated => own shots.
    // Response shape { data: [{id, clock, updated_at}], paging:
    // {count,page,limit,pages} } is confirmed against OpenAPI 1.8.2, and the
    // default newest-first-by-start-time sort the wholePageOlder early-stop
    // relies on is confirmed against the visualizer.coffee source (see the note
    // in visualizershotlist.h). The kMaxPages ceiling still bounds the loop and
    // turns a ceiling hit into a fail-safe retry (below) regardless.
    constexpr int kMaxPages = 50;          // 50 * 100 = 5000 shots hard cap
    constexpr int kItemsPerPage = 100;

    QUrl url("https://visualizer.coffee/api/shots");
    QString q = QString("page=%1&items=%2").arg(page).arg(kItemsPerPage);
    url.setQuery(q);

    QNetworkRequest request(url);
    request.setRawHeader("Authorization", authHeader().toUtf8());
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, page, windowStartEpoch, accumulated]() mutable {
        // Capture everything off `reply` BEFORE deleteLater() — reading
        // it afterwards is fragile and would make the one diagnostic on
        // the only failure surface unreliable.
        if (reply->error() != QNetworkReply::NoError) {
            const int sc = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QString errStr = reply->errorString();
            reply->deleteLater();
            emit shotListFailed(QString("Shot list fetch failed (HTTP %1): %2")
                                .arg(sc).arg(errStr));
            return;
        }
        const QByteArray body = reply->readAll();
        reply->deleteLater();

        // Parse + window-filter + terminate via the shared page processor (see
        // visualizershotlist.h). The recovery importer runs the identical policy;
        // keeping it in one place stops the two copies from drifting.
        using namespace VisualizerShotList;
        const PageResult pr = processPage(body, page, kMaxPages,
                                          windowStartEpoch,
                                          std::numeric_limits<qint64>::max());
        switch (pr.reason) {
        case FailReason::ParseError:
            emit shotListFailed(QStringLiteral("Shot list response parse error: %1")
                                .arg(pr.parseError));
            return;
        case FailReason::MissingPaging:
            // A 200 without paging metadata is almost certainly an auth/error
            // envelope (e.g. expired session returning {}), NOT a legitimately
            // empty library — treating it as success would permanently burn the
            // run-once flag. Fail safe instead.
            emit shotListFailed(QStringLiteral(
                "Shot list response missing paging metadata (likely auth/error envelope)"));
            return;
        case FailReason::PageCeiling:
            // Hitting the defensive page ceiling without reaching the real end is
            // an ABNORMAL exit (oversized library, or the assumed newest-first
            // sort was violated). Emitting a truncated list as "success" would
            // permanently mark the backfill done with missing shots. Fail safe.
            emit shotListFailed(QStringLiteral(
                "Shot list exceeded page ceiling (%1) before end (%2 pages) — "
                "backfill incomplete, will retry next boot")
                .arg(kMaxPages).arg(pr.totalPages));
            return;
        case FailReason::None:
            break;
        }

        for (const Entry& e : pr.inWindow) {
            QVariantMap m;
            m["visualizerId"] = e.visualizerId;
            m["url"] = QString(VISUALIZER_SHOT_URL) + e.visualizerId;
            m["clockEpoch"] = e.clockEpoch;
            accumulated.append(m);
        }

        if (pr.verdict == Verdict::Done) {
            emit shotListFetched(accumulated);
            return;
        }
        fetchShotListPage(page + 1, windowStartEpoch, accumulated);
    });
}

void VisualizerUploader::repairShotBeans(const QVector<BeanRepair>& repairs)
{
    // Re-entry is checked FIRST: an empty snapshot arriving mid-pass must not
    // announce a finished pass, or a listener re-drains against a queue that is
    // still being written.
    if (m_beanRepairRunning) {
        // The DB flags are the durable state, so nothing is lost — this snapshot
        // is simply older than the one already draining. Recording that one was
        // dropped is what lets the caller re-drain when the pass ends, so a bag
        // unlinked mid-pass is not stranded until the next launch.
        m_beanRepairMissedWork = true;
        DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "bean repair already running - ignoring re-entry";
        return;
    }
    // Cleared for ANY snapshot we accept, empty included: this one supersedes
    // whatever was dropped, so the flag must not survive into the next pass and
    // trigger a re-drain that has nothing to find.
    m_beanRepairMissedWork = false;
    if (repairs.isEmpty()) {
        // Deliberately silent. Emitting beanRepairFinished here would let the
        // re-drain consumer answer its own signal with another empty read.
        return;
    }
    m_beanRepairQueue = repairs;
    m_beanRepairDone = 0;
    m_beanRepairCleared = 0;
    m_beanRepairDeclined = 0;
    m_beanRepairFailed = false;
    m_beanRepairRunning = true;
    DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "bean repair over" << repairs.size() << "queued shot(s)";
    sendNextBeanRepair();
}

void VisualizerUploader::sendNextBeanRepair()
{
    if (m_beanRepairQueue.isEmpty()) {
        m_beanRepairRunning = false;
        DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "bean repair pass ended -" << m_beanRepairDone
                 << "name(s) restored," << m_beanRepairCleared << "link(s) cleared,"
                 << m_beanRepairDeclined << "declined (server bag decides those)"
                 << (m_beanRepairFailed ? "- incomplete, resumes next boot" : "");
        emit beanRepairFinished(m_beanRepairDone, !m_beanRepairFailed);
        return;
    }
    const BeanRepair repair = m_beanRepairQueue.takeFirst();
    if (repair.visualizerId.isEmpty() || repair.shotId <= 0) {
        // The producer's SQL makes this unreachable; if it ever becomes
        // reachable, an empty id addresses the COLLECTION endpoint and a shotId
        // of 0 can never be settled, so refuse rather than send.
        DIAG_WARN(VISUALIZER, "VisualizerUploader") << "skipping malformed bean-repair entry (shot"
                   << repair.shotId << "id" << repair.visualizerId << ")";
        m_beanRepairFailed = true;
        scheduleNextBeanRepair();
        return;
    }
    // Read the shot BEFORE deciding anything. The paged list cannot answer this
    // (it carries no bean fields at all), and a repair that writes without
    // reading has nothing to compare against. `essentials` drops the shot's
    // chart data from the response (shots_controller.rb:24 —
    // `include_information: !params[:essentials].presence`); this pass only ever
    // reads two string fields.
    QNetworkRequest request = makeApiJsonRequest(QStringLiteral("/api/shots/")
                                                 + repair.visualizerId
                                                 + QStringLiteral("?essentials=1"));
    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, repair]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError netError = reply->error();
        const QString netErrorText = reply->errorString();
        const QByteArray body = reply->readAll();

        if (isBeanRepairFatalStatus(status)) {
            abandonBeanRepairPass(status);
            return;
        }
        if (status == 404) {
            // `with_shot`'s Shot.find_by came back empty (shots_controller.rb:
            // 105-112) — deleted on visualizer.coffee. Nothing to repair, and the
            // flag has to go or it is re-read on every boot forever.
            DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "bean repair skipped - shot" << repair.visualizerId
                     << "is gone";
            emit beanRepairSettled(repair.shotId);
            scheduleNextBeanRepair();
            return;
        }
        if (status == 403) {
            // NOT an ownership verdict, however much it looks like one. `show`
            // does not run load_users_shot — that before_action is
            // `only: %i[update destroy]` (shots_controller.rb:8) — and `with_shot`
            // carries no authorize!, so Rails returns 200 here even for another
            // account's shot. A 403 on this GET therefore comes from something in
            // front of the app (WAF, bot challenge, proxy), which is account- or
            // IP-wide: settling shots against it would clear every flag in the
            // queue, irreversibly, for a condition that has nothing to do with
            // any individual shot.
            abandonBeanRepairPass(status);
            return;
        }
        if (netError != QNetworkReply::NoError) {
            m_beanRepairFailed = true;
            DIAG_WARN(VISUALIZER, "VisualizerUploader") << "bean repair read failed for shot" << repair.visualizerId
                       << "(HTTP" << status << "," << netError << netErrorText << ") - stays queued";
            scheduleNextBeanRepair();
            return;
        }

        // A body we cannot read is NOT "the server holds blank names". Parsed
        // loosely, a captive portal page, a proxy error or a 204 all yield an
        // empty object, which compares as a disagreement — and in live mode that
        // would PATCH empty bean names onto the user's shot and settle it as
        // done. Same shape as reconcileShotBag's read-back guard below.
        QJsonParseError parseError{};
        const QJsonObject remote = QJsonDocument::fromJson(body, &parseError).object();
        if (parseError.error != QJsonParseError::NoError
            || !remote.contains(QStringLiteral("bean_brand"))) {
            m_beanRepairFailed = true;
            DIAG_WARN(VISUALIZER, "VisualizerUploader") << "bean repair read unusable for shot" << repair.visualizerId
                       << "-" << parseError.errorString() << "- stays queued";
            scheduleNextBeanRepair();
            return;
        }

        const RemoteBagState bagState = remoteCoffeeBagState(remote);
        if (bagState == RemoteBagState::Unreadable) {
            // Same rule as the bean_brand guard above: a field we cannot read is
            // not evidence. This one decides whether the pass WRITES, so a wrong
            // answer here is a PATCH the account did not need.
            m_beanRepairFailed = true;
            DIAG_WARN(VISUALIZER, "VisualizerUploader") << "bean repair - shot" << repair.visualizerId
                       << "returned an unreadable coffee_bag_id - stays queued";
            scheduleNextBeanRepair();
            return;
        }
        switch (planBeanRepair(bagState == RemoteBagState::Present,
                               repair.beanBrand, repair.beanType)) {
        case BeanRepairPlan::NothingToDo:
            // A shot with a server coffee_bag takes shot.rb's first branch on
            // every touch, so its identity comes from that bag and no write to
            // the SHOT can change it. The residual work, if any, is on the bag.
            m_beanRepairDeclined++;
            DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "bean repair declined - shot" << repair.visualizerId
                     << "takes its identity from a server coffee_bag; nothing written."
                     << "If it still reads wrong, the bag is what to correct.";
            emit beanRepairSettled(repair.shotId);
            scheduleNextBeanRepair();
            return;
        case BeanRepairPlan::ClearCanonicalOnly:
            DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "bean repair - shot" << repair.visualizerId
                     << "has no complete local bean names; clearing the canonical link only";
            scheduleBeanRepairRequest([this, repair]() { sendCanonicalClearOnly(repair); });
            return;
        case BeanRepairPlan::RestoreNames:
            break;
        }

        const QString remoteBrand = remote.value(QStringLiteral("bean_brand")).toString().trimmed();
        const QString remoteType = remote.value(QStringLiteral("bean_type")).toString().trimmed();
        if (decideBeanRepair(remoteBrand, remoteType, repair.beanBrand, repair.beanType)
            == BeanRepairAction::AlreadyCorrect) {
            // The server already agrees — the common case, and it must not
            // produce a write.
            emit beanRepairSettled(repair.shotId);
            scheduleNextBeanRepair();
            return;
        }

        // Logged BEFORE the write, and carrying both sides: this line is the
        // only record of what a user's cloud history looked like before the
        // repair touched it.
        const QDateTime when = QDateTime::fromSecsSinceEpoch(repair.timestamp);
        DIAG_DEBUG(VISUALIZER, "VisualizerUploader").noquote() << "Visualizer bean repair"
                           << when.toString(Qt::ISODate)
                           << "server:" << (remoteBrand + QLatin1String(" / ") + remoteType)
                           << "-> app:" << (repair.beanBrand + QLatin1String(" / ") + repair.beanType)
                           << "| canonical:"
                           << (repair.canonicalId.isEmpty() ? QStringLiteral("clear")
                                                            : QStringLiteral("keep"));
        scheduleBeanRepairRequest([this, repair]() { sendBeanRepairPatch(repair); });
    });
}

// static
VisualizerUploader::RemoteBagState
VisualizerUploader::remoteCoffeeBagState(const QJsonObject& remote)
{
    // `coffee_bag_id` is emitted ONLY when the shot has a bag: it is built as
    // `coffee_bag_id: coffee_bag&.id` (shot/jsonable.rb:71) inside a hash that
    // ends in `attributes.compact` (:80), so a nil is stripped and the key is
    // ABSENT rather than null. An earlier version of this code claimed the key
    // was always present and tested `!isNull()`, which is true for an absent key
    // too (QJsonValue::Undefined is not Null) — the answer was right only
    // because a second clause happened to carry it.
    const QJsonValue value = remote.value(QStringLiteral("coffee_bag_id"));
    if (value.isUndefined() || value.isNull())
        return RemoteBagState::Absent;
    // Anything that is not a string is a shape we do not understand. NOT "no
    // bag": QJsonValue::toString() returns an empty QString for every non-string
    // type (qjsonvalue.cpp:780-783 -> qcborvalue.cpp:2251-2254), so reading it
    // that way would silently turn an unparseable field into a licence to write.
    if (!value.isString())
        return RemoteBagState::Unreadable;
    return value.toString().isEmpty() ? RemoteBagState::Absent : RemoteBagState::Present;
}

// static
VisualizerUploader::BeanRepairPlan VisualizerUploader::planBeanRepair(
    bool remoteHasCoffeeBag, const QString& localBrand, const QString& localType)
{
    // The server bag decides FIRST, whatever the names say. A shot with a
    // coffee_bag takes refresh_coffee_bag_fields' first branch, which re-derives
    // bean_brand, bean_type AND canonical_coffee_bag_id from that bag
    // (shot.rb:64-75) — so nothing this pass sends to the shot can survive, and
    // the attempt is not free: the callback fires on any id change and
    // re-renders roast_date into the user's date format. Measured on a live
    // account. This used to gate only the canonical clear, which left the same
    // futile write reachable through the names arm whenever local and server had
    // drifted — and drift is the cohort this pass exists for, not the exception.
    if (remoteHasCoffeeBag)
        return BeanRepairPlan::NothingToDo;

    // Complete local names are the only case that may assert an identity —
    // sending an empty one BLANKS the server's value, which is the destructive
    // direction of the rule the storage predicate states ("an empty name on
    // either side proves nothing"). Per FIELD: a brand with no type is enough to
    // wipe the type.
    if (!localBrand.trimmed().isEmpty() && !localType.trimmed().isEmpty())
        return BeanRepairPlan::RestoreNames;

    // No names to assert and no bag to override us: drop the borrowed canonical
    // id so the server stops re-deriving this shot's identity from another
    // roaster's record.
    return BeanRepairPlan::ClearCanonicalOnly;
}

// static
VisualizerUploader::BeanRepairAction VisualizerUploader::decideBeanRepair(
    const QString& remoteBrand, const QString& remoteType,
    const QString& localBrand, const QString& localType)
{
    // Trim-and-case-insensitive, matching the storage-side predicate. NOT
    // because the server normalises these: `Shot` does not include Squishable
    // (only CoffeeBag and the roaster models do), so a shot's bean_brand is
    // stored verbatim. The reason is simply that a whitespace or capitalisation
    // difference is not worth a write to a user's cloud account.
    return (remoteBrand.trimmed().compare(localBrand.trimmed(), Qt::CaseInsensitive) == 0
            && remoteType.trimmed().compare(localType.trimmed(), Qt::CaseInsensitive) == 0)
        ? BeanRepairAction::AlreadyCorrect : BeanRepairAction::NeedsPatch;
}

// static
bool VisualizerUploader::isBeanRepairFatalStatus(int status)
{
    // Statuses that cannot differ per shot: retrying the rest of the queue
    // against them is guaranteed waste. 429 is the measured one — the first live
    // version walked 349 shots collecting them — and a revoked or rotated
    // password (401) behaves identically, one failure per shot per boot, forever.
    //
    // 403 is NOT one of these, though it reads like it. It comes from
    // `authorize! @shot` (shots_controller.rb:98-102), so it is an ownership
    // verdict on one shot; treating it as fatal let a single mis-recorded
    // visualizer_id abandon the pass at the same position on every boot and
    // strand every shot behind it. It is handled per shot instead.
    return status == 429 || status == 401;
}

void VisualizerUploader::sendBeanRepairPatch(const BeanRepair& repair)
{
    QJsonObject shot{
        {QStringLiteral("bean_brand"), repair.beanBrand},
        {QStringLiteral("bean_type"), repair.beanType},
        // Explicit null clears the borrowed link, and the names survive BECAUSE
        // it changes: refresh_coffee_bag_fields fires on the id change and, with
        // neither a coffee_bag nor a canonical bag, leaves the fields alone
        // (shot.rb:64-75 — the canonical assignment is the `elsif` branch).
        //
        // A shot that DOES have a server-side coffee_bag (Coffee Management)
        // takes the FIRST branch and re-derives both fields from that bag,
        // discarding what we send — verified live: the PATCH returns 200 and its
        // own body comes back holding the bag's names, not ours. That costs
        // nothing, because such a shot was never renamed in the first place: the
        // canonical assignment is the `elsif`, so with a coffee_bag present the
        // names have always come from coffee_bag.roaster.name / coffee_bag.name
        // and this pass finds it AlreadyCorrect and never PATCHes at all. The
        // read-back below is what keeps that claim honest — it fires only when
        // the server bag and the local bag have drifted apart. (Note the callback
        // is CONDITIONAL: if the shot's canonical id is already null and we send
        // null, nothing changed, it never fires, and our names are saved.)
        {QStringLiteral("canonical_coffee_bag_id"),
         repair.canonicalId.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(repair.canonicalId)},
    };
    // `essentials` for the same reason as the GET: `update` renders through the
    // same `include_information: !params[:essentials].presence`
    // (shots_controller.rb:70), so without it every repaired shot drags its whole
    // pressure/flow/temperature series back over a rate-limited connection. The
    // bean fields the read-back needs are in Jsonable::ALLOWED_ATTRIBUTES either way.
    QNetworkRequest request = makeApiJsonRequest(QStringLiteral("/api/shots/")
                                                 + repair.visualizerId
                                                 + QStringLiteral("?essentials=1"));
    QNetworkReply* reply = m_networkManager->sendCustomRequest(
        request, "PATCH", QJsonDocument(QJsonObject{{QStringLiteral("shot"), shot}})
                              .toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, repair]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        if (status >= 200 && status < 300) {
            // 200 does NOT mean the values took. `update` discards the result of
            // @shot.update and renders the in-memory object either way
            // (shots_controller.rb:65-70), and refresh_coffee_bag_fields may have
            // overwritten both fields from a server-side coffee_bag between our
            // params and the save — measured, not hypothesised. So read the body
            // back rather than reporting a write the server discarded. (This
            // catches an overwrite, not a validation failure: a rejected save
            // renders OUR values, so the body agrees and the shot settles. That
            // gap needs a status the endpoint does not give us.)
            //
            // Any 2xx is accepted rather than a literal 200 purely as slack —
            // this endpoint always renders JSON and cannot 204 today. An
            // unreadable body is refused below, so the slack costs nothing.
            QJsonParseError patchParse{};
            const QJsonObject saved = QJsonDocument::fromJson(body, &patchParse).object();
            if (patchParse.error != QJsonParseError::NoError
                || !saved.contains(QStringLiteral("bean_brand"))) {
                // Same rule as the GET: a body we cannot read is not evidence of
                // anything. Counting it as a repair and clearing the flag would
                // lose the shot permanently on a captive portal, a proxy error
                // page served as 200, or a truncated response.
                m_beanRepairFailed = true;
                DIAG_WARN(VISUALIZER, "VisualizerUploader") << "bean repair PATCH result unreadable for shot"
                           << repair.visualizerId << "-" << patchParse.errorString()
                           << "- stays queued";
                scheduleNextBeanRepair();
                return;
            }
            if (decideBeanRepair(saved.value(QStringLiteral("bean_brand")).toString(),
                                 saved.value(QStringLiteral("bean_type")).toString(),
                                 repair.beanBrand, repair.beanType)
                != BeanRepairAction::AlreadyCorrect) {
                DIAG_WARN(VISUALIZER, "VisualizerUploader").noquote()
                    << "bean repair PATCH accepted but not applied for shot"
                    << repair.visualizerId << "- server kept"
                    << (saved.value(QStringLiteral("bean_brand")).toString()
                        + QLatin1String(" / ") + saved.value(QStringLiteral("bean_type")).toString())
                    << "(its coffee_bag outranks the shot; the bag re-push is what corrects it)";
                // Settled but NOT repaired. Deliberately does NOT set
                // m_beanRepairFailed: that flag drives "resumes next boot", and
                // this shot's flag is gone, so it will not. The warning above is
                // the record; claiming a retry that cannot happen is worse than
                // saying nothing.
                m_beanRepairDeclined++;
            } else {
                m_beanRepairDone++;
            }
            emit beanRepairSettled(repair.shotId);
        } else if (isBeanRepairFatalStatus(status)) {
            abandonBeanRepairPass(status);
            return;
        } else if (status == 403 || status == 404) {
            // THIS is where an ownership verdict lands: `update` does run
            // load_users_shot (shots_controller.rb:8), so 403 is
            // `authorize! @shot` refusing a shot this account does not own and
            // 404 is its find_by missing. Neither changes on a retry, so the
            // flag must go — leaving it set re-sends the same refusal every
            // boot, at two requests per shot, forever. A shot can reach here
            // with someone else's id after a device-to-device transfer, which
            // the GET cannot detect because `show` authorizes nothing.
            DIAG_WARN(VISUALIZER, "VisualizerUploader") << "bean repair abandoned for shot" << repair.visualizerId
                       << "- HTTP" << status
                       << (status == 403 ? "(not this account's shot)" : "(gone)")
                       << "- flag cleared, it can never succeed";
            emit beanRepairSettled(repair.shotId);
        } else {
            m_beanRepairFailed = true;
            DIAG_WARN(VISUALIZER, "VisualizerUploader") << "bean repair PATCH failed for shot" << repair.visualizerId
                       << "(HTTP" << status << "," << reply->error() << reply->errorString()
                       << ") - stays queued";
        }
        scheduleNextBeanRepair();
    });
}

void VisualizerUploader::sendCanonicalClearOnly(const BeanRepair& repair)
{
    // The half of the repair that needs no local names: drop the borrowed link
    // so refresh_coffee_bag_fields stops re-deriving this shot's identity from
    // another roaster's record. The bean names are deliberately ABSENT from the
    // body — sending them empty is the write this path exists to avoid.
    const QJsonObject shot{
        {QStringLiteral("canonical_coffee_bag_id"), QJsonValue(QJsonValue::Null)},
    };
    QNetworkRequest request = makeApiJsonRequest(QStringLiteral("/api/shots/")
                                                 + repair.visualizerId
                                                 + QStringLiteral("?essentials=1"));
    QNetworkReply* reply = m_networkManager->sendCustomRequest(
        request, "PATCH", QJsonDocument(QJsonObject{{QStringLiteral("shot"), shot}})
                              .toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, repair]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 200 && status < 300) {
            // No read-back: this request asserts nothing about the names, and
            // the API never reports canonical_coffee_bag_id (it is not in
            // Jsonable::ALLOWED_ATTRIBUTES), so there is nothing to verify.
            m_beanRepairCleared++;
            emit beanRepairSettled(repair.shotId);
        } else if (isBeanRepairFatalStatus(status)) {
            abandonBeanRepairPass(status);
            return;
        } else if (status == 403 || status == 404) {
            DIAG_WARN(VISUALIZER, "VisualizerUploader") << "canonical clear abandoned for shot"
                       << repair.visualizerId << "- HTTP" << status
                       << "- flag cleared, it can never succeed";
            emit beanRepairSettled(repair.shotId);
        } else {
            m_beanRepairFailed = true;
            DIAG_WARN(VISUALIZER, "VisualizerUploader") << "canonical clear failed for shot" << repair.visualizerId
                       << "(HTTP" << status << ") - stays queued";
        }
        scheduleNextBeanRepair();
    });
}

void VisualizerUploader::abandonBeanRepairPass(int status)
{
    // Abandon rather than walking the queue collecting the same refusal — the
    // first live version did exactly that and burned 349 requests for nothing.
    // Everything still flagged is retried on a later boot.
    m_beanRepairFailed = true;
    m_beanRepairQueue.clear();
    DIAG_WARN(VISUALIZER, "VisualizerUploader") << "bean repair stopped on HTTP" << status << "after"
               << m_beanRepairDone << "shot(s) - resumes next boot";
    scheduleNextBeanRepair();
}

void VisualizerUploader::scheduleNextBeanRepair()
{
    // Pace the queue. This is a THROTTLE, not a guard: visualizer.coffee rate
    // limits — fetchShotListPage above walks its pages back to back, which is
    // why a library-wide pass had to stop being built on it.
    scheduleBeanRepairRequest([this]() { sendNextBeanRepair(); });
}

void VisualizerUploader::scheduleBeanRepairRequest(std::function<void()> send)
{
    // Every REQUEST goes through here, not every shot. That distinction is the
    // whole point: the interval used to gate only the next shot, while a shot
    // that needed repairing fired its GET and then its PATCH back to back, so
    // the real rate was double the documented one.
    paceApiRequest(this, std::move(send));
}

QJsonObject VisualizerUploader::buildAppInfoJson()
{
    QJsonObject app;
    app["app_name"] = "Decenza";
    app["app_version"] = VERSION_STRING;
    return app;
}

QJsonObject VisualizerUploader::buildProfileSettings(const Profile* profile)
{
    QJsonObject s;
    if (!profile) return s;

    s["profile_title"] = profile->title();
    if (!profile->author().isEmpty())
        s["author"] = profile->author();
    if (!profile->beverageType().isEmpty())
        s["beverage_type"] = profile->beverageType();
    if (!profile->profileNotes().isEmpty())
        s["profile_notes"] = profile->profileNotes();
    s["settings_profile_type"] = profile->profileType();

    // Temperature settings (as strings, matching de1app convention)
    s["espresso_temperature"] = ProfileJson::enc(profile->espressoTemperature(), ProfileJson::Temperature);
    const auto presets = profile->temperaturePresets();
    for (qsizetype i = 0; i < presets.size() && i < 4; ++i)
        s[QStringLiteral("espresso_temperature_%1").arg(i)] = ProfileJson::enc(presets[i], ProfileJson::Temperature);

    // Limits
    s["maximum_pressure"] = ProfileJson::enc(profile->maximumPressure(), ProfileJson::Pressure);
    s["maximum_flow"] = ProfileJson::enc(profile->maximumFlow(), ProfileJson::Flow);
    s["flow_profile_minimum_pressure"] = ProfileJson::enc(profile->minimumPressure(), ProfileJson::Pressure);
    s["tank_desired_water_temperature"] = ProfileJson::enc(profile->tankDesiredWaterTemperature(), ProfileJson::TankTemp);
    s["maximum_flow_range_advanced"] = ProfileJson::enc(profile->maximumFlowRangeAdvanced(), ProfileJson::Limiter);
    s["maximum_pressure_range_advanced"] = ProfileJson::enc(profile->maximumPressureRangeAdvanced(), ProfileJson::Limiter);

    // Target weight/volume
    s["final_desired_shot_weight"] = ProfileJson::enc(profile->targetWeight(), ProfileJson::TargetMass);
    s["final_desired_shot_weight_advanced"] = s["final_desired_shot_weight"];
    s["final_desired_shot_volume"] = ProfileJson::enc(profile->targetVolume(), ProfileJson::TargetMass);
    s["final_desired_shot_volume_advanced"] = s["final_desired_shot_volume"];
    s["final_desired_shot_volume_advanced_count_start"] = QString::number(profile->preinfuseFrameCount());

    // Simple profile parameters (settings_2a/2b — Visualizer uses these to reconstruct simple profiles)
    s["preinfusion_time"] = QString::number(profile->preinfusionTime(), 'f', 1);
    s["preinfusion_flow_rate"] = QString::number(profile->preinfusionFlowRate(), 'f', 1);
    s["preinfusion_stop_pressure"] = QString::number(profile->preinfusionStopPressure(), 'f', 1);
    s["espresso_pressure"] = QString::number(profile->espressoPressure(), 'f', 1);
    s["espresso_hold_time"] = QString::number(profile->espressoHoldTime(), 'f', 1);
    s["espresso_decline_time"] = QString::number(profile->espressoDeclineTime(), 'f', 1);
    s["pressure_end"] = QString::number(profile->pressureEnd(), 'f', 1);
    s["flow_profile_hold"] = QString::number(profile->flowProfileHold(), 'f', 1);
    s["flow_profile_decline"] = QString::number(profile->flowProfileDecline(), 'f', 1);
    s["maximum_flow_range_default"] = QString::number(profile->maximumFlowRangeDefault(), 'f', 1);
    s["maximum_pressure_range_default"] = QString::number(profile->maximumPressureRangeDefault(), 'f', 1);

    // Advanced shot frames as TCL list
    QStringList frameTclParts;
    for (const auto& step : profile->steps())
        frameTclParts << step.toTclList();
    s["advanced_shot"] = frameTclParts.join(' ');

    return s;
}

QByteArray VisualizerUploader::buildMultipartData(const QByteArray& jsonData, const QString& boundary)
{
    QByteArray data;

    // File part
    data.append("--" + boundary.toUtf8() + "\r\n");
    data.append("Content-Disposition: form-data; name=\"file\"; filename=\"shot.json\"\r\n");
    data.append("Content-Type: application/json\r\n\r\n");
    data.append(jsonData);
    data.append("\r\n");

    // End boundary
    data.append("--" + boundary.toUtf8() + "--\r\n");

    return data;
}

QString VisualizerUploader::authHeader() const
{
    return QString::fromLatin1(basicAuthHeader(m_settings->value("visualizer/username", "").toString(),
                                               m_settings->value("visualizer/password", "").toString()));
}

QString VisualizerUploader::apiErrorMessage(int status, const QByteArray& body, const QString& transportError) const
{
    if (status == 401)
        return tr_("visualizer.error.invalidCredentials", "Invalid credentials");
    // Visualizer explains most refusals in a JSON `error`: a 422's validation
    // failure, a 403 for a disabled account, a 429 rate limit, a 400 for a
    // malformed parameter (api/base_controller.rb).
    const QString serverMessage = QJsonDocument::fromJson(body).object().value(QStringLiteral("error")).toString();
    if (!serverMessage.isEmpty())
        return serverMessage;
    if (status == 429)
        return tr_("visualizer.error.rateLimited", "Visualizer is busy - try again in a few minutes");
    if (status == 422)
        return tr_("visualizer.error.invalidShotData422", "Invalid shot data (422)");
    return tr_("visualizer.error.http", "HTTP %1: %2").arg(status).arg(transportError);
}

bool VisualizerUploader::validateUpload(const ShotProjection& shot)
{
    const double minDuration = m_settings->upload()->minDuration();
    const QString beverageType = uploadBeverageType(shot);
    const double duration = shot.durationSec;
    const UploadIneligible ineligible = uploadIneligibility(shot, minDuration);
    if (ineligible == UploadIneligible::Maintenance) {
        const QString reason = tr_("visualizer.skip.maintenance", "maintenance profile (%1)").arg(beverageType);
        m_lastUploadStatus = tr_("visualizer.status.skipped", "Skipped: %1").arg(reason);
        emit lastUploadStatusChanged();
        // Policy skip, not an error — uploadSkipped lets the page clear its
        // in-flight flags without surfacing a red error to UI listeners that
        // treat uploadFailed as a real failure. The page wraps the reason
        // with a translated "Upload skipped:" prefix; emit just the reason
        // payload so the C++ "Skipped:" prefix doesn't double up.
        m_jobSkipReason = reason;
        emit uploadSkipped(reason);
        DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "Skipping upload for maintenance profile:" << beverageType;
        return false;
    }

    // Check credentials
    QString username = m_settings->value("visualizer/username", "").toString();
    QString password = m_settings->value("visualizer/password", "").toString();
    if (username.isEmpty() || password.isEmpty()) {
        m_lastUploadStatus = tr_("visualizer.upload.noCredentials", "No credentials configured");
        emit lastUploadStatusChanged();
        noteJobFailure(tr_("visualizer.upload.credentialsMissing", "Visualizer credentials not configured"));
        emit uploadFailed(tr_("visualizer.upload.credentialsMissing", "Visualizer credentials not configured"));
        return false;
    }

    if (ineligible == UploadIneligible::TooShort) {
        const QString reason = tr_("visualizer.skip.tooShort", "shot too short (%1s < %2s)").arg(duration, 0, 'f', 1).arg(minDuration, 0, 'f', 0);
        m_lastUploadStatus = tr_("visualizer.status.skipped", "Skipped: %1").arg(reason);
        emit lastUploadStatusChanged();
        // Policy skip, not an error — see uploadSkipped rationale on the
        // maintenance branch above. Emit just the reason payload.
        m_jobSkipReason = reason;
        emit uploadSkipped(reason);
        DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "Shot too short, not uploading";
        return false;
    }

    setUploading(true);
    m_lastUploadStatus = tr_("visualizer.status.uploading", "Uploading...");
    emit lastUploadStatusChanged();
    return true;
}

void VisualizerUploader::sendUpload(const QByteArray& jsonData)
{
    // Save JSON to file for debugging
    QString debugFile = uploadDebugFilePath(QStringLiteral("last_upload.json"));
    QFile file(debugFile);
    if (file.open(QIODevice::WriteOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(jsonData);
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
    } else {
        DIAG_WARN(VISUALIZER, "VisualizerUploader") << "Failed to save debug JSON to" << debugFile;
    }

    // Build multipart form data
    QString boundary = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QByteArray multipartData = buildMultipartData(jsonData, boundary);

    // Create request
    QUrl url(VISUALIZER_API_URL);
    QNetworkRequest request(url);

    request.setRawHeader("Authorization", authHeader().toUtf8());
    request.setRawHeader("Content-Type", QString("multipart/form-data; boundary=%1").arg(boundary).toUtf8());
    // Prevent Qt from following redirects (which can lose auth headers)
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    // Older builds left last_upload_debug.txt here holding base64 username:password.
    const QString staleAuthFile = uploadDebugFilePath(QStringLiteral("last_upload_debug.txt"));
    static bool staleAuthFileWarned = false;
    if (QFile::exists(staleAuthFile) && !QFile::remove(staleAuthFile) && !staleAuthFileWarned) {
        staleAuthFileWarned = true;
        DIAG_WARN(VISUALIZER, "VisualizerUploader") << "could not delete" << staleAuthFile;
    }

    // Send request
    QNetworkReply* reply = m_networkManager->post(request, multipartData);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onUploadFinished(reply);
    });

    DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "Uploading shot...";
}

// static
QByteArray VisualizerUploader::buildHistoryShotJson(const ShotProjection& shotData, bool includePortal,
                                                 const QJsonObject& machineState)
{
    QJsonObject root;
    if (includePortal && !shotData.portalSamples.isEmpty())
        root["decenza_portal_samples"] = QJsonArray::fromVariantList(shotData.portalSamples);
    root["version"] = 2;

    // Use original timestamp from the shot
    root["clock"] = shotData.timestamp;
    root["timestamp"] = shotData.timestamp;
    root["date"] = QDateTime::fromSecsSinceEpoch(shotData.timestamp).toString(Qt::ISODate);

    // Helper to convert QVariantList of {x,y} points to QVector<QPointF>
    auto toPointVector = [](const QVariantList& points) -> QVector<QPointF> {
        QVector<QPointF> result;
        result.reserve(points.size());
        for (const auto& pt : points) {
            QVariantMap p = pt.toMap();
            result.append(QPointF(p["x"].toDouble(), p["y"].toDouble()));
        }
        return result;
    };

    // Helper to extract just the values from a point vector
    auto extractValues = [](const QVector<QPointF>& points) -> QJsonArray {
        QJsonArray values;
        for (const auto& pt : points) {
            values.append(pt.y());
        }
        return values;
    };

    // Helper to extract elapsed times
    auto extractTimes = [](const QVector<QPointF>& points) -> QJsonArray {
        QJsonArray times;
        for (const auto& pt : points) {
            times.append(pt.x());
        }
        return times;
    };

    // Convert to point vectors for interpolation
    QVector<QPointF> pressureData = toPointVector(shotData.pressure);
    QVector<QPointF> flowData = toPointVector(shotData.flow);
    QVector<QPointF> tempData = toPointVector(shotData.temperature);
    QVector<QPointF> pressureGoalData = toPointVector(shotData.pressureGoal);
    QVector<QPointF> flowGoalData = toPointVector(shotData.flowGoal);
    QVector<QPointF> tempGoalData = toPointVector(shotData.temperatureGoal);
    QVector<QPointF> tempMixData = toPointVector(shotData.temperatureMix);
    QVector<QPointF> tempMixGoalData = toPointVector(shotData.temperatureMixGoal);
    QVector<QPointF> weightData = toPointVector(shotData.weight);
    QVector<QPointF> weightFlowRateData = toPointVector(shotData.weightFlowRate);
    QVector<QPointF> weightFlowRateRawData = toPointVector(shotData.weightFlowRateRaw);

    // Elapsed time array (from pressure data - the master timeline)
    root["elapsed"] = extractTimes(pressureData);

    // Pressure object
    QJsonObject pressure;
    pressure["pressure"] = extractValues(pressureData);
    pressure["goal"] = interpolateGoalData(pressureGoalData, pressureData);
    root["pressure"] = pressure;

    // Flow object
    QJsonObject flow;
    flow["flow"] = extractValues(flowData);
    flow["goal"] = interpolateGoalData(flowGoalData, pressureData);
    // Weight-based flow rate (g/s from scale)
    if (!weightFlowRateData.isEmpty()) {
        flow["by_weight"] = interpolateGoalData(weightFlowRateData, pressureData);
    }
    if (!weightFlowRateRawData.isEmpty())
        flow["by_weight_raw"] = interpolateGoalData(weightFlowRateRawData, pressureData);
    root["flow"] = flow;

    // Temperature object
    QJsonObject temperature;
    temperature["basket"] = extractValues(tempData);
    temperature["goal"] = interpolateGoalData(tempGoalData, pressureData);
    if (!tempMixData.isEmpty()) {
        temperature["mix"] = interpolateGoalData(tempMixData, pressureData);
    }
    // Omit rather than zero-fill when absent (shots saved before it was recorded).
    if (!tempMixGoalData.isEmpty()) {
        temperature["mix_goal"] = interpolateGoalData(tempMixGoalData, pressureData);
    }
    root["temperature"] = temperature;

    // Totals object
    QJsonObject totals;
    if (!weightData.isEmpty()) {
        totals["weight"] = interpolateGoalData(weightData, pressureData);
    }
    // Water dispensed: scale by 0.1 to match de1app's espresso_water_dispensed convention
    QVector<QPointF> waterDispensedData = toPointVector(shotData.waterDispensed);
    if (!waterDispensedData.isEmpty()) {
        QJsonArray waterDispensedRaw = interpolateGoalData(waterDispensedData, pressureData);
        QJsonArray waterDispensedScaled;
        for (const auto& v : waterDispensedRaw)
            waterDispensedScaled.append(v.toDouble() * 0.1);
        totals["water_dispensed"] = waterDispensedScaled;
    }
    root["totals"] = totals;

    // Resistance object: P/flow² (Darcy, matches de1app's espresso_resistance) and
    // P/flow_weight² (scale flow, de1app calls this espresso_resistance_weight → by_weight)
    QVector<QPointF> histWeightFlowData = toPointVector(shotData.weightFlowRate);
    {
        QVector<QPointF> histResData = toPointVector(shotData.darcyResistance);
        QJsonObject resistance;
        if (!histResData.isEmpty())
            resistance["resistance"] = interpolateGoalData(histResData, pressureData);
        if (!histWeightFlowData.isEmpty() && !pressureData.isEmpty()) {
            QJsonArray fwInterp = interpolateGoalData(histWeightFlowData, pressureData);
            QJsonArray resByWeight;
            for (qsizetype i = 0; i < pressureData.size(); ++i) {
                double fw = fwInterp[i].toDouble();
                double res = 0.0;
                if (fw > 0.05)
                    res = qMin(pressureData[i].y() / (fw * fw), 19.0);
                resByWeight.append(res);
            }
            resistance["by_weight"] = resByWeight;
        }
        if (!resistance.isEmpty())
            root["resistance"] = resistance;
    }

    // Scale object: raw weight series at native sample times. Only emit if there is scale data.
    if (!weightData.isEmpty() || !histWeightFlowData.isEmpty()) {
        QJsonObject scale;
        scale["espresso_start"] = static_cast<double>(shotData.timestamp);
        if (!weightData.isEmpty()) {
            QJsonArray weights, arrivals;
            for (const auto& pt : weightData) {
                arrivals.append(pt.x());
                weights.append(pt.y());
            }
            scale["weight_arrival"] = arrivals;
            scale["weight"] = weights;
        }
        if (!histWeightFlowData.isEmpty()) {
            QJsonArray flows;
            for (const auto& pt : histWeightFlowData)
                flows.append(pt.y());
            scale["weight_flow"] = flows;
        }
        root["scale"] = scale;
    }

    // State change array from history phase markers
    const QVariantList& phases = shotData.phases;
    if (!phases.isEmpty() && !pressureData.isEmpty()) {
        // Collect times of real frame transitions only (skip Start/End markers)
        QVector<double> markerTimes;
        for (const auto& p : phases) {
            QVariantMap pm = p.toMap();
            int frameNum = pm.value("frameNumber", -1).toInt();
            QString label = pm["label"].toString();
            if (frameNum >= 0 && label != "Start")
                markerTimes.append(pm["time"].toDouble());
        }
        QJsonArray stateChange;
        double stateVal = 10000000.0;
        qsizetype markerIdx = 0;
        for (const auto& pt : pressureData) {
            while (markerIdx < markerTimes.size() && pt.x() >= markerTimes[markerIdx]) {
                stateVal *= -1.0;
                markerIdx++;
            }
            stateChange.append(stateVal);
        }
        root["state_change"] = stateChange;
    }

    // Meta object
    QJsonObject meta;

    // Bean info
    QJsonObject bean;
    if (!shotData.beanBrand.isEmpty()) bean["brand"] = shotData.beanBrand;
    if (!shotData.beanType.isEmpty()) bean["type"] = shotData.beanType;
    if (!shotData.roastDate.isEmpty()) bean["roast_date"] = RoastDate::toIso(shotData.roastDate);
    if (!shotData.roastLevel.isEmpty()) bean["roast_level"] = shotData.roastLevel;
    meta["bean"] = bean;

    // Shot info
    QJsonObject shot;
    if (shotData.enjoyment0to100 > 0) shot["enjoyment"] = shotData.enjoyment0to100;
    if (!shotData.espressoNotes.isEmpty()) shot["notes"] = shotData.espressoNotes;
    if (shotData.drinkTdsPct > 0) shot["tds"] = shotData.drinkTdsPct;
    if (shotData.drinkEyPct > 0) shot["ey"] = shotData.drinkEyPct;
    meta["shot"] = shot;

    // Grinder info (combine brand+model for visualizer compatibility)
    QJsonObject grinder;
    QString grinderDisplay2 = grinderDisplayName(shotData.grinderBrand, shotData.grinderModel);
    if (!grinderDisplay2.isEmpty()) grinder["model"] = grinderDisplay2;
    { const QString gs = grinderSettingWithRpm(shotData.grinderSetting, shotData.rpm);
      if (!gs.isEmpty()) grinder["setting"] = gs; }
    meta["grinder"] = grinder;

    // Weights: use stored final weight from history; fall back to flow-integrated volume if missing
    double finalWeight = shotData.finalWeightG;
    if (finalWeight <= 0 && !waterDispensedData.isEmpty())
        finalWeight = waterDispensedData.last().y();  // actual ml (normalized at import)
    if (shotData.doseWeightG > 0) meta["in"] = shotData.doseWeightG;
    if (finalWeight > 0) meta["out"] = finalWeight;
    // The last sample's time.
    if (!pressureData.isEmpty()) meta["time"] = pressureData.last().x();

    root["meta"] = meta;

    // App info (with settings sub-object for Visualizer metadata extraction)
    QJsonObject app = buildAppInfoJson();

    QJsonObject settings;
    if (!shotData.beanBrand.isEmpty()) settings["bean_brand"] = shotData.beanBrand;
    if (!shotData.beanType.isEmpty()) settings["bean_type"] = shotData.beanType;
    if (!shotData.roastDate.isEmpty()) settings["roast_date"] = RoastDate::toIso(shotData.roastDate);
    if (!shotData.roastLevel.isEmpty()) settings["roast_level"] = shotData.roastLevel;
    if (!grinderDisplay2.isEmpty()) settings["grinder_model"] = grinderDisplay2;
    { const QString gs = grinderSettingWithRpm(shotData.grinderSetting, shotData.rpm);
      if (!gs.isEmpty()) settings["grinder_setting"] = gs; }
    if (shotData.doseWeightG > 0) settings["grinder_dose_weight"] = shotData.doseWeightG;
    if (finalWeight > 0) settings["drink_weight"] = finalWeight;
    if (shotData.drinkTdsPct > 0) settings["drink_tds"] = shotData.drinkTdsPct;
    if (shotData.drinkEyPct > 0) settings["drink_ey"] = shotData.drinkEyPct;
    if (shotData.enjoyment0to100 > 0) settings["espresso_enjoyment"] = shotData.enjoyment0to100;
    if (!shotData.espressoNotes.isEmpty()) settings["espresso_notes"] = shotData.espressoNotes;
    // Structured taste taps → CVA (add-ai-taste-intake), best-effort on the
    // initial .shot upload settings; the authoritative sync is the PATCH path.
    applyTasteCvaMapping(settings, shotData.tasteBalance, shotData.tasteBody);

    // de1app's key: Visualizer's parser reads the barista from my_name only
    // (Parsers::Base#build_shot); a `barista` key there is ignored.
    if (!shotData.barista.isEmpty()) settings["my_name"] = shotData.barista;

    // Parse profile JSON and merge profile fields for Visualizer TCL extraction
    QJsonObject profileJsonObj;
    if (!shotData.profileJson.isEmpty()) {
        QJsonDocument profileDoc = QJsonDocument::fromJson(shotData.profileJson.toUtf8());
        if (!profileDoc.isNull()) {
            // Upload the STORED snapshot verbatim. Do not re-serialize it through
            // Profile::fromJson/toJsonObject: fromJson is not a pure decoder — it
            // fills defaults for absent keys and rewrites espresso_temperature via
            // the leaked-default repair, so round-tripping would make a historical
            // shot claim values it never ran (Visualizer-imported profiles omit
            // espresso_temperature entirely). The snapshot is a record, not a
            // profile we own; new shots are already stored in canonical form.
            profileJsonObj = profileDoc.object();
            Profile profile = Profile::fromJson(profileDoc);
            if (profile.isValid()) {
                QJsonObject profileSettings = buildProfileSettings(&profile);
                for (auto it = profileSettings.begin(); it != profileSettings.end(); ++it)
                    settings[it.key()] = it.value();
            }
        }
    }

    // Also set profile_title from shot data (may differ from profile's own title)
    if (!shotData.profileName.isEmpty())
        settings["profile_title"] = shotData.profileName;

    QJsonObject data;
    data["settings"] = settings;
    if (!machineState.isEmpty())
        data["machine_state"] = machineState;
    if (!shotData.debugLog.isEmpty())
        data["debug_log"] = shotData.debugLog;
    app["data"] = data;

    root["app"] = app;

    // Profile JSON object for Visualizer's ?format=json download
    if (!profileJsonObj.isEmpty())
        root["profile"] = profileJsonObj;

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// ===================================================================
// Visualizer Coffee Management sync (bean-bag-inventory)
//
// Endpoint behaviors are spike-verified against the live API and the
// open-source controllers (openspec/changes/bean-bag-inventory/design.md,
// "spike findings"): bag/roaster CRUD is premium-gated (not CM-gated);
// `coffee_bag_id` on shot PATCH is only permitted when the user's
// coffee_management_enabled flag is on — a PATCH whose shot{} contains
// ONLY that key returns 400 when CM is off and 200 when on, which is the
// deterministic probe; the upload POST ignores coffee_bag_id, so linking
// is always a post-upload PATCH; linking rewrites the shot's bean fields
// server-side from the bag.
// ===================================================================

static const char* cmStateName(VisualizerUploader::CmState state)
{
    switch (state) {
    case VisualizerUploader::CmState::Unknown:            return "Unknown";
    case VisualizerUploader::CmState::Active:             return "Active";
    case VisualizerUploader::CmState::NoCoffeeManagement: return "NoCoffeeManagement";
    case VisualizerUploader::CmState::PremiumNoCm:        return "PremiumNoCm";
    }
    return "?";
}

void VisualizerUploader::setCmState(CmState state)
{
    if (m_cmState == state)
        return;
    DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "Visualizer CM: state" << cmStateName(m_cmState) << "->" << cmStateName(state);
    m_cmState = state;
}

QNetworkRequest VisualizerUploader::makeApiJsonRequest(const QString& path) const
{
    QNetworkRequest request{QUrl(QStringLiteral("https://visualizer.coffee") + path)};
    request.setRawHeader("Authorization", authHeader().toUtf8());
    // Rails derives request.format from Accept (NOT Content-Type) — without
    // this the shot PATCH 422s with "Request must be JSON".
    request.setRawHeader("Accept", "application/json");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(15000);
    return request;
}

void VisualizerUploader::syncCoffeeBagAfterUpload(qint64 dbShotId, const QString& visualizerShotId)
{
    if (dbShotId <= 0 || visualizerShotId.isEmpty() || m_localDbPath.isEmpty())
        return;
    // CM-off accounts have no bags to enrich. Cached per session (reset by
    // connectAccount) so toggling Coffee Management converges next upload.
    if (!bagEditPushAllowed(m_cmState))
        return;

    const QString dbPath = m_localDbPath;
    QPointer<VisualizerUploader> self(this);
    QThread* thread = QThread::create([self, dbPath, dbShotId, visualizerShotId]() {
        QVariantMap bagMap;
        withTempDb(dbPath, "viz_bagsync", [&](QSqlDatabase& db) {
            QSqlQuery query(db);
            query.prepare("SELECT bag_id FROM shots WHERE id = :id");
            query.bindValue(":id", dbShotId);
            if (!query.exec() || !query.next() || query.value(0).isNull())
                return;
            const CoffeeBag bag = CoffeeBagStorage::loadBagStatic(db, query.value(0).toLongLong());
            if (bag.isValid())
                bagMap = bag.toVariantMap();
        });
        // QPointer dereference only on the main thread (see loadShotWithMetadata note).
        QMetaObject::invokeMethod(qApp, [self, visualizerShotId, bagMap]() {
            if (self && !bagMap.isEmpty())
                self->reconcileShotBag(visualizerShotId, bagMap);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void VisualizerUploader::reconcileShotBag(const QString& visualizerShotId, const QVariantMap& bag)
{
    // Read back the bag the SERVER linked to this shot. visualizer.coffee's
    // upload parser find-or-creates the user's coffee_bag from bean_brand/
    // bean_type/roast_date and links it on EVERY upload (parsers/base.rb
    // set_coffee_bag; verified live against the API). So we never guess the id,
    // match on roast_date, or PATCH a shot with an unverified bag id —
    // shots.coffee_bag_id has a DB foreign key, so a dead id would 500. We read
    // the authoritative link straight back, then enrich the (server-created,
    // bare) bag with the descriptive origin fields the server never sets.
    QNetworkRequest request = makeApiJsonRequest(QStringLiteral("/api/shots/") + visualizerShotId);
    QNetworkReply* reply = m_networkManager->get(request);
    const qint64 localBagId = bag.value("id").toLongLong();
    connect(reply, &QNetworkReply::finished, this, [this, reply, visualizerShotId, bag, localBagId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "Visualizer CM: shot read-back failed - retry next upload";
            return;
        }
        QJsonParseError parseError;
        const QJsonObject shot = QJsonDocument::fromJson(reply->readAll(), &parseError).object();
        if (parseError.error != QJsonParseError::NoError) {
            DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "Visualizer CM: shot read-back unparseable - retry next upload";
            return;
        }
        const QString serverBagId = shot.value("coffee_bag_id").toString();
        const QString serverRoasterId = shot.value("roaster_id").toString();

        if (serverBagId.isEmpty()) {
            // No bag linked though we sent bean tags — usually Coffee Management is
            // off. But an empty coffee_bag_id is NOT a definitive signal (a server
            // that returned the shot before its bag link was visible looks the
            // same), so we must not cache a session-long negative or wipe the
            // stored id off it. Leave the state Unknown and retry next upload; the
            // per-upload read-back is cheap and self-corrects. (The only negative
            // we cache is the definitive 403 in enrichRemoteBag.)
            //
            // The canonical link is NOT Coffee-Management-gated, so a known coffee
            // still attaches in canonical-only mode: PATCH the shot's canonical
            // when our bag carries one. With no coffee_bag on the shot the server
            // keeps it (its refresh_coffee_bag_fields only overrides canonical when
            // a bag is linked). Idempotent, so re-PATCHing each upload is harmless.
            //
            // Same identity guard as the metadata PATCH: a canonical record
            // that names a different coffee would rename the shot server-side.
            const QString canonicalId = bag.value("beanBaseId").toString();
            const bool conflicts = BeanBaseBlob::canonicalIdentityConflicts(
                bag.value("beanBaseData").toString(),
                {bag.value("roasterName").toString(), bag.value("coffeeName").toString()});
            if (!canonicalId.isEmpty() && !conflicts)
                linkShotCanonical(visualizerShotId, canonicalId);
            DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "Visualizer CM: shot has no server bag -"
                     << (canonicalId.isEmpty() ? "nothing to link"
                         : conflicts ? "canonical link withheld (record names another coffee)"
                                     : "linking canonical coffee");
            return;
        }

        // A linked bag means CM is active. Capture the authoritative ids — the
        // server assigns them, Decenza often never had them (bags born entirely
        // server-side), and they change when a deleted bag is recreated. This is
        // also the self-heal: a stale local id is simply overwritten with the
        // server's current one, so a bag deleted on visualizer.coffee converges
        // the moment its replacement is auto-created on the next upload.
        setCmState(CmState::Active);
        if (bag.value("visualizerBagId").toString() != serverBagId)
            persistBagSyncIds(localBagId, serverBagId, serverRoasterId);
        enrichRemoteBag(serverBagId, bag);
        // CM just (re)confirmed Active: drain bag edits whose push failed retryably.
        retrySyncPendingBags();

        // Verified-roaster badge: the server creates the roaster bare, so link it
        // to its canonical when we have one (best-effort; the badge is cosmetic).
        const QString canonicalRoasterId = QJsonDocument::fromJson(
            bag.value("beanBaseData").toString().toUtf8())
                .object().value("canonicalRoasterId").toString();
        if (!serverRoasterId.isEmpty() && !canonicalRoasterId.isEmpty())
            enrichRemoteRoaster(serverRoasterId, canonicalRoasterId);
    });
}

void VisualizerUploader::linkShotCanonical(const QString& visualizerShotId, const QString& canonicalId)
{
    // PATCH the shot's canonical_coffee_bag_id (permitted regardless of Coffee
    // Management). The DYE-metadata PATCH (updateShotOnVisualizer) also carries
    // the canonical, but only goes out when the shot is edited — so this
    // guarantees a known coffee links even on a shot nobody edits. Same value as
    // that path, so a double-send is idempotent.
    QJsonObject shotObj{{QStringLiteral("canonical_coffee_bag_id"), canonicalId}};
    QJsonObject root{{QStringLiteral("shot"), shotObj}};
    QNetworkRequest request = makeApiJsonRequest(QStringLiteral("/api/shots/") + visualizerShotId);
    QNetworkReply* reply = m_networkManager->sendCustomRequest(
        request, "PATCH", QJsonDocument(root).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, visualizerShotId, canonicalId]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200) {
            DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "Visualizer CM: linked shot" << visualizerShotId << "to canonical" << canonicalId;
        } else {
            // status is 0 on a transport error (no HTTP response) — surface the
            // network error string then, matching the sibling read-back handlers.
            DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "Visualizer CM: shot canonical link failed (HTTP" << status
                     << reply->errorString() << ") - retry next upload";
        }
    });
}

// static
QJsonObject VisualizerUploader::buildBagEnrichBody(const QJsonObject& remoteBag, const QVariantMap& bag)
{
    // The PATCH body of descriptive fields to fill on the server bag: only fields
    // we hold locally AND the server left blank (null/missing/whitespace). The
    // server-managed name/roast_date/roast_level are deliberately excluded. Pure
    // (no I/O) so the fill-blanks contract and the blob→API field mapping are
    // unit-tested directly (tst_coffeebags).
    QJsonObject body;
    auto fillBlank = [&](const char* apiKey, const QString& localValue) {
        if (localValue.isEmpty())
            return;
        const QJsonValue rv = remoteBag.value(QLatin1String(apiKey));
        const bool blank = rv.isNull() || rv.isUndefined()
                           || (rv.isString() && rv.toString().trimmed().isEmpty());
        if (blank)
            body[QLatin1String(apiKey)] = localValue;
    };
    const QMap<QString, QString> local = VisualizerSync::bagLocalValues(bag);
    for (auto it = local.cbegin(); it != local.cend(); ++it) {
        if (it.key() == QLatin1StringView("name") || it.key() == QLatin1StringView("roast_date")
            || it.key() == QLatin1StringView("roast_level"))
            continue;
        fillBlank(it.key().toLatin1().constData(),
                  it.key() == QLatin1StringView("notes") ? VisualizerNotes::plainToHtml(it.value()) : it.value());
    }
    return body;
}

void VisualizerUploader::enrichRemoteBag(const QString& serverBagId, const QVariantMap& bag)
{
    QNetworkRequest request = makeApiJsonRequest(QStringLiteral("/api/coffee_bags/") + serverBagId);
    QNetworkReply* reply = m_networkManager->get(request);
    const qint64 localBagId = bag.value("id").toLongLong();
    connect(reply, &QNetworkReply::finished, this, [this, reply, bag, serverBagId, localBagId]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 404) {
            // Raced a deletion since the shot read-back. Drop the stale id; the
            // next upload recreates+relinks the bag server-side.
            persistBagSyncIds(localBagId, QString(), QString());
            DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "Visualizer CM: bag" << serverBagId << "gone before enrich - cleared local id";
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "Visualizer CM: bag read for enrich failed (HTTP" << status
                     << ") - retry next upload";
            return;
        }
        QJsonParseError parseError;
        const QJsonObject remote = QJsonDocument::fromJson(reply->readAll(), &parseError).object();
        if (parseError.error != QJsonParseError::NoError) {
            // A 200 with an unparseable body would read as "every field blank" and
            // trigger a full-overwrite PATCH, clobbering the user's server values.
            DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "Visualizer CM: bag" << serverBagId << "enrich GET unparseable - retry next upload";
            return;
        }
        // Fill only the fields the server left blank — never clobber a value the
        // user set on visualizer.coffee, and forward-compatible by construction:
        // if the server is later fixed to seed descriptive fields from the
        // canonical bean record, this sees them already populated and skips them
        // (empty body → no PATCH), so the server's values always win. We re-read
        // every upload, so no version check is needed.
        const QJsonObject body = buildBagEnrichBody(remote, bag);
        if (body.isEmpty()) {
            DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "Visualizer CM: bag" << serverBagId << "already complete - nothing to enrich";
            return;
        }
        QNetworkRequest patch = makeApiJsonRequest(QStringLiteral("/api/coffee_bags/") + serverBagId);
        QNetworkReply* preply = m_networkManager->sendCustomRequest(
            patch, "PATCH", QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(preply, &QNetworkReply::finished, this, [this, preply, serverBagId, localBagId]() {
            preply->deleteLater();
            const int st = preply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (st == 200) {
                DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "Visualizer CM: enriched bag" << serverBagId << "with descriptive fields";
            } else if (st == 403) {
                setCmState(CmState::NoCoffeeManagement);
                DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "Visualizer CM: bag enrich 403 - account is not premium";
            } else if (st == 404) {
                persistBagSyncIds(localBagId, QString(), QString());
                DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "Visualizer CM: bag" << serverBagId << "gone during enrich - cleared local id";
            } else {
                DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "Visualizer CM: bag enrich failed (HTTP" << st << ") - retry next upload";
            }
        });
    });
}

void VisualizerUploader::enrichRemoteRoaster(const QString& roasterId, const QString& canonicalRoasterId)
{
    QNetworkRequest request = makeApiJsonRequest(QStringLiteral("/api/roasters/") + roasterId);
    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, roasterId, canonicalRoasterId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;  // best-effort: the verified-roaster badge is cosmetic
        QJsonParseError parseError;
        const QJsonObject roaster = QJsonDocument::fromJson(reply->readAll(), &parseError).object();
        if (parseError.error != QJsonParseError::NoError)
            return;
        // Only set it when blank — never repoint a roaster the user/server
        // already linked elsewhere. An unparseable body bails above rather than
        // reading the field as null and force-linking.
        if (!roaster.value("canonical_roaster_id").isNull())
            return;
        QJsonObject body{{QStringLiteral("canonical_roaster_id"), canonicalRoasterId}};
        QNetworkRequest patch = makeApiJsonRequest(QStringLiteral("/api/roasters/") + roasterId);
        QNetworkReply* preply = m_networkManager->sendCustomRequest(
            patch, "PATCH", QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(preply, &QNetworkReply::finished, this, [preply, roasterId]() {
            preply->deleteLater();
            const int st = preply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "Visualizer CM: roaster" << roasterId << "canonical link PATCH HTTP" << st;
        });
    });
}

void VisualizerUploader::resolveRoasterId(const QString& roasterName, const QString& canonicalRoasterId,
                                          std::function<void(const QString&)> onResolved)
{
    // Creating is for an account known to use Coffee Management: while that is
    // unconfirmed (Unknown), an unmatched name resolves to "" (roaster left as
    // is) rather than adding a roaster to a list that may be dormant.
    const bool mayCreate = m_cmState == CmState::Active;
    QNetworkRequest request = makeApiJsonRequest(QStringLiteral("/api/roasters?items=100"));
    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, roasterName, canonicalRoasterId, mayCreate, onResolved = std::move(onResolved)]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "Visualizer CM: roaster list failed - retry next time";
            return;
        }
        const QJsonArray data = QJsonDocument::fromJson(reply->readAll())
                                    .object().value("data").toArray();
        for (const QJsonValue& value : data) {
            const QJsonObject roaster = value.toObject();
            if (roaster.value("name").toString().compare(roasterName, Qt::CaseInsensitive) == 0) {
                onResolved(roaster.value("id").toString());
                return;
            }
        }

        if (!mayCreate) {
            onResolved(QString());
            return;
        }
        // Create the roaster; carry the canonical roaster UUID when present
        // (verified-badge linking on visualizer.coffee).
        QJsonObject body;
        body["name"] = roasterName;
        if (!canonicalRoasterId.isEmpty())
            body["canonical_roaster_id"] = canonicalRoasterId;

        QNetworkRequest createRequest = makeApiJsonRequest(QStringLiteral("/api/roasters"));
        QNetworkReply* createReply = m_networkManager->post(
            createRequest, QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(createReply, &QNetworkReply::finished, this,
                [this, createReply, onResolved]() {
            createReply->deleteLater();
            const int status = createReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 201) {
                onResolved(QJsonDocument::fromJson(createReply->readAll())
                               .object().value("id").toString());
            } else if (status == 403) {
                // Bag/roaster CRUD is premium-gated: a 403 means not premium.
                setCmState(CmState::NoCoffeeManagement);
                DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "Visualizer CM: roaster create 403 - account is not premium";
            } else {
                DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "Visualizer CM: roaster create failed (HTTP" << status << ")";
            }
        });
    });
}

void VisualizerUploader::persistBagSyncIds(qint64 localBagId, const QString& visualizerBagId,
                                           const QString& visualizerRoasterId)
{
    if (localBagId <= 0 || m_localDbPath.isEmpty())
        return;
    const QString dbPath = m_localDbPath;
    QThread* thread = QThread::create([dbPath, localBagId, visualizerBagId, visualizerRoasterId]() {
        withTempDb(dbPath, "viz_bagids", [&](QSqlDatabase& db) {
            QVariantMap fields{{QStringLiteral("visualizerBagId"), visualizerBagId}};
            if (!visualizerRoasterId.isEmpty())
                fields.insert(QStringLiteral("visualizerRoasterId"), visualizerRoasterId);
            if (!CoffeeBagStorage::updateBagFieldsStatic(db, localBagId, fields))
                DIAG_WARN(VISUALIZER, "VisualizerUploader") << "Visualizer CM: failed to persist sync ids for bag" << localBagId;
        });
    });
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void VisualizerUploader::updateBagOnVisualizer(qint64 localBagId)
{
    if (localBagId <= 0 || m_localDbPath.isEmpty())
        return;
    if (!bagEditPushAllowed(m_cmState))
        return;

    // Park FIRST, un-park on outcome. Any failure between here and
    // patchRemoteBag's reply handler — bag load failure, the roaster-list GET
    // dying offline, a roaster create dropped — leaves the flag set and the
    // edit is re-pushed after the next upload or sync pass instead of lost
    // (the transport failure that motivates the retry hits the roaster GET
    // first, one hop before the PATCH). patchRemoteBag clears it on every
    // reply it actually receives (200/403/404/422); only retryable outcomes
    // leave it set.
    persistBagSyncPending(localBagId, true);

    const QString dbPath = m_localDbPath;
    QPointer<VisualizerUploader> self(this);
    QThread* thread = QThread::create([self, dbPath, localBagId]() {
        QVariantMap bagMap;
        withTempDb(dbPath, "viz_bagupd", [&](QSqlDatabase& db) {
            const CoffeeBag bag = CoffeeBagStorage::loadBagStatic(db, localBagId);
            if (bag.isValid())
                bagMap = bag.toVariantMap();
        });
        // QPointer dereference only on the main thread.
        QMetaObject::invokeMethod(qApp, [self, bagMap, localBagId]() {
            if (!self)
                return;
            if (bagMap.isEmpty()) {
                // Deleted bag (row gone, flag moot) or a transient load
                // failure (flag stays set, retried next cycle). Log so the
                // retry drain's count is explainable.
                DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "Visualizer CM: bag" << localBagId << "load failed or deleted - push skipped";
                return;
            }
            // Not synced yet → nothing to PATCH, and pending is moot: the
            // next shot upload's server-side find-or-create carries the
            // CURRENT local fields anyway.
            if (bagMap.value("visualizerBagId").toString().isEmpty()) {
                self->persistBagSyncPending(localBagId, false);
                DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "Visualizer CM: bag" << localBagId << "not synced yet - upload-time create covers it";
                return;
            }
            const QString roasterName = bagMap.value("roasterName").toString().trimmed();
            if (roasterName.isEmpty()) {
                // No roaster to (re)resolve — PATCH descriptive fields only,
                // leaving the remote roaster_id untouched.
                self->patchRemoteBag(bagMap, QString());
                return;
            }
            const QString canonicalRoasterId = QJsonDocument::fromJson(
                bagMap.value("beanBaseData").toString().toUtf8())
                    .object().value("canonicalRoasterId").toString();
            // Re-resolve so a roaster rename re-points roaster_id; patchRemoteBag
            // writes roaster_id only when it actually changed.
            self->resolveRoasterId(roasterName, canonicalRoasterId,
                                   [self, bagMap](const QString& roasterId) {
                if (self)
                    self->patchRemoteBag(bagMap, roasterId);
            });
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void VisualizerUploader::patchRemoteBag(const QVariantMap& bag, const QString& roasterId)
{
    const QString bagUuid = bag.value("visualizerBagId").toString();
    if (bagUuid.isEmpty())
        return;

    QVariantMap sent;
    QJsonObject body = VisualizerSync::bagPushBody(bag, &sent);
    QJsonValue archivedAt;
    if (VisualizerSync::bagArchiveForPush(bag, QDateTime::currentDateTimeUtc(), &archivedAt)) {
        body["archived_at"] = archivedAt;
        sent.insert(QStringLiteral("archived_at"), archivedAt.toString());
    }
    const QString storedRoasterId = bag.value("visualizerRoasterId").toString();
    const bool roasterChanged = !roasterId.isEmpty() && roasterId != storedRoasterId;
    if (roasterChanged)
        body["roaster_id"] = roasterId;

    const qint64 localBagId = bag.value("id").toLongLong();
    if (body.isEmpty()) {
        // Nothing changed here since Visualizer was last seen.
        persistBagSyncPending(localBagId, false);
        return;
    }
    const QString bagDisplayName = QStringList{bag.value("roasterName").toString(),
                                               bag.value("coffeeName").toString()}
                                       .join(QLatin1Char(' ')).trimmed();
    ++m_bagPushGeneration[localBagId];
    QNetworkRequest request = makeApiJsonRequest(QStringLiteral("/api/coffee_bags/") + bagUuid);
    QNetworkReply* reply = m_networkManager->sendCustomRequest(
        request, "PATCH", QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, bagUuid, roasterId, roasterChanged, localBagId, bagDisplayName, sent]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200) {
            DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "Visualizer CM: updated coffee bag" << bagUuid;
            persistBagSyncPending(localBagId, false);
            // The archive as the server stored it, so the next list read matches.
            QVariantMap accepted = sent;
            if (accepted.contains(QStringLiteral("archived_at")))
                accepted.insert(QStringLiteral("archived_at"),
                                QJsonDocument::fromJson(reply->readAll()).object().value("archived_at").toString());
            persistBagSeen(localBagId, accepted);
            // A roaster rename moved the bag to a different roaster_id — persist
            // it so the next update diffs against the new value.
            if (roasterChanged)
                persistBagSyncIds(localBagId, bagUuid, roasterId);
        } else if (status == 403) {
            // Bag CRUD is premium-gated: a 403 means not premium. Definitive —
            // clear the pending flag so it doesn't retry forever.
            setCmState(CmState::NoCoffeeManagement);
            persistBagSyncPending(localBagId, false);
            DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "Visualizer CM: bag update 403 - account is not premium";
        } else if (status == 404) {
            // The remote bag was deleted on visualizer.coffee; our id is stale.
            // Leave it — the next shot upload re-creates and re-links the bag
            // (carrying the current local fields), so pending is moot.
            persistBagSyncPending(localBagId, false);
            DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "Visualizer CM: bag update 404 - remote bag" << bagUuid << "gone";
        } else if (status == 422) {
            // The server rejected the values (name+roast_date uniqueness,
            // defrost-before-frozen). Retrying the same body cannot succeed —
            // keep the local edit, surface the server's message once. The
            // toast names the bag: a 422 can fire from the retry drain hours
            // after the edit, when "the bag update" identifies nothing.
            persistBagSyncPending(localBagId, false);
            const QJsonObject err = QJsonDocument::fromJson(reply->readAll()).object();
            QString message = err.value(QStringLiteral("error")).toString();
            if (message.isEmpty())
                message = tr_("visualizer.bag.rejected", "Visualizer rejected the bag update");
            emit bagPushRejected(localBagId, bagDisplayName, message);
            DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "Visualizer CM: bag update 422 for bag" << localBagId
                     << "(" << bagDisplayName << ") -" << message;
        } else {
            // Transport error (status 0), 429, or 5xx: retryable. Park the bag
            // as sync-pending; the next upload or sync pass re-pushes it.
            persistBagSyncPending(localBagId, true);
            DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "Visualizer CM: bag update failed (HTTP" << status << ") - queued for retry";
        }
    });
}

void VisualizerUploader::persistBagSyncPending(qint64 localBagId, bool pending)
{
    if (localBagId <= 0 || m_localDbPath.isEmpty())
        return;
    const QString dbPath = m_localDbPath;
    QThread* thread = QThread::create([dbPath, localBagId, pending]() {
        withTempDb(dbPath, "viz_bagpend", [&](QSqlDatabase& db) {
            if (!CoffeeBagStorage::updateBagFieldsStatic(
                    db, localBagId, {{QStringLiteral("visualizerSyncPending"), pending}}))
                DIAG_WARN(VISUALIZER, "VisualizerUploader") << "Visualizer CM: failed to persist sync-pending for bag" << localBagId;
        });
    });
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void VisualizerUploader::persistBagSeen(qint64 localBagId, const QVariantMap& sent)
{
    if (localBagId <= 0 || m_localDbPath.isEmpty() || sent.isEmpty())
        return;
    const QString dbPath = m_localDbPath;
    QThread* thread = QThread::create([dbPath, localBagId, sent]() {
        withTempDb(dbPath, "viz_bagseen", [&](QSqlDatabase& db) {
            // A failed write leaves these fields to be sent again on the next edit.
            if (!CoffeeBagStorage::mergeVisualizerSeenStatic(db, localBagId, sent))
                DIAG_WARN(VISUALIZER, "VisualizerUploader") << "Visualizer CM: failed to record pushed fields for bag" << localBagId;
        });
    });
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void VisualizerUploader::retrySyncPendingBags()
{
    // Re-push bags whose edit-time push never completed (any failure after the
    // park-first set). Called from the upload read-back and at the end of each
    // VisualizerShotSync pass.
    // Each re-push runs the full park-first cycle, so this self-drains on
    // success/definitive outcomes and re-parks on repeat failure.
    if (m_localDbPath.isEmpty() || !bagEditPushAllowed(m_cmState))
        return;
    const QString dbPath = m_localDbPath;
    QPointer<VisualizerUploader> self(this);
    QThread* thread = QThread::create([self, dbPath]() {
        QVector<qint64> pendingIds;
        withTempDb(dbPath, "viz_bagretry", [&](QSqlDatabase& db) {
            QSqlQuery query(db);
            if (!query.exec("SELECT id FROM coffee_bags WHERE visualizer_sync_pending = 1")) {
                // This is the ONLY drain trigger — a silent skip here would
                // make "my edit never reached Visualizer" undebuggable.
                DIAG_WARN(VISUALIZER, "VisualizerUploader") << "Visualizer CM: sync-pending query failed:" << query.lastError().text();
                return;
            }
            while (query.next())
                pendingIds << query.value(0).toLongLong();
        });
        QMetaObject::invokeMethod(qApp, [self, pendingIds]() {
            if (!self || pendingIds.isEmpty())
                return;
            DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << "Visualizer CM: re-pushing" << pendingIds.size() << "sync-pending bag(s)";
            for (qint64 bagId : pendingIds)
                self->updateBagOnVisualizer(bagId);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}
