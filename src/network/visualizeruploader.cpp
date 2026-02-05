#include "visualizeruploader.h"
#include "../models/shotdatamodel.h"
#include "../core/settings.h"
#include "../profile/profile.h"
#include "version.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHttpMultiPart>
#include <QDateTime>
#include <QDebug>
#include <QUuid>
#include <QStandardPaths>
#include <QFile>
#include <QDir>
#include <QBuffer>

VisualizerUploader::VisualizerUploader(Settings* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_networkManager(new QNetworkAccessManager(this))
{
}

// Helper: Interpolate goal data to match elapsed timestamps
// Goal data may have different timestamps or gaps; we need to align to the master elapsed array
// Gaps > 0.5s between goal points indicate mode switches (flow/pressure) - return 0 during gaps
static QJsonArray interpolateGoalData(const QVector<QPointF>& goalData, const QVector<QPointF>& masterData) {
    QJsonArray result;

    if (goalData.isEmpty() || masterData.isEmpty()) {
        // Return zeros for all timestamps if no goal data
        for (int i = 0; i < masterData.size(); ++i) {
            result.append(0.0);
        }
        return result;
    }

    // Gap threshold: if consecutive goal points are more than 0.5s apart, treat as a gap
    constexpr double GAP_THRESHOLD = 0.5;

    int goalIdx = 0;
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
                                     const ShotMetadata& metadata)
{
    if (!shotData) {
        emit uploadFailed("No shot data available");
        return;
    }

    // Skip maintenance profiles (cleaning, calibration, descaling)
    if (profile) {
        QString bevType = profile->beverageType();
        if (bevType == "cleaning" || bevType == "calibrate" || bevType == "descale") {
            qDebug() << "Visualizer: Skipping upload for maintenance profile:" << bevType;
            return;
        }
    }

    // Check credentials
    QString username = m_settings->value("visualizer/username", "").toString();
    QString password = m_settings->value("visualizer/password", "").toString();

    if (username.isEmpty() || password.isEmpty()) {
        m_lastUploadStatus = "No credentials configured";
        emit lastUploadStatusChanged();
        emit uploadFailed("Visualizer credentials not configured");
        return;
    }

    // Check minimum duration
    double minDuration = m_settings->value("visualizer/minDuration", 6.0).toDouble();
    if (duration < minDuration) {
        m_lastUploadStatus = QString("Shot too short (%1s < %2s)").arg(duration, 0, 'f', 1).arg(minDuration, 0, 'f', 0);
        emit lastUploadStatusChanged();
        emit uploadFailed(m_lastUploadStatus);
        qDebug() << "Visualizer: Shot too short, not uploading";
        return;
    }

    m_uploading = true;
    emit uploadingChanged();
    m_lastUploadStatus = "Uploading...";
    emit lastUploadStatusChanged();

    // Build JSON payload
    QByteArray jsonData = buildShotJson(shotData, profile, finalWeight, doseWeight, metadata);

    // Save JSON to file for debugging
    QString debugPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (debugPath.isEmpty()) {
        debugPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    QDir().mkpath(debugPath);
    QString debugFile = debugPath + "/last_upload.json";
    QFile file(debugFile);
    if (file.open(QIODevice::WriteOnly)) {
        // Pretty print for readability
        QJsonDocument doc = QJsonDocument::fromJson(jsonData);
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
        qDebug() << "Visualizer: Saved debug JSON to" << debugFile;
    } else {
        qDebug() << "Visualizer: Failed to save debug JSON to" << debugFile;
    }

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

    // Save debug info to file
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

void VisualizerUploader::uploadShotFromHistory(const QVariantMap& shotData)
{
    if (shotData.isEmpty()) {
        emit uploadFailed("No shot data available");
        return;
    }

    // Skip maintenance profiles (cleaning, calibration, descaling)
    {
        QString profileJson = shotData["profileJson"].toString();
        if (!profileJson.isEmpty()) {
            QJsonDocument profileDoc = QJsonDocument::fromJson(profileJson.toUtf8());
            if (!profileDoc.isNull()) {
                QString bevType = profileDoc.object()["beverage_type"].toString();
                if (bevType == "cleaning" || bevType == "calibrate" || bevType == "descale") {
                    qDebug() << "Visualizer: Skipping upload for maintenance profile:" << bevType;
                    return;
                }
            }
        }
    }

    // Check credentials
    QString username = m_settings->value("visualizer/username", "").toString();
    QString password = m_settings->value("visualizer/password", "").toString();

    if (username.isEmpty() || password.isEmpty()) {
        m_lastUploadStatus = "No credentials configured";
        emit lastUploadStatusChanged();
        emit uploadFailed("Visualizer credentials not configured");
        return;
    }

    double duration = shotData["duration"].toDouble();
    double minDuration = m_settings->value("visualizer/minDuration", 6.0).toDouble();
    if (duration < minDuration) {
        m_lastUploadStatus = QString("Shot too short (%1s < %2s)").arg(duration, 0, 'f', 1).arg(minDuration, 0, 'f', 0);
        emit lastUploadStatusChanged();
        emit uploadFailed(m_lastUploadStatus);
        qDebug() << "Visualizer: Shot too short, not uploading";
        return;
    }

    m_uploading = true;
    emit uploadingChanged();
    m_lastUploadStatus = "Uploading...";
    emit lastUploadStatusChanged();

    // Build JSON from history data
    QJsonObject root;
    root["version"] = 2;

    // Use original timestamp from the shot
    qint64 timestamp = shotData["timestamp"].toLongLong();
    root["clock"] = timestamp;
    root["timestamp"] = timestamp;
    root["date"] = QDateTime::fromSecsSinceEpoch(timestamp).toString(Qt::ISODate);

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

    // Helper to extract just the values from a point vector (for data aligned with elapsed)
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
    QVector<QPointF> pressureData = toPointVector(shotData["pressure"].toList());
    QVector<QPointF> flowData = toPointVector(shotData["flow"].toList());
    QVector<QPointF> tempData = toPointVector(shotData["temperature"].toList());
    QVector<QPointF> pressureGoalData = toPointVector(shotData["pressureGoal"].toList());
    QVector<QPointF> flowGoalData = toPointVector(shotData["flowGoal"].toList());
    QVector<QPointF> tempGoalData = toPointVector(shotData["temperatureGoal"].toList());
    QVector<QPointF> weightData = toPointVector(shotData["weight"].toList());

    // Elapsed time array (from pressure data - the master timeline)
    root["elapsed"] = extractTimes(pressureData);

    // Pressure object - interpolate goal to match elapsed timestamps
    QJsonObject pressure;
    pressure["pressure"] = extractValues(pressureData);
    pressure["goal"] = interpolateGoalData(pressureGoalData, pressureData);
    root["pressure"] = pressure;

    // Flow object - interpolate goal to match elapsed timestamps
    QJsonObject flow;
    flow["flow"] = extractValues(flowData);
    flow["goal"] = interpolateGoalData(flowGoalData, pressureData);
    root["flow"] = flow;

    // Temperature object - interpolate goal to match elapsed timestamps
    QJsonObject temperature;
    temperature["basket"] = extractValues(tempData);
    temperature["goal"] = interpolateGoalData(tempGoalData, pressureData);
    root["temperature"] = temperature;

    // Totals object (weight) - interpolate to match elapsed timestamps
    QJsonObject totals;
    if (!weightData.isEmpty()) {
        totals["weight"] = interpolateGoalData(weightData, pressureData);
    }
    root["totals"] = totals;

    // Meta object
    QJsonObject meta;

    // Bean info
    QJsonObject bean;
    QString beanBrand = shotData["beanBrand"].toString();
    QString beanType = shotData["beanType"].toString();
    QString roastDate = shotData["roastDate"].toString();
    QString roastLevel = shotData["roastLevel"].toString();
    if (!beanBrand.isEmpty()) bean["brand"] = beanBrand;
    if (!beanType.isEmpty()) bean["type"] = beanType;
    if (!roastDate.isEmpty()) bean["roast_date"] = roastDate;
    if (!roastLevel.isEmpty()) bean["roast_level"] = roastLevel;
    meta["bean"] = bean;

    // Shot info
    QJsonObject shot;
    int enjoyment = shotData["enjoyment"].toInt();
    QString notes = shotData["espressoNotes"].toString();
    double tds = shotData["drinkTds"].toDouble();
    double ey = shotData["drinkEy"].toDouble();
    if (enjoyment > 0) shot["enjoyment"] = enjoyment;
    if (!notes.isEmpty()) shot["notes"] = notes;
    if (tds > 0) shot["tds"] = tds;
    if (ey > 0) shot["ey"] = ey;
    meta["shot"] = shot;

    // Grinder info
    QJsonObject grinder;
    QString grinderModel = shotData["grinderModel"].toString();
    QString grinderSetting = shotData["grinderSetting"].toString();
    if (!grinderModel.isEmpty()) grinder["model"] = grinderModel;
    if (!grinderSetting.isEmpty()) grinder["setting"] = grinderSetting;
    meta["grinder"] = grinder;

    // Weights
    double doseWeight = shotData["doseWeight"].toDouble();
    double finalWeight = shotData["finalWeight"].toDouble();
    if (doseWeight > 0) meta["in"] = doseWeight;
    if (finalWeight > 0) meta["out"] = finalWeight;
    meta["time"] = duration;

    root["meta"] = meta;

    // App info
    QJsonObject app;
#if defined(Q_OS_IOS)
    app["app_name"] = "Decenza DE1 iOS";
#elif defined(Q_OS_ANDROID)
    app["app_name"] = "Decenza DE1 Android";
#elif defined(Q_OS_WIN)
    app["app_name"] = "Decenza DE1 Windows";
#elif defined(Q_OS_MACOS)
    app["app_name"] = "Decenza DE1 macOS";
#else
    app["app_name"] = "Decenza DE1";
#endif
    app["app_version"] = VERSION_STRING;
    root["app"] = app;

    // Profile
    QString profileJson = shotData["profileJson"].toString();
    if (!profileJson.isEmpty()) {
        QJsonDocument profileDoc = QJsonDocument::fromJson(profileJson.toUtf8());
        if (!profileDoc.isNull()) {
            root["profile"] = profileDoc.object();
        }
    }

    // Convert to JSON bytes
    QJsonDocument doc(root);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

    // Save JSON to file for debugging
    QString debugPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (debugPath.isEmpty()) {
        debugPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    QDir().mkpath(debugPath);
    QString debugFile = debugPath + "/last_upload.json";
    QFile file(debugFile);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
    }

    // Create multipart form data
    QString boundary = "----DecenzaBoundary" + QString::number(QDateTime::currentMSecsSinceEpoch());
    QByteArray multipartData;
    multipartData.append("--" + boundary.toUtf8() + "\r\n");
    multipartData.append("Content-Disposition: form-data; name=\"file\"; filename=\"shot.json\"\r\n");
    multipartData.append("Content-Type: application/json\r\n\r\n");
    multipartData.append(jsonData);
    multipartData.append("\r\n--" + boundary.toUtf8() + "--\r\n");

    // Send request
    QUrl url(VISUALIZER_API_URL);
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", authHeader().toUtf8());
    request.setRawHeader("Content-Type", QString("multipart/form-data; boundary=%1").arg(boundary).toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_networkManager->post(request, multipartData);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onUploadFinished(reply);
    });

    qDebug() << "Visualizer: Uploading shot from history...";
}

void VisualizerUploader::updateShotOnVisualizer(const QString& visualizerId, const QVariantMap& shotData)
{
    if (visualizerId.isEmpty()) {
        emit uploadFailed("No visualizer ID for update");
        return;
    }

    // Check credentials
    QString username = m_settings->value("visualizer/username", "").toString();
    QString password = m_settings->value("visualizer/password", "").toString();

    if (username.isEmpty() || password.isEmpty()) {
        m_lastUploadStatus = "No credentials configured";
        emit lastUploadStatusChanged();
        emit uploadFailed("Visualizer credentials not configured");
        return;
    }

    m_uploading = true;
    emit uploadingChanged();
    m_lastUploadStatus = "Updating...";
    emit lastUploadStatusChanged();

    // Build JSON body: {"shot": {"bean_brand": "...", ...}}
    QJsonObject shotObj;
    auto setField = [&](const QString& apiField, const QString& mapKey) {
        if (!shotData.contains(mapKey)) return;
        QVariant val = shotData[mapKey];
        if (val.typeId() == QMetaType::Double) {
            double d = val.toDouble();
            if (d > 0) shotObj[apiField] = d;
        } else if (val.typeId() == QMetaType::Int) {
            int i = val.toInt();
            if (i > 0) shotObj[apiField] = i;
        } else {
            QString s = val.toString();
            if (!s.isEmpty()) shotObj[apiField] = s;
        }
    };

    setField("bean_brand", "beanBrand");
    setField("bean_type", "beanType");
    setField("roast_level", "roastLevel");
    setField("roast_date", "roastDate");
    setField("bean_weight", "doseWeight");
    setField("drink_weight", "finalWeight");
    setField("grinder_model", "grinderModel");
    setField("grinder_setting", "grinderSetting");
    setField("drink_tds", "drinkTds");
    setField("drink_ey", "drinkEy");
    setField("espresso_enjoyment", "enjoyment");
    setField("espresso_notes", "espressoNotes");
    setField("barista", "barista");
    setField("profile_title", "profileName");

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
        m_lastUploadStatus = "Update successful";
        emit lastUploadStatusChanged();
        emit updateSuccess(visualizerId);
        qDebug() << "Visualizer: Update successful for shot" << visualizerId;
    } else {
        QString errorMsg;

        if (statusCode == 401) {
            errorMsg = "Invalid credentials";
        } else if (statusCode == 404) {
            errorMsg = "Shot not found on Visualizer";
        } else if (statusCode == 422) {
            QJsonDocument doc = QJsonDocument::fromJson(response);
            errorMsg = doc.object()["error"].toString();
            if (errorMsg.isEmpty()) {
                errorMsg = "Invalid data (422)";
            }
        } else {
            errorMsg = QString("HTTP %1: %2").arg(statusCode).arg(reply->errorString());
        }

        m_lastUploadStatus = "Failed: " + errorMsg;
        emit lastUploadStatusChanged();
        emit uploadFailed(errorMsg);
        qDebug() << "Visualizer: Update failed -" << errorMsg << "Response:" << response;
    }

    reply->deleteLater();
}

void VisualizerUploader::testConnection()
{
    QString username = m_settings->value("visualizer/username", "").toString();
    QString password = m_settings->value("visualizer/password", "").toString();

    if (username.isEmpty() || password.isEmpty()) {
        emit connectionTestResult(false, "Username or password not set");
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
            m_lastUploadStatus = "Upload successful";
            emit lastShotUrlChanged();
            emit lastUploadStatusChanged();
            emit uploadSuccess(shotId, m_lastShotUrl);
            qDebug() << "Visualizer: Upload successful, ID:" << shotId;
        } else {
            m_lastUploadStatus = "Upload completed (no ID returned)";
            emit lastUploadStatusChanged();
            qDebug() << "Visualizer: Upload response:" << response;
        }
    } else {
        QString errorMsg;

        if (statusCode == 401) {
            errorMsg = "Invalid credentials";
        } else if (statusCode == 422) {
            QJsonDocument doc = QJsonDocument::fromJson(response);
            errorMsg = doc.object()["error"].toString();
            if (errorMsg.isEmpty()) {
                errorMsg = "Invalid shot data (422)";
            }
        } else {
            errorMsg = QString("HTTP %1: %2").arg(statusCode).arg(reply->errorString());
        }

        m_lastUploadStatus = "Failed: " + errorMsg;
        emit lastUploadStatusChanged();
        emit uploadFailed(errorMsg);
        qDebug() << "Visualizer: Upload failed -" << errorMsg << "Response:" << response;
    }

    reply->deleteLater();
}

