// visualizer.coffee REST client.
//
// Authoritative API reference:   https://apidocs.visualizer.coffee
//   Spec source:                 OpenAPI 3.1 (current version 1.8.2).
//   Public endpoints used:       POST /api/shots/upload       (initial upload)
//                                PATCH /api/shots/{id}        (update metadata)
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
//     value == "set". `updateShotOnVisualizer` emits every editable
//     field explicitly (null for unset) so the cloud copy mirrors
//     local state — including local clears. Issue #1150 / migration 16.

#include "visualizeruploader.h"
#include "beanbase_blob.h"
#include "roastdate.h"
#include "tastecvamap.h"
#include "visualizershotlist.h"
#include "../core/translationmanager.h"
#include "../history/coffeebagstorage.h"
#include "../core/dbutils.h"
#include "../models/shotdatamodel.h"
#include "../core/settings.h"
#include "../core/settings_visualizer.h"
#include "../profile/profile.h"
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
#include <QDebug>
#include <QUuid>
#include <QStandardPaths>
#include <QFile>
#include <QDir>
#include <QBuffer>

namespace {
// Visualizer has no rpm field, so the grinder rpm dial-in is appended to the
// grind setting using the community convention ("2.4 1400rpm") that the grinder
// parser already tolerates (add-equipment-packages).
QString grinderSettingWithRpm(const QString& setting, qint64 rpm) {
    if (rpm <= 0)
        return setting;
    return setting.isEmpty() ? QStringLiteral("%1rpm").arg(rpm)
                             : QStringLiteral("%1 %2rpm").arg(setting).arg(rpm);
}
} // namespace

