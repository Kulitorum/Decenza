#include "visualizeruploader.h"
#include "../models/shotdatamodel.h"
#include "../core/settings.h"
#include "../profile/profile.h"

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

VisualizerUploader::VisualizerUploader(Settings* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_networkManager(new QNetworkAccessManager(this))
{
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
    const auto& weightData = shotData->weightData();

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
    if (!pressureGoalData.isEmpty()) {
        QJsonArray pressureGoalValues;
        for (const auto& pt : pressureGoalData) {
            pressureGoalValues.append(pt.y());
        }
        pressure["goal"] = pressureGoalValues;
    }
    root["pressure"] = pressure;

    // Flow object
    QJsonObject flow;
    QJsonArray flowValues;
    for (const auto& pt : flowData) {
        flowValues.append(pt.y());
    }
    flow["flow"] = flowValues;
    if (!flowGoalData.isEmpty()) {
        QJsonArray flowGoalValues;
        for (const auto& pt : flowGoalData) {
            flowGoalValues.append(pt.y());
        }
        flow["goal"] = flowGoalValues;
    }
    if (!weightData.isEmpty()) {
        QJsonArray byWeight;
        for (const auto& pt : weightData) {
            byWeight.append(pt.y() * 5.0);  // Undo the /5 scaling
        }
        flow["by_weight"] = byWeight;
    }
    root["flow"] = flow;

    // Temperature object
    QJsonObject temperature;
    QJsonArray basketValues;
    for (const auto& pt : temperatureData) {
        basketValues.append(pt.y());
    }
    temperature["basket"] = basketValues;
    if (!temperatureGoalData.isEmpty()) {
        QJsonArray tempGoalValues;
        for (const auto& pt : temperatureGoalData) {
            tempGoalValues.append(pt.y());
        }
        temperature["goal"] = tempGoalValues;
    }
    root["temperature"] = temperature;

    // Totals object
    QJsonObject totals;
    if (!weightData.isEmpty()) {
        QJsonArray weightValues;
        for (const auto& pt : weightData) {
            weightValues.append(pt.y() * 5.0);  // Undo the /5 scaling
        }
        totals["weight"] = weightValues;
    }
    root["totals"] = totals;

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
    app["app_name"] = "Decenza DE1";
    app["app_version"] = "1.0.0";

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
    obj["notes"] = profile->notes();
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