void VisualizerUploader::onTestFinished(QNetworkReply* reply)
{
    if (reply->error() == QNetworkReply::NoError) {
        emit connectionTestResult(true, "Connection successful!");
    } else {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString errorMsg;

        if (statusCode == 401) {
            errorMsg = "Invalid username or password";
        } else {
            errorMsg = reply->errorString();
        }

        emit connectionTestResult(false, errorMsg);
    }

    reply->deleteLater();
}

QByteArray VisualizerUploader::buildShotJson(ShotDataModel* shotData,
                                              const Profile* profile,
                                              double finalWeight,
                                              double doseWeight,
                                              const ShotMetadata& metadata)
{
    QJsonObject root;

    // Get data from ShotDataModel
    const auto& pressureData = shotData->pressureData();
    const auto& flowData = shotData->flowData();
    const auto& temperatureData = shotData->temperatureData();
    const auto& pressureGoalData = shotData->pressureGoalData();
    const auto& flowGoalData = shotData->flowGoalData();
    const auto& temperatureGoalData = shotData->temperatureGoalData();
    const auto& weightFlowData = shotData->weightData();  // Flow rate from scale (g/s)
    const auto& cumulativeWeightData = shotData->cumulativeWeightData();  // Cumulative weight (g)

    // Use de1app version 2 format
    root["version"] = 2;

    // Timestamps
    qint64 clockTime = QDateTime::currentSecsSinceEpoch();
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
    // Interpolate weight flow to match elapsed timestamps
    if (!weightFlowData.isEmpty()) {
        flow["by_weight"] = interpolateGoalData(weightFlowData, pressureData);
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
    root["temperature"] = temperature;

    // Totals object
    QJsonObject totals;
    if (!cumulativeWeightData.isEmpty()) {
        // Interpolate cumulative weight to match elapsed timestamps
        totals["weight"] = interpolateGoalData(cumulativeWeightData, pressureData);
    }
    // Water dispensed (cumulative flow volume in ml)
    const auto& waterDispensedData = shotData->waterDispensedData();
    if (!waterDispensedData.isEmpty()) {
        totals["water_dispensed"] = interpolateGoalData(waterDispensedData, pressureData);
    }
    root["totals"] = totals;

    // Resistance object (pressure / flow²)
    const auto& resistanceData = shotData->resistanceData();
    if (!resistanceData.isEmpty()) {
        QJsonObject resistance;
        resistance["resistance"] = interpolateGoalData(resistanceData, pressureData);
        root["resistance"] = resistance;
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
        bean["roast_date"] = metadata.roastDate;
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

    // Grinder info
    QJsonObject grinder;
    if (!metadata.grinderModel.isEmpty())
        grinder["model"] = metadata.grinderModel;
    if (!metadata.grinderSetting.isEmpty())
        grinder["setting"] = metadata.grinderSetting;
    meta["grinder"] = grinder;

    // Weights
    double beanWeight = metadata.beanWeight > 0 ? metadata.beanWeight : doseWeight;
    double drinkWeight = metadata.drinkWeight > 0 ? metadata.drinkWeight : finalWeight;
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
    QJsonObject app;
#if defined(Q_OS_IOS)
    app["app_name"] = "Decenza DE1 iOS";
#elif defined(Q_OS_ANDROID)
    app["app_name"] = "Decenza DE1 Android";
#elif defined(Q_OS_WIN)
    app["app_name"] = "Decenza DE1 Windows";
#elif defined(Q_OS_MACOS)
    app["app_name"] = "Decenza DE1 macOS";
#elif defined(Q_OS_LINUX)
    app["app_name"] = "Decenza DE1 Linux";
#else
    app["app_name"] = "Decenza DE1";
#endif
    app["app_version"] = VERSION_STRING;

    // Build settings object with all metadata (de1app field names)
    QJsonObject settings;
    if (!metadata.beanBrand.isEmpty())
        settings["bean_brand"] = metadata.beanBrand;
    if (!metadata.beanType.isEmpty())
        settings["bean_type"] = metadata.beanType;
    if (!metadata.roastDate.isEmpty())
        settings["roast_date"] = metadata.roastDate;
    if (!metadata.roastLevel.isEmpty())
        settings["roast_level"] = metadata.roastLevel;
    if (!metadata.grinderModel.isEmpty())
        settings["grinder_model"] = metadata.grinderModel;
    if (!metadata.grinderSetting.isEmpty())
        settings["grinder_setting"] = metadata.grinderSetting;
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

    QJsonObject data;
    data["settings"] = settings;
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

QJsonObject VisualizerUploader::buildVisualizerProfileJson(const Profile* profile)
{
    QJsonObject obj;

    if (!profile) {
        obj["title"] = "Unknown";
        return obj;
    }

    // Basic metadata
    obj["title"] = profile->title();
    obj["author"] = profile->author();
    obj["notes"] = profile->profileNotes();
    obj["beverage_type"] = profile->beverageType();

    // Convert steps to Visualizer format
    QJsonArray stepsArray;
    for (const auto& step : profile->steps()) {
        QJsonObject stepObj;

        // Values as strings (Visualizer format)
        stepObj["name"] = step.name;
        stepObj["temperature"] = QString::number(step.temperature, 'f', 2);
        stepObj["sensor"] = step.sensor;
        stepObj["pump"] = step.pump;
        stepObj["transition"] = step.transition;
        stepObj["pressure"] = QString::number(step.pressure, 'f', 2);
        stepObj["flow"] = QString::number(step.flow, 'f', 2);
        stepObj["seconds"] = QString::number(step.seconds, 'f', 2);
        stepObj["volume"] = QString::number(step.volume, 'f', 0);
        stepObj["weight"] = "0";  // Per-step weight not used

        // Exit condition (Visualizer format: {type, value, condition})
        if (step.exitIf && !step.exitType.isEmpty()) {
            QJsonObject exitObj;
            if (step.exitType == "pressure_over") {
                exitObj["type"] = "pressure";
                exitObj["value"] = QString::number(step.exitPressureOver, 'f', 2);
                exitObj["condition"] = "over";
            } else if (step.exitType == "pressure_under") {
                exitObj["type"] = "pressure";
                exitObj["value"] = QString::number(step.exitPressureUnder, 'f', 2);
                exitObj["condition"] = "under";
            } else if (step.exitType == "flow_over") {
                exitObj["type"] = "flow";
                exitObj["value"] = QString::number(step.exitFlowOver, 'f', 2);
                exitObj["condition"] = "over";
            } else if (step.exitType == "flow_under") {
                exitObj["type"] = "flow";
                exitObj["value"] = QString::number(step.exitFlowUnder, 'f', 2);
                exitObj["condition"] = "under";
            }
            if (!exitObj.isEmpty()) {
                stepObj["exit"] = exitObj;
            }
        }

        // Limiter (Visualizer format: {value, range})
        QJsonObject limiterObj;
        limiterObj["value"] = QString::number(step.maxFlowOrPressure, 'f', 1);
        limiterObj["range"] = QString::number(step.maxFlowOrPressureRange, 'f', 1);
        stepObj["limiter"] = limiterObj;

        stepsArray.append(stepObj);
    }
    obj["steps"] = stepsArray;

    // Additional Visualizer metadata
    obj["tank_temperature"] = "0";
    obj["target_weight"] = QString::number(profile->targetWeight(), 'f', 0);
    obj["target_volume"] = QString::number(profile->targetVolume(), 'f', 0);
    obj["target_volume_count_start"] = "2";
    obj["legacy_profile_type"] = profile->profileType();
    obj["type"] = "advanced";
    obj["lang"] = "en";
    obj["hidden"] = "0";
    obj["reference_file"] = profile->title();
    obj["changes_since_last_espresso"] = "";
    obj["version"] = "2";

    return obj;
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