VisualizerUploader::VisualizerUploader(QNetworkAccessManager* networkManager, Settings* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_networkManager(networkManager)
{
    Q_ASSERT(networkManager);
}

QString VisualizerUploader::tr_(const char* key, const char* fallback) const {
    if (m_translationManager)
        return m_translationManager->translateString(QString::fromUtf8(key),
                                               QString::fromUtf8(fallback));
    return QString::fromUtf8(fallback);
}

// Helper: Interpolate goal data to match elapsed timestamps
// Goal data may have different timestamps or gaps; we need to align to the master elapsed array
// Gaps > 0.5s between goal points indicate mode switches (flow/pressure) - return 0 during gaps
static QJsonArray interpolateGoalData(const QVector<QPointF>& goalData, const QVector<QPointF>& masterData) {
    QJsonArray result;

    if (goalData.isEmpty() || masterData.isEmpty()) {
        // Return zeros for all timestamps if no goal data
        for (qsizetype i = 0; i < masterData.size(); ++i) {
            result.append(0.0);
        }
        return result;
    }

    // Gap threshold: if consecutive goal points are more than 0.5s apart, treat as a gap
    constexpr double GAP_THRESHOLD = 0.5;

    qsizetype goalIdx = 0;
    for (const auto& masterPt : masterData) {
        double t = masterPt.x();

        // Find the goal data points surrounding this timestamp
        while (goalIdx < goalData.size() - 1 && goalData[goalIdx + 1].x() <= t) {
            goalIdx++;
        }

        if (goalIdx == 0 && t < goalData[0].x()) {
            // Before first goal point - use 0
            result.append(0.0);
        } else if (goalIdx >= goalData.size() - 1) {
            // At or past last point
            double timeSinceLast = t - goalData.last().x();
            if (timeSinceLast > GAP_THRESHOLD) {
                // Far past the last goal point - probably in a different mode
                result.append(0.0);
            } else {
                result.append(goalData.last().y());
            }
        } else {
            // Between goalData[goalIdx] and goalData[goalIdx+1]
            double t0 = goalData[goalIdx].x();
            double t1 = goalData[goalIdx + 1].x();
            double v0 = goalData[goalIdx].y();
            double v1 = goalData[goalIdx + 1].y();

            // Check for gap between goal points
            if (t1 - t0 > GAP_THRESHOLD) {
                // Gap detected - check which side of the gap we're on
                if (t - t0 < GAP_THRESHOLD) {
                    // Close to the earlier point - use its value
                    result.append(v0);
                } else if (t1 - t < GAP_THRESHOLD) {
                    // Close to the later point - use its value
                    result.append(v1);
                } else {
                    // In the middle of the gap - return 0
                    result.append(0.0);
                }
            } else if (t1 - t0 > 0.001) {
                // Normal case - interpolate
                double ratio = (t - t0) / (t1 - t0);
                result.append(v0 + ratio * (v1 - v0));
            } else {
                result.append(v0);
            }
        }
    }

    return result;
}

void VisualizerUploader::uploadShot(ShotDataModel* shotData,
                                     const Profile* profile,
                                     double duration,
                                     double finalWeight,
                                     double doseWeight,
                                     const ShotMetadata& metadata,
                                     const QString& debugLog,
                                     qint64 shotEpoch,
                                     qint64 dbShotId)
{
    if (!shotData) {
        emit uploadFailed(tr_("visualizer.upload.noShotData", "No shot data available"));
        return;
    }

    QString beverageType = profile ? profile->beverageType() : QString();
    if (!validateUpload(beverageType, duration))
        return;

    m_uploadingDbShotId = dbShotId;
    m_uploadRetries = 0;
    QByteArray jsonData = buildShotJson(shotData, profile, finalWeight, doseWeight, metadata, debugLog, shotEpoch);
    sendUpload(jsonData);
}

void VisualizerUploader::uploadShotFromHistory(const ShotProjection& shotData)
{
    if (!shotData.isValid()) {
        emit uploadFailed(tr_("visualizer.upload.noShotData", "No shot data available"));
        return;
    }

    // Extract beverage type from profile JSON
    QString beverageType;
    if (!shotData.profileJson.isEmpty()) {
        QJsonDocument profileDoc = QJsonDocument::fromJson(shotData.profileJson.toUtf8());
        if (!profileDoc.isNull()) {
            beverageType = profileDoc.object()["beverage_type"].toString();
        }
    }

    if (!validateUpload(beverageType, shotData.durationSec))
        return;

    m_uploadingDbShotId = shotData.id;
    m_uploadRetries = 0;
    QByteArray jsonData = buildHistoryShotJson(shotData);
    sendUpload(jsonData);
}

void VisualizerUploader::uploadShotFromHistoryWithOverrides(
    const QVariant& baseShot, const QVariantMap& overrides)
{
    ShotProjection shot = ShotProjection::coerce(baseShot);
    if (!shot.isValid()) {
        emit uploadFailed(tr_("visualizer.upload.noShotData", "No shot data available"));
        return;
    }
    auto applyStr    = [&](QString       ShotProjection::*f, const char* k) {
        auto it = overrides.find(QLatin1String(k));
        if (it != overrides.end()) shot.*f = it->toString();
    };
    auto applyDouble = [&](double        ShotProjection::*f, const char* k) {
        auto it = overrides.find(QLatin1String(k));
        if (it != overrides.end()) shot.*f = it->toDouble();
    };
    auto applyInt    = [&](int           ShotProjection::*f, const char* k) {
        auto it = overrides.find(QLatin1String(k));
        if (it != overrides.end()) shot.*f = it->toInt();
    };
    auto applyI64    = [&](qint64        ShotProjection::*f, const char* k) {
        auto it = overrides.find(QLatin1String(k));
        if (it != overrides.end()) shot.*f = it->toLongLong();
    };

    applyStr   (&ShotProjection::profileName,     "profileName");
    applyStr   (&ShotProjection::beanBrand,       "beanBrand");
    applyStr   (&ShotProjection::beanType,        "beanType");
    applyStr   (&ShotProjection::roastDate,       "roastDate");
    applyStr   (&ShotProjection::roastLevel,      "roastLevel");
    applyStr   (&ShotProjection::grinderBrand,    "grinderBrand");
    applyStr   (&ShotProjection::grinderModel,    "grinderModel");
    applyStr   (&ShotProjection::grinderSetting,  "grinderSetting");
    applyI64   (&ShotProjection::rpm,             "rpm");
    applyStr   (&ShotProjection::barista,         "barista");
    applyStr   (&ShotProjection::espressoNotes,   "espressoNotes");
    // grinderBurrs and beverageType: no PATCH/POST JSON fields for them; callers
    // intentionally omit them from overrides so the applyStr lines would be no-ops.
    applyDouble(&ShotProjection::doseWeightG,     "doseWeightG");
    applyDouble(&ShotProjection::finalWeightG,    "finalWeightG");
    applyDouble(&ShotProjection::drinkTdsPct,     "drinkTdsPct");
    applyDouble(&ShotProjection::drinkEyPct,      "drinkEyPct");
    applyInt   (&ShotProjection::enjoyment0to100, "enjoyment0to100");
    applyStr   (&ShotProjection::tasteBalance,    "tasteBalance");
    applyStr   (&ShotProjection::tasteBody,       "tasteBody");

    uploadShotFromHistory(shot);
}

void VisualizerUploader::updateShotOnVisualizerWithOverrides(
    const QString& visualizerId,
    const QVariant& baseShot,
    const QVariantMap& overrides)
{
    ShotProjection shot = ShotProjection::coerce(baseShot);
    // updateShotOnVisualizer below only guards on visualizerId, not validity, so
    // catch a coerce miss here — otherwise a PATCH could go out with id=0 /
    // durationSec=0 / empty curves.
    if (!shot.isValid()) {
        emit uploadFailed(tr_("visualizer.upload.noShotData", "No shot data available"));
        emit updateFailed(visualizerId, false, "No shot data available");
        return;
    }
    auto applyStr    = [&](QString       ShotProjection::*f, const char* k) {
        auto it = overrides.find(QLatin1String(k));
        if (it != overrides.end()) shot.*f = it->toString();
    };
    auto applyDouble = [&](double        ShotProjection::*f, const char* k) {
        auto it = overrides.find(QLatin1String(k));
        if (it != overrides.end()) shot.*f = it->toDouble();
    };
    auto applyInt    = [&](int           ShotProjection::*f, const char* k) {
        auto it = overrides.find(QLatin1String(k));
        if (it != overrides.end()) shot.*f = it->toInt();
    };
    auto applyI64    = [&](qint64        ShotProjection::*f, const char* k) {
        auto it = overrides.find(QLatin1String(k));
        if (it != overrides.end()) shot.*f = it->toLongLong();
    };

    applyStr   (&ShotProjection::profileName,     "profileName");
    applyStr   (&ShotProjection::beanBrand,       "beanBrand");
    applyStr   (&ShotProjection::beanType,        "beanType");
    applyStr   (&ShotProjection::roastDate,       "roastDate");
    applyStr   (&ShotProjection::roastLevel,      "roastLevel");
    applyStr   (&ShotProjection::grinderBrand,    "grinderBrand");
    applyStr   (&ShotProjection::grinderModel,    "grinderModel");
    applyStr   (&ShotProjection::grinderSetting,  "grinderSetting");
    applyI64   (&ShotProjection::rpm,             "rpm");
    applyStr   (&ShotProjection::barista,         "barista");
    applyStr   (&ShotProjection::espressoNotes,   "espressoNotes");
    // grinderBurrs and beverageType: no PATCH/POST JSON fields for them; callers
    // intentionally omit them from overrides so the applyStr lines would be no-ops.
    applyDouble(&ShotProjection::doseWeightG,     "doseWeightG");
    applyDouble(&ShotProjection::finalWeightG,    "finalWeightG");
    applyDouble(&ShotProjection::drinkTdsPct,     "drinkTdsPct");
    applyDouble(&ShotProjection::drinkEyPct,      "drinkEyPct");
    applyInt   (&ShotProjection::enjoyment0to100, "enjoyment0to100");
    applyStr   (&ShotProjection::tasteBalance,    "tasteBalance");
    applyStr   (&ShotProjection::tasteBody,       "tasteBody");

    updateShotOnVisualizer(visualizerId, shot);
}

void VisualizerUploader::updateShotOnVisualizer(const QString& visualizerId, const ShotProjection& shotData)
{
    if (visualizerId.isEmpty()) {
        emit uploadFailed(tr_("visualizer.error.noVizId", "No visualizer ID for update"));
        emit updateFailed(visualizerId, false, "No visualizer ID for update");
        return;
    }

    // Check credentials
    QString username = m_settings->value("visualizer/username", "").toString();
    QString password = m_settings->value("visualizer/password", "").toString();

    if (username.isEmpty() || password.isEmpty()) {
        m_lastUploadStatus = tr_("visualizer.upload.noCredentials", "No credentials configured");
        emit lastUploadStatusChanged();
        emit uploadFailed(tr_("visualizer.upload.credentialsMissing", "Visualizer credentials not configured"));
        emit updateFailed(visualizerId, false, "Visualizer credentials not configured");
        return;
    }

    m_uploading = true;
    emit uploadingChanged();
    m_lastUploadStatus = tr_("visualizer.status.updating", "Updating...");
    emit lastUploadStatusChanged();

    // Build JSON body: {"shot": {"bean_brand": "...", ...}}
    // Field-pointer accessors give compile-time safety on the projection side;
    // the API field strings are visualizer.coffee's external schema (snake_case)
    // and intentionally distinct from the projection's field names.
    //
    // This is a PATCH and we always emit every editable field — the
    // Visualizer API marks them `nullable: true`, so we use JSON null
    // to clear a value the user has unset locally (TDS reset, rating
    // cleared, etc.). Sending literal 0 / "" would set the field to a
    // *value* of zero/empty-string rather than clearing it, which on
    // visualizer.coffee renders as e.g. "Rating: 0/100" instead of
    // "Unrated". Required by migration 16's back-sync for issue #1150
    // users whose default rating is 0.
    QJsonObject shotObj;
    auto setStr = [&](const QString& apiField, QString ShotProjection::*field) {
        const QString& s = shotData.*field;
        shotObj[apiField] = s.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(s);
    };
    auto setDouble = [&](const QString& apiField, double ShotProjection::*field) {
        const double v = shotData.*field;
        shotObj[apiField] = v > 0 ? QJsonValue(v) : QJsonValue(QJsonValue::Null);
    };

    setStr("bean_brand", &ShotProjection::beanBrand);
    setStr("bean_type", &ShotProjection::beanType);
    setStr("roast_level", &ShotProjection::roastLevel);
    // roast_date is the one field that can carry a legacy non-ISO display
    // string (the migration-16 back-sync drains pre-bag shots through this
    // PATCH), so normalize it here rather than via the generic setStr. Empty
    // still clears to null; an unparseable value passes through unchanged.
    {
        const QString iso = RoastDate::toIso(shotData.roastDate);
        shotObj["roast_date"] = iso.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(iso);
    }
    setDouble("bean_weight", &ShotProjection::doseWeightG);
    setDouble("drink_weight", &ShotProjection::finalWeightG);
    // Combine brand + model for visualizer (no separate brand field in API)
    {
        const QString combined =
            (shotData.grinderBrand.trimmed() + " " + shotData.grinderModel.trimmed()).trimmed();
        shotObj["grinder_model"] =
            combined.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(combined);
    }
    // Pair RPM into the freeform grinder_setting (the Visualizer has no native
    // RPM field), matching the initial POST — otherwise a metadata edit PATCHes
    // "2.4 1400rpm" back to bare "2.4" and drops the RPM. Empty → null, same as
    // the generic setStr it replaces.
    {
        const QString gs = grinderSettingWithRpm(shotData.grinderSetting, shotData.rpm);
        shotObj["grinder_setting"] = gs.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(gs);
    }
    setDouble("drink_tds", &ShotProjection::drinkTdsPct);
    setDouble("drink_ey", &ShotProjection::drinkEyPct);
    // null, not omitted, and not 0. Migration 16's back-sync depends on this:
    // it PATCHes shots whose invented rating it just reset to 0, and null is
    // what clears them to Unrated — literal 0 would read as "Rated 0/100" and
    // omitting the field would leave the stale rating on Visualizer forever.
    // The create-path builder below omits on zero instead, which is correct
    // there; do not unify the two onto skip-on-zero.
    shotObj["espresso_enjoyment"] = shotData.enjoyment0to100 > 0
        ? QJsonValue(shotData.enjoyment0to100)
        : QJsonValue(QJsonValue::Null);
    setStr("espresso_notes", &ShotProjection::espressoNotes);
    setStr("barista", &ShotProjection::barista);
    setStr("profile_title", &ShotProjection::profileName);
    // Structured taste taps → CVA (add-ai-taste-intake). Only-when-tapped /
    // never-null so this PATCH cannot clear a hand-entered CVA score.
    applyTasteCvaMapping(shotObj, shotData.tasteBalance, shotData.tasteBody);

    // Canonical bean linkage (5C): when the shot's Bean Base snapshot was
    // picked via Visualizer's canonical autocomplete, the blob carries
    // Visualizer's canonical UUID — send it so the shot clusters by bean on
    // visualizer.coffee too (accepted for ALL users on shot PATCH; the
    // server back-fills bean fields from the canonical record). Only emit
    // when present: never null it out, since the user may have linked the
    // bag in Visualizer's own UI.
    if (!shotData.beanBaseJson.isEmpty()) {
        // A NON-EMPTY blob that fails to parse is corruption, not "unlinked" —
        // every consumer degrades identically/silently, so log it here at the
        // upload chokepoint where it would otherwise vanish without a trace.
        if (!QJsonDocument::fromJson(shotData.beanBaseJson.toUtf8()).isObject())
            qWarning() << "VisualizerUploader: corrupt beanBaseJson on shot" << shotData.id;
        const QString canonicalId = BeanBaseBlob::canonicalId(shotData.beanBaseJson);
        if (!canonicalId.isEmpty())
            shotObj["canonical_coffee_bag_id"] = canonicalId;
    }

    QJsonObject root;
    root["shot"] = shotObj;

    QJsonDocument doc(root);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

    qDebug() << "Visualizer: Updating shot" << visualizerId << "with:" << jsonData;

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
}

void VisualizerUploader::onUpdateFinished(QNetworkReply* reply, const QString& visualizerId)
{
    m_uploading = false;
    emit uploadingChanged();

    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray response = reply->readAll();

    if (reply->error() == QNetworkReply::NoError) {
        m_lastUploadStatus = tr_("visualizer.status.updateSuccess", "Update successful");
        emit lastUploadStatusChanged();
        emit updateSuccess(visualizerId);
        qDebug() << "Visualizer: Update successful for shot" << visualizerId;
    } else {
        QString errorMsg;

        if (statusCode == 401) {
            errorMsg = tr_("visualizer.error.invalidCredentials", "Invalid credentials");
        } else if (statusCode == 404) {
            errorMsg = tr_("visualizer.error.shotNotFound", "Shot not found on Visualizer");
        } else if (statusCode == 422) {
            QJsonDocument doc = QJsonDocument::fromJson(response);
            QJsonObject obj = doc.object();
            errorMsg = obj["error"].toString();
            if (errorMsg.isEmpty()) {
                errorMsg = tr_("visualizer.error.invalidData422", "Invalid data (422)");
            }
        } else {
            errorMsg = tr_("visualizer.error.http", "HTTP %1: %2").arg(statusCode).arg(reply->errorString());
        }

        m_lastUploadStatus = tr_("visualizer.status.failed", "Failed: %1").arg(errorMsg);
        emit lastUploadStatusChanged();
        emit uploadFailed(errorMsg);
        // 404 is the one terminal outcome: the shot is gone from (or was
        // never on) Visualizer, so no retry can ever succeed. Everything
        // else — offline, 5xx, 401 (fixable credentials), 422 — is worth
        // retrying on a later boot.
        emit updateFailed(visualizerId, statusCode == 404, errorMsg);
        qWarning() << "Visualizer: Update failed -" << errorMsg << "Response:" << response;
    }

    reply->deleteLater();
}

void VisualizerUploader::testConnection()
{
    // Re-detect Coffee Management on the next upload — the user may have
    // toggled it (or switched accounts) since the last probe.
    setCmState(CmState::Unknown);

    QString username = m_settings->value("visualizer/username", "").toString();
    QString password = m_settings->value("visualizer/password", "").toString();

    if (username.isEmpty() || password.isEmpty()) {
        emit connectionTestResult(false, tr_("visualizer.test.noUserPass", "Username or password not set"));
        return;
    }

    // Try to access the API to verify credentials
    // We'll use a simple GET to the shots endpoint
    QNetworkRequest request(QUrl("https://visualizer.coffee/api/shots?items=1"));
    request.setRawHeader("Authorization", authHeader().toUtf8());

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onTestFinished(reply);
    });
}

void VisualizerUploader::onUploadFinished(QNetworkReply* reply)
{
    m_uploading = false;
    emit uploadingChanged();

    // Save response to debug file
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray response = reply->readAll();

    QString debugPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (debugPath.isEmpty()) {
        debugPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    QString responseFile = debugPath + "/last_upload_response.txt";
    QFile file(responseFile);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QString("HTTP Status: %1\n\n").arg(statusCode).toUtf8());
        file.write(response);
        file.close();
        qDebug() << "Visualizer: Saved response to" << responseFile;
    }

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
            qDebug() << "Visualizer: Upload successful, ID:" << shotId
                     << "for local shot" << m_uploadingDbShotId;
            // Coffee Management: the server auto-links the shot to its bag on
            // upload; read that link back and enrich the bag's descriptive fields.
            syncCoffeeBagAfterUpload(m_uploadingDbShotId, shotId);
        } else {
            // A 200 with no parseable shot id is a failure, not a success: the
            // local shot row never gets its visualizer_id and the bag sync
            // chain never runs. Surface it so the user sees an error and a
            // retry path, instead of a benign-looking "completed" status.
            m_lastUploadStatus = tr_("visualizer.error.noShotIdReturned", "Upload returned no shot id (unexpected response)");
            emit lastUploadStatusChanged();
            qWarning() << "Visualizer: upload succeeded (HTTP" << statusCode
                       << ") but response had no shot id:" << response.left(200);
            emit uploadFailed(m_lastUploadStatus);
        }
    } else {
        // Transient failures (transport error/timeout = no HTTP status, or a 5xx
        // server blip) auto-retry the same payload a bounded number of times.
        // Auth (401), validation (422), and rate-limit (429 — retrying worsens
        // it) are permanent here; the once-per-device reconciliation backfill
        // recovers anything that still slips through.
        const bool transient = (statusCode == 0 || statusCode >= 500);
        if (transient && m_uploadRetries < kMaxUploadRetries && !m_lastUploadJson.isEmpty()) {
            ++m_uploadRetries;
            qWarning() << "Visualizer: upload transient failure (HTTP" << statusCode
                       << reply->errorString() << ") - retry" << m_uploadRetries
                       << "of" << kMaxUploadRetries;
            reply->deleteLater();
            sendUpload(m_lastUploadJson);  // keeps m_uploadingDbShotId for the retry
            return;
        }

        QString errorMsg;

        if (statusCode == 401) {
            errorMsg = tr_("visualizer.error.invalidCredentials", "Invalid credentials");
        } else if (statusCode == 422) {
            QJsonDocument doc = QJsonDocument::fromJson(response);
            QJsonObject obj = doc.object();
            errorMsg = obj["error"].toString();
            if (errorMsg.isEmpty()) {
                errorMsg = tr_("visualizer.error.invalidShotData422", "Invalid shot data (422)");
            }
        } else {
            errorMsg = tr_("visualizer.error.http", "HTTP %1: %2").arg(statusCode).arg(reply->errorString());
        }

        m_lastUploadStatus = tr_("visualizer.status.failed", "Failed: %1").arg(errorMsg);
        emit lastUploadStatusChanged();
        emit uploadFailed(errorMsg);
        qDebug() << "Visualizer: Upload failed -" << errorMsg << "Response:" << response;
    }

    // Clear the per-upload id on every terminal outcome (success,
    // no-id, or failure) so a subsequent upload can't inherit a stale
    // correlation. Safe because callers never overlap uploads (see the
    // m_uploadingDbShotId note in the header) — m_uploading is UI-only,
    // not a concurrency guard.
    m_uploadingDbShotId = 0;
    reply->deleteLater();
}

void VisualizerUploader::onTestFinished(QNetworkReply* reply)
{
    if (reply->error() == QNetworkReply::NoError) {
        emit connectionTestResult(true, tr_("visualizer.test.success", "Connection successful!"));
    } else {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString errorMsg;

        if (statusCode == 401) {
            errorMsg = tr_("visualizer.test.invalidUserPass", "Invalid username or password");
        } else {
            errorMsg = reply->errorString();
        }

        emit connectionTestResult(false, errorMsg);
    }

    reply->deleteLater();
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

QByteArray VisualizerUploader::buildShotJson(ShotDataModel* shotData,
                                              const Profile* profile,
                                              double finalWeight,
                                              double doseWeight,
                                              const ShotMetadata& metadata,
                                              const QString& debugLog,
                                              qint64 shotEpoch)
{
    QJsonObject root;

    // Get data from ShotDataModel
    const auto& pressureData = shotData->pressureData();
    const auto& flowData = shotData->flowData();
    const auto& temperatureData = shotData->temperatureData();
    const auto& pressureGoalData = shotData->pressureGoalData();
    const auto& flowGoalData = shotData->flowGoalData();
    const auto& temperatureGoalData = shotData->temperatureGoalData();
    const auto& weightFlowRateData = shotData->weightFlowRateData();   // Scale flow rate (g/s)
    const auto& darcyResistanceData = shotData->darcyResistanceData(); // P/flow² (Darcy formula, matches de1app)
    const auto& cumulativeWeightData = shotData->cumulativeWeightData(); // Cumulative weight (g)

    // Use de1app version 2 format
    root["version"] = 2;

    // Timestamps — use the caller-supplied shot epoch so pending uploads don't use upload time
    qint64 clockTime = shotEpoch > 0 ? shotEpoch : QDateTime::currentSecsSinceEpoch();
    root["clock"] = clockTime;
    root["timestamp"] = clockTime;
    root["date"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    // Elapsed time array
    QJsonArray elapsed;
    for (const auto& pt : pressureData) {
        elapsed.append(pt.x());
    }
    root["elapsed"] = elapsed;

    // Pressure object
    QJsonObject pressure;
    QJsonArray pressureValues;
    for (const auto& pt : pressureData) {
        pressureValues.append(pt.y());
    }
    pressure["pressure"] = pressureValues;
    // Interpolate goal data to match elapsed timestamps
    pressure["goal"] = interpolateGoalData(pressureGoalData, pressureData);
    root["pressure"] = pressure;

    // Flow object
    QJsonObject flow;
    QJsonArray flowValues;
    for (const auto& pt : flowData) {
        flowValues.append(pt.y());
    }
    flow["flow"] = flowValues;
    // Interpolate goal data to match elapsed timestamps
    flow["goal"] = interpolateGoalData(flowGoalData, pressureData);
    // Interpolate weight flow rate (g/s from scale) to match elapsed timestamps
    if (!weightFlowRateData.isEmpty()) {
        flow["by_weight"] = interpolateGoalData(weightFlowRateData, pressureData);
    }
    // Raw (pre-smoothing) weight flow rate
    const auto& weightFlowRateRawData = shotData->weightFlowRateRawData();
    if (!weightFlowRateRawData.isEmpty()) {
        flow["by_weight_raw"] = interpolateGoalData(weightFlowRateRawData, pressureData);
    }
    root["flow"] = flow;

    // Temperature object
    QJsonObject temperature;
    QJsonArray basketValues;
    for (const auto& pt : temperatureData) {
        basketValues.append(pt.y());
    }
    temperature["basket"] = basketValues;
    // Interpolate goal data to match elapsed timestamps
    temperature["goal"] = interpolateGoalData(temperatureGoalData, pressureData);
    // Mix temperature (water input temperature)
    const auto& temperatureMixData = shotData->temperatureMixData();
    if (!temperatureMixData.isEmpty()) {
        temperature["mix"] = interpolateGoalData(temperatureMixData, pressureData);
    }
    // Mix temperature goal (SetMixTemp). Omit the key entirely when absent —
    // interpolateGoalData() would otherwise fill a zero array, and Visualizer
    // would draw a 0 °C goal line. Missing means "legacy shot" to Visualizer.
    const auto& temperatureMixGoalData = shotData->temperatureMixGoalData();
    if (!temperatureMixGoalData.isEmpty()) {
        temperature["mix_goal"] = interpolateGoalData(temperatureMixGoalData, pressureData);
    }
    root["temperature"] = temperature;

    // Totals object
    QJsonObject totals;
    if (!cumulativeWeightData.isEmpty()) {
        // Interpolate cumulative weight to match elapsed timestamps
        totals["weight"] = interpolateGoalData(cumulativeWeightData, pressureData);
    }
    // Water dispensed: de1app stores espresso_water_dispensed at 0.1× scale (tenths of ml),
    // so Visualizer expects values ~4.0 for a 40ml shot, not 40.0. Apply the same scaling.
    const auto& waterDispensedData = shotData->waterDispensedData();
    if (!waterDispensedData.isEmpty()) {
        QJsonArray waterDispensedRaw = interpolateGoalData(waterDispensedData, pressureData);
        QJsonArray waterDispensedScaled;
        for (const auto& v : waterDispensedRaw)
            waterDispensedScaled.append(v.toDouble() * 0.1);
        totals["water_dispensed"] = waterDispensedScaled;
    }
    root["totals"] = totals;

    // Resistance object: P/flow² (Darcy formula, matches de1app's espresso_resistance) and
    // P/flow_weight² (scale flow, de1app calls this espresso_resistance_weight → by_weight)
    const auto& resistanceData = darcyResistanceData;  // use Darcy P/flow² to match de1app
    {
        QJsonObject resistance;
        if (!resistanceData.isEmpty())
            resistance["resistance"] = interpolateGoalData(resistanceData, pressureData);
        if (!weightFlowRateData.isEmpty() && !pressureData.isEmpty()) {
            QJsonArray fwInterp = interpolateGoalData(weightFlowRateData, pressureData);
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

    // State change array (de1app format: alternating sign value at each frame transition)
    // Used by Visualizer to draw vertical frame markers on the shot graph
    const auto& markers = shotData->phaseMarkersList();
    if (!markers.isEmpty() && !pressureData.isEmpty()) {
        // Collect times of real frame transitions only (skip Start/End markers)
        QVector<double> transitionTimes;
        for (const auto& m : markers) {
            if (m.frameNumber >= 0 && m.label != "Start")
                transitionTimes.append(m.time);
        }
        QJsonArray stateChange;
        double stateVal = 10000000.0;
        qsizetype markerIdx = 0;
        for (const auto& pt : pressureData) {
            while (markerIdx < transitionTimes.size() && pt.x() >= transitionTimes[markerIdx]) {
                stateVal *= -1.0;
                markerIdx++;
            }
            stateChange.append(stateVal);
        }
        root["state_change"] = stateChange;
    }

    // Scale object: raw weight series at native sample times (for connectivity debugging)
    // de1app sends scale_raw_weight/arrival (raw BLE readings); we send processed cumulative weight.
    // Only emit if there is actual scale data.
    if (!cumulativeWeightData.isEmpty() || !weightFlowRateData.isEmpty()) {
        QJsonObject scale;
        scale["espresso_start"] = clockTime;  // shot-end epoch (consistent with history path)
        if (!cumulativeWeightData.isEmpty()) {
            QJsonArray weights, arrivals;
            for (const auto& pt : cumulativeWeightData) {
                arrivals.append(pt.x());
                weights.append(pt.y());
            }
            scale["weight_arrival"] = arrivals;
            scale["weight"] = weights;
        }
        if (!weightFlowRateData.isEmpty()) {
            QJsonArray flows;
            for (const auto& pt : weightFlowRateData)
                flows.append(pt.y());
            scale["weight_flow"] = flows;
        }
        root["scale"] = scale;
    }

    // Meta object (de1app format)
    QJsonObject meta;

    // Bean info
    QJsonObject bean;
    if (!metadata.beanBrand.isEmpty())
        bean["brand"] = metadata.beanBrand;
    if (!metadata.beanType.isEmpty())
        bean["type"] = metadata.beanType;
    if (!metadata.roastDate.isEmpty())
        bean["roast_date"] = RoastDate::toIso(metadata.roastDate);
    if (!metadata.roastLevel.isEmpty())
        bean["roast_level"] = metadata.roastLevel;
    meta["bean"] = bean;

    // Shot info
    QJsonObject shot;
    if (metadata.espressoEnjoyment > 0)
        shot["enjoyment"] = metadata.espressoEnjoyment;
    if (!metadata.espressoNotes.isEmpty())
        shot["notes"] = metadata.espressoNotes;
    if (metadata.drinkTds > 0)
        shot["tds"] = metadata.drinkTds;
    if (metadata.drinkEy > 0)
        shot["ey"] = metadata.drinkEy;
    meta["shot"] = shot;

    // Grinder info (combine brand+model for visualizer compatibility)
    QJsonObject grinder;
    QString grinderDisplay = metadata.grinderBrand.isEmpty() ? metadata.grinderModel
        : (metadata.grinderModel.isEmpty() ? metadata.grinderBrand
           : metadata.grinderBrand + " " + metadata.grinderModel);
    if (!grinderDisplay.isEmpty())
        grinder["model"] = grinderDisplay;
    {
        const QString gs = grinderSettingWithRpm(metadata.grinderSetting, metadata.rpm);
        if (!gs.isEmpty()) grinder["setting"] = gs;
    }
    meta["grinder"] = grinder;

    // Weights
    double beanWeight = metadata.beanWeight > 0 ? metadata.beanWeight : doseWeight;
    // Use user-entered weight first, then scale weight, then app's flow-integrated volume (ml ≈ g for espresso)
    double drinkWeight = metadata.drinkWeight > 0 ? metadata.drinkWeight : finalWeight;
    if (drinkWeight <= 0) {
        const auto& wdData = shotData->waterDispensedData();
        if (!wdData.isEmpty())
            drinkWeight = wdData.last().y();  // actual ml from flow integration, not scaled
    }
    if (beanWeight > 0)
        meta["in"] = beanWeight;
    if (drinkWeight > 0)
        meta["out"] = drinkWeight;

    // Time
    if (!elapsed.isEmpty()) {
        meta["time"] = elapsed.last().toDouble();
    }

    root["meta"] = meta;

    // App info with settings (Visualizer extracts metadata from app.data.settings)
    QJsonObject app = buildAppInfoJson();

    // Build settings object with all metadata (de1app field names)
    QJsonObject settings;
    if (!metadata.beanBrand.isEmpty())
        settings["bean_brand"] = metadata.beanBrand;
    if (!metadata.beanType.isEmpty())
        settings["bean_type"] = metadata.beanType;
    if (!metadata.roastDate.isEmpty())
        settings["roast_date"] = RoastDate::toIso(metadata.roastDate);
    if (!metadata.roastLevel.isEmpty())
        settings["roast_level"] = metadata.roastLevel;
    if (!grinderDisplay.isEmpty())
        settings["grinder_model"] = grinderDisplay;
    {
        const QString gs = grinderSettingWithRpm(metadata.grinderSetting, metadata.rpm);
        if (!gs.isEmpty()) settings["grinder_setting"] = gs;
    }
    if (beanWeight > 0)
        settings["grinder_dose_weight"] = beanWeight;
    if (drinkWeight > 0)
        settings["drink_weight"] = drinkWeight;
    if (metadata.drinkTds > 0)
        settings["drink_tds"] = metadata.drinkTds;
    if (metadata.drinkEy > 0)
        settings["drink_ey"] = metadata.drinkEy;
    if (metadata.espressoEnjoyment > 0)
        settings["espresso_enjoyment"] = metadata.espressoEnjoyment;
    if (!metadata.espressoNotes.isEmpty())
        settings["espresso_notes"] = metadata.espressoNotes;
    if (!metadata.barista.isEmpty())
        settings["barista"] = metadata.barista;

    // Merge profile fields so Visualizer's DecentJson parser can extract TCL profile data
    if (profile) {
        QJsonObject profileSettings = buildProfileSettings(profile);
        for (auto it = profileSettings.begin(); it != profileSettings.end(); ++it)
            settings[it.key()] = it.value();
    }

    QJsonObject data;
    data["settings"] = settings;
    // Machine state (de1app includes the full ::DE1 array; we include key fields)
    if (m_device) {
        QJsonObject machineState;
        if (!m_device->firmwareVersion().isEmpty())
            machineState["firmware_version"] = m_device->firmwareVersion();
        machineState["state"] = m_device->stateString();
        machineState["substate"] = m_device->subStateString();
        machineState["headless"] = m_device->isHeadless() ? 1 : 0;
        data["machine_state"] = machineState;
    }
    if (!debugLog.isEmpty())
        data["debug_log"] = debugLog;
    app["data"] = data;

    root["app"] = app;

    // Also add barista at root level (Visualizer may extract from here)
    if (!metadata.barista.isEmpty())
        root["barista"] = metadata.barista;

    // Profile
    if (profile) {
        root["profile"] = buildVisualizerProfileJson(profile);
    }

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
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
    s["espresso_temperature"] = QString::number(profile->espressoTemperature(), 'f', 2);
    const auto presets = profile->temperaturePresets();
    for (qsizetype i = 0; i < presets.size() && i < 4; ++i)
        s[QStringLiteral("espresso_temperature_%1").arg(i)] = QString::number(presets[i], 'f', 2);

    // Limits
    s["maximum_pressure"] = QString::number(profile->maximumPressure(), 'f', 1);
    s["maximum_flow"] = QString::number(profile->maximumFlow(), 'f', 1);
    s["flow_profile_minimum_pressure"] = QString::number(profile->minimumPressure(), 'f', 1);
    s["tank_desired_water_temperature"] = QString::number(profile->tankDesiredWaterTemperature(), 'f', 1);
    s["maximum_flow_range_advanced"] = QString::number(profile->maximumFlowRangeAdvanced(), 'f', 1);
    s["maximum_pressure_range_advanced"] = QString::number(profile->maximumPressureRangeAdvanced(), 'f', 1);

    // Target weight/volume
    s["final_desired_shot_weight"] = QString::number(profile->targetWeight(), 'f', 1);
    s["final_desired_shot_weight_advanced"] = s["final_desired_shot_weight"];
    s["final_desired_shot_volume"] = QString::number(profile->targetVolume(), 'f', 0);
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

QJsonObject VisualizerUploader::buildVisualizerProfileJson(const Profile* profile)
{
    if (!profile) {
        QJsonObject obj;
        obj["title"] = "Unknown";
        return obj;
    }

    // Single canonical serialization — identical to the on-disk / exported / share-code
    // format. Profile::toJsonObject() is the one source of truth (string-encoded values,
    // the ecosystem-required tank_temperature / target_volume_count_start keys, standard
    // DE1 v2 metadata, non-empty steps), so a profile downloaded from Visualizer is the
    // same profile any DE1 app would read. Do not re-serialize here — that duplication is
    // exactly what let the two paths drift.
    return profile->toJsonObject();
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
    QString username = m_settings->value("visualizer/username", "").toString();
    QString password = m_settings->value("visualizer/password", "").toString();
    QString credentials = username + ":" + password;
    QByteArray base64 = credentials.toUtf8().toBase64();
    return "Basic " + QString::fromLatin1(base64);
}

bool VisualizerUploader::validateUpload(const QString& beverageType, double duration)
{
    // Skip maintenance profiles (shared tier — see Profile::isMaintenanceBeverageType)
    if (Profile::isMaintenanceBeverageType(beverageType)) {
        const QString reason = tr_("visualizer.skip.maintenance", "maintenance profile (%1)").arg(beverageType);
        m_lastUploadStatus = tr_("visualizer.status.skipped", "Skipped: %1").arg(reason);
        emit lastUploadStatusChanged();
        // Policy skip, not an error — uploadSkipped lets the page clear its
        // in-flight flags without surfacing a red error to UI listeners that
        // treat uploadFailed as a real failure. The page wraps the reason
        // with a translated "Upload skipped:" prefix; emit just the reason
        // payload so the C++ "Skipped:" prefix doesn't double up.
        emit uploadSkipped(reason);
        qDebug() << "Visualizer: Skipping upload for maintenance profile:" << beverageType;
        return false;
    }

    // Check credentials
    QString username = m_settings->value("visualizer/username", "").toString();
    QString password = m_settings->value("visualizer/password", "").toString();
    if (username.isEmpty() || password.isEmpty()) {
        m_lastUploadStatus = tr_("visualizer.upload.noCredentials", "No credentials configured");
        emit lastUploadStatusChanged();
        emit uploadFailed(tr_("visualizer.upload.credentialsMissing", "Visualizer credentials not configured"));
        return false;
    }

    // Check minimum duration
    double minDuration = m_settings->value("visualizer/minDuration", 6.0).toDouble();
    if (duration < minDuration) {
        const QString reason = tr_("visualizer.skip.tooShort", "shot too short (%1s < %2s)").arg(duration, 0, 'f', 1).arg(minDuration, 0, 'f', 0);
        m_lastUploadStatus = tr_("visualizer.status.skipped", "Skipped: %1").arg(reason);
        emit lastUploadStatusChanged();
        // Policy skip, not an error — see uploadSkipped rationale on the
        // maintenance branch above. Emit just the reason payload.
        emit uploadSkipped(reason);
        qDebug() << "Visualizer: Shot too short, not uploading";
        return false;
    }

    m_uploading = true;
    emit uploadingChanged();
    m_lastUploadStatus = tr_("visualizer.status.uploading", "Uploading...");
    emit lastUploadStatusChanged();
    return true;
}

void VisualizerUploader::sendUpload(const QByteArray& jsonData)
{
    // Save JSON to file for debugging
    QString debugPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (debugPath.isEmpty()) {
        debugPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    QDir().mkpath(debugPath);

    QString debugFile = debugPath + "/last_upload.json";
    QFile file(debugFile);
    if (file.open(QIODevice::WriteOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(jsonData);
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
        qDebug() << "Visualizer: Saved debug JSON to" << debugFile;
    } else {
        qDebug() << "Visualizer: Failed to save debug JSON to" << debugFile;
    }

    // Retain the payload so onUploadFinished can re-POST it on a transient
    // failure without rebuilding from (possibly-gone) shot state.
    m_lastUploadJson = jsonData;

    // Build multipart form data
    QString boundary = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QByteArray multipartData = buildMultipartData(jsonData, boundary);

    // Create request
    QUrl url(VISUALIZER_API_URL);
    QNetworkRequest request(url);

    QString authHeaderValue = authHeader();
    request.setRawHeader("Authorization", authHeaderValue.toUtf8());
    request.setRawHeader("Content-Type", QString("multipart/form-data; boundary=%1").arg(boundary).toUtf8());
    // Prevent Qt from following redirects (which can lose auth headers)
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    // Save debug auth info to file
    QString authDebugFile = debugPath + "/last_upload_debug.txt";
    QFile dbgFile(authDebugFile);
    if (dbgFile.open(QIODevice::WriteOnly)) {
        QString username = m_settings->value("visualizer/username", "").toString();
        dbgFile.write(QString("Username: %1\n").arg(username).toUtf8());
        dbgFile.write(QString("Auth header: %1\n").arg(authHeaderValue.left(30) + "...").toUtf8());
        dbgFile.write(QString("URL: %1\n").arg(url.toString()).toUtf8());
        dbgFile.write(QString("Content-Length: %1\n").arg(multipartData.size()).toUtf8());
        dbgFile.close();
    }

    // Send request
    QNetworkReply* reply = m_networkManager->post(request, multipartData);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onUploadFinished(reply);
    });

    qDebug() << "Visualizer: Uploading shot...";
}

// static
QByteArray VisualizerUploader::buildHistoryShotJson(const ShotProjection& shotData)
{
    QJsonObject root;
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
    root["flow"] = flow;

    // Temperature object. Keep this in step with buildShotJson()'s temperature
    // block — a shot re-uploaded from history must carry the same series the
    // live upload sent, or re-uploading silently degrades it.
    QJsonObject temperature;
    temperature["basket"] = extractValues(tempData);
    temperature["goal"] = interpolateGoalData(tempGoalData, pressureData);
    if (!tempMixData.isEmpty()) {
        temperature["mix"] = interpolateGoalData(tempMixData, pressureData);
    }
    // Omit rather than zero-fill when absent — see buildShotJson().
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
    QString grinderDisplay2 = shotData.grinderBrand.isEmpty() ? shotData.grinderModel
        : (shotData.grinderModel.isEmpty() ? shotData.grinderBrand
                                           : shotData.grinderBrand + " " + shotData.grinderModel);
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
    meta["time"] = shotData.durationSec;

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

    if (!shotData.barista.isEmpty()) settings["barista"] = shotData.barista;

    // Parse profile JSON and merge profile fields for Visualizer TCL extraction
    QJsonObject profileJsonObj;
    if (!shotData.profileJson.isEmpty()) {
        QJsonDocument profileDoc = QJsonDocument::fromJson(shotData.profileJson.toUtf8());
        if (!profileDoc.isNull()) {
            Profile profile = Profile::fromJson(profileDoc);
            if (profile.isValid()) {
                // Re-serialize through the one canonical serializer so a history
                // re-upload emits the exact same format as a live upload (and
                // normalizes an old numeric-encoded stored profile to the current
                // string format). Fall back to the raw stored object only when the
                // stored JSON doesn't parse to a valid profile.
                profileJsonObj = profile.toJsonObject();
                QJsonObject profileSettings = buildProfileSettings(&profile);
                for (auto it = profileSettings.begin(); it != profileSettings.end(); ++it)
                    settings[it.key()] = it.value();
            } else {
                profileJsonObj = profileDoc.object();
            }
        }
    }

    // Also set profile_title from shot data (may differ from profile's own title)
    if (!shotData.profileName.isEmpty())
        settings["profile_title"] = shotData.profileName;

    QJsonObject data;
    data["settings"] = settings;
    if (!shotData.debugLog.isEmpty())
        data["debug_log"] = shotData.debugLog;
    app["data"] = data;

    root["app"] = app;

    // Barista at root level (Visualizer may extract from here)
    if (!shotData.barista.isEmpty())
        root["barista"] = shotData.barista;

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
    qDebug() << "Visualizer CM: state" << cmStateName(m_cmState) << "->" << cmStateName(state);
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
    // testConnection) so toggling Coffee Management converges next upload.
    if (m_cmState == CmState::NoCoffeeManagement || m_cmState == CmState::PremiumNoCm)
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
            qDebug() << "Visualizer CM: shot read-back failed - retry next upload";
            return;
        }
        QJsonParseError parseError;
        const QJsonObject shot = QJsonDocument::fromJson(reply->readAll(), &parseError).object();
        if (parseError.error != QJsonParseError::NoError) {
            qDebug() << "Visualizer CM: shot read-back unparseable - retry next upload";
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
            const QString canonicalId = bag.value("beanBaseId").toString();
            if (!canonicalId.isEmpty())
                linkShotCanonical(visualizerShotId, canonicalId);
            qDebug() << "Visualizer CM: shot has no server bag -"
                     << (canonicalId.isEmpty() ? "nothing to link" : "linking canonical coffee");
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
        // CM just (re)confirmed Active: drain bag edits whose push failed
        // retryably or was parked while the state was still Unknown.
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
    // the canonical, but only fires when there's metadata (rating/notes) to send
    // — so this guarantees a known coffee links even on a bare, no-bag shot. Same
    // value as that path, so a double-send is idempotent.
    QJsonObject shotObj{{QStringLiteral("canonical_coffee_bag_id"), canonicalId}};
    QJsonObject root{{QStringLiteral("shot"), shotObj}};
    QNetworkRequest request = makeApiJsonRequest(QStringLiteral("/api/shots/") + visualizerShotId);
    QNetworkReply* reply = m_networkManager->sendCustomRequest(
        request, "PATCH", QJsonDocument(root).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, visualizerShotId, canonicalId]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200) {
            qDebug() << "Visualizer CM: linked shot" << visualizerShotId << "to canonical" << canonicalId;
        } else {
            // status is 0 on a transport error (no HTTP response) — surface the
            // network error string then, matching the sibling read-back handlers.
            qDebug() << "Visualizer CM: shot canonical link failed (HTTP" << status
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
    const QJsonObject blob = QJsonDocument::fromJson(
        bag.value("beanBaseData").toString().toUtf8()).object();
    fillBlank("country",                 blob.value("origin").toString());
    fillBlank("region",                  blob.value("region").toString());
    fillBlank("farm",                    blob.value("farm").toString());
    fillBlank("farmer",                  blob.value("producer").toString());
    fillBlank("variety",                 blob.value("variety").toString());
    fillBlank("processing",              blob.value("process").toString());
    fillBlank("harvest_time",            blob.value("harvest").toString());
    fillBlank("quality_score",           blob.value("qualityScore").toString());
    fillBlank("place_of_purchase",       blob.value("placeOfPurchase").toString());
    fillBlank("tasting_notes",           blob.value("tastingNotes").toString());
    fillBlank("elevation",               blob.value("elevation").toString());
    fillBlank("url",                     blob.value("link").toString());
    fillBlank("notes",                   bag.value("notes").toString());
    fillBlank("frozen_date",             bag.value("frozenDate").toString());
    fillBlank("defrosted_date",          bag.value("defrostDate").toString());
    fillBlank("canonical_coffee_bag_id", bag.value("beanBaseId").toString());
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
            qDebug() << "Visualizer CM: bag" << serverBagId << "gone before enrich - cleared local id";
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "Visualizer CM: bag read for enrich failed (HTTP" << status
                     << ") - retry next upload";
            return;
        }
        QJsonParseError parseError;
        const QJsonObject remote = QJsonDocument::fromJson(reply->readAll(), &parseError).object();
        if (parseError.error != QJsonParseError::NoError) {
            // A 200 with an unparseable body would read as "every field blank" and
            // trigger a full-overwrite PATCH, clobbering the user's server values.
            qDebug() << "Visualizer CM: bag" << serverBagId << "enrich GET unparseable - retry next upload";
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
            qDebug() << "Visualizer CM: bag" << serverBagId << "already complete - nothing to enrich";
            return;
        }
        QNetworkRequest patch = makeApiJsonRequest(QStringLiteral("/api/coffee_bags/") + serverBagId);
        QNetworkReply* preply = m_networkManager->sendCustomRequest(
            patch, "PATCH", QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(preply, &QNetworkReply::finished, this, [this, preply, serverBagId, localBagId]() {
            preply->deleteLater();
            const int st = preply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (st == 200) {
                qDebug() << "Visualizer CM: enriched bag" << serverBagId << "with descriptive fields";
            } else if (st == 403) {
                setCmState(CmState::NoCoffeeManagement);
                qDebug() << "Visualizer CM: bag enrich 403 - account is not premium";
            } else if (st == 404) {
                persistBagSyncIds(localBagId, QString(), QString());
                qDebug() << "Visualizer CM: bag" << serverBagId << "gone during enrich - cleared local id";
            } else {
                qDebug() << "Visualizer CM: bag enrich failed (HTTP" << st << ") - retry next upload";
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
            qDebug() << "Visualizer CM: roaster" << roasterId << "canonical link PATCH HTTP" << st;
        });
    });
}

void VisualizerUploader::resolveRoasterId(const QString& roasterName, const QString& canonicalRoasterId,
                                          std::function<void(const QString&)> onResolved)
{
    QNetworkRequest request = makeApiJsonRequest(QStringLiteral("/api/roasters?items=100"));
    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, roasterName, canonicalRoasterId, onResolved = std::move(onResolved)]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "Visualizer CM: roaster list failed - retry next time";
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
                qDebug() << "Visualizer CM: roaster create 403 - account is not premium";
            } else {
                qDebug() << "Visualizer CM: roaster create failed (HTTP" << status << ")";
            }
        });
    });
}

// static
void VisualizerUploader::addBagDescriptiveFields(QJsonObject& body, const QVariantMap& bag)
{
    // Field names spike-verified (note defrosted_date). startWeightG is
    // local-only: no server field, and `metadata` is the user's own space.
    // The canonical id is a LINK, not a substitute for the attributes — the
    // server does not auto-fill them, so we always send the blob fields.
    // EVERY field (name included) is omitted when locally empty, never sent
    // as ""/null: a roaster-only local bag must not blank (name: 422s) or
    // clear a server-side value the user set on visualizer.coffee.
    auto setIf = [&body](const char* key, const QString& value) {
        if (!value.isEmpty())
            body[QLatin1String(key)] = value;
    };
    setIf("name", bag.value("coffeeName").toString());
    // No RoastDate::toIso() here, unlike the shot paths: a CoffeeBag's roastDate
    // is ISO yyyy-MM-dd by construction (ChangeBeansDialog only stores a 10-char
    // yyyy-mm-dd), which is exactly what the server's roast_date column expects.
    setIf("roast_date", bag.value("roastDate").toString());
    setIf("roast_level", bag.value("roastLevel").toString());
    setIf("frozen_date", bag.value("frozenDate").toString());
    setIf("defrosted_date", bag.value("defrostDate").toString());
    setIf("notes", bag.value("notes").toString());
    setIf("canonical_coffee_bag_id", bag.value("beanBaseId").toString());
    const QJsonObject blob = QJsonDocument::fromJson(
        bag.value("beanBaseData").toString().toUtf8()).object();
    setIf("country", blob.value("origin").toString());
    setIf("region", blob.value("region").toString());
    setIf("farm", blob.value("farm").toString());
    setIf("farmer", blob.value("producer").toString());
    setIf("variety", blob.value("variety").toString());
    setIf("processing", blob.value("process").toString());
    setIf("harvest_time", blob.value("harvest").toString());
    setIf("quality_score", blob.value("qualityScore").toString());
    setIf("place_of_purchase", blob.value("placeOfPurchase").toString());
    setIf("tasting_notes", blob.value("tastingNotes").toString());
    setIf("elevation", blob.value("elevation").toString());
    setIf("url", blob.value("link").toString());
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
                qWarning() << "Visualizer CM: failed to persist sync ids for bag" << localBagId;
        });
    });
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void VisualizerUploader::updateBagOnVisualizer(qint64 localBagId)
{
    if (localBagId <= 0 || m_localDbPath.isEmpty())
        return;
    // Only push to CM-active accounts — matches the create path; a CM-off
    // user's remote bag list is dormant state we never add to. Unknown (pre
    // first-upload this session) also skips: a bag PATCH is premium-gated, NOT
    // CM-gated, so it cannot double as a CM probe — the edit is parked as
    // sync-pending and propagates once a shot upload confirms CM (the
    // retrySyncPendingBags call in the read-back). A definitive CM-off state
    // parks nothing: those accounts never push.
    if (m_cmState != CmState::Active) {
        if (m_cmState == CmState::Unknown)
            persistBagSyncPending(localBagId, true);
        return;
    }

    // Park FIRST, un-park on outcome. Any failure between here and
    // patchRemoteBag's reply handler — bag load failure, the roaster-list GET
    // dying offline, a roaster create dropped — leaves the flag set and the
    // edit is re-pushed on the next upload cycle instead of silently lost
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
                qDebug() << "Visualizer CM: bag" << localBagId << "load failed or deleted - push skipped";
                return;
            }
            // Not synced yet → nothing to PATCH, and pending is moot: the
            // next shot upload's server-side find-or-create carries the
            // CURRENT local fields anyway. Clearing here also stops a bag
            // parked while CM was Unknown (but never uploaded) from sitting
            // in the pending set forever.
            if (bagMap.value("visualizerBagId").toString().isEmpty()) {
                self->persistBagSyncPending(localBagId, false);
                qDebug() << "Visualizer CM: bag" << localBagId << "not synced yet - upload-time create covers it";
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

    QJsonObject body;
    addBagDescriptiveFields(body, bag);
    const QString storedRoasterId = bag.value("visualizerRoasterId").toString();
    const bool roasterChanged = !roasterId.isEmpty() && roasterId != storedRoasterId;
    if (roasterChanged)
        body["roaster_id"] = roasterId;

    const qint64 localBagId = bag.value("id").toLongLong();
    const QString bagDisplayName = QStringList{bag.value("roasterName").toString(),
                                               bag.value("coffeeName").toString()}
                                       .join(QLatin1Char(' ')).trimmed();
    QNetworkRequest request = makeApiJsonRequest(QStringLiteral("/api/coffee_bags/") + bagUuid);
    QNetworkReply* reply = m_networkManager->sendCustomRequest(
        request, "PATCH", QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, bagUuid, roasterId, roasterChanged, localBagId, bagDisplayName]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200) {
            qDebug() << "Visualizer CM: updated coffee bag" << bagUuid;
            persistBagSyncPending(localBagId, false);
            // A roaster rename moved the bag to a different roaster_id — persist
            // it so the next update diffs against the new value.
            if (roasterChanged)
                persistBagSyncIds(localBagId, bagUuid, roasterId);
        } else if (status == 403) {
            // Bag CRUD is premium-gated: a 403 means not premium. Definitive —
            // clear the pending flag so it doesn't retry forever.
            setCmState(CmState::NoCoffeeManagement);
            persistBagSyncPending(localBagId, false);
            qDebug() << "Visualizer CM: bag update 403 - account is not premium";
        } else if (status == 404) {
            // The remote bag was deleted on visualizer.coffee; our id is stale.
            // Leave it — the next shot upload re-creates and re-links the bag
            // (carrying the current local fields), so pending is moot.
            persistBagSyncPending(localBagId, false);
            qDebug() << "Visualizer CM: bag update 404 - remote bag" << bagUuid << "gone";
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
            qDebug() << "Visualizer CM: bag update 422 for bag" << localBagId
                     << "(" << bagDisplayName << ") -" << message;
        } else {
            // Transport error (status 0), 429, or 5xx: retryable. Park the bag
            // as sync-pending; the next upload cycle re-pushes it.
            persistBagSyncPending(localBagId, true);
            qDebug() << "Visualizer CM: bag update failed (HTTP" << status << ") - queued for retry";
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
                qWarning() << "Visualizer CM: failed to persist sync-pending for bag" << localBagId;
        });
    });
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void VisualizerUploader::retrySyncPendingBags()
{
    // Re-push bags whose edit-time push never completed (parked while CM was
    // Unknown, or any failure after the park-first set). Called from the
    // upload read-back once CM is confirmed Active — event-driven, no timers.
    // Each re-push runs the full park-first cycle, so this self-drains on
    // success/definitive outcomes and re-parks on repeat failure.
    if (m_localDbPath.isEmpty() || m_cmState != CmState::Active)
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
                qWarning() << "Visualizer CM: sync-pending query failed:" << query.lastError().text();
                return;
            }
            while (query.next())
                pendingIds << query.value(0).toLongLong();
        });
        QMetaObject::invokeMethod(qApp, [self, pendingIds]() {
            if (!self || pendingIds.isEmpty())
                return;
            qDebug() << "Visualizer CM: re-pushing" << pendingIds.size() << "sync-pending bag(s)";
            for (qint64 bagId : pendingIds)
                self->updateBagOnVisualizer(bagId);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}
