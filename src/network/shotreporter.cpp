#include "shotreporter.h"
#include "locationprovider.h"
#include "../core/settings.h"
#include "version.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QDebug>

ShotReporter::ShotReporter(QNetworkAccessManager* networkManager, Settings* settings,
                           LocationProvider* locationProvider, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_locationProvider(locationProvider)
    , m_networkManager(networkManager)
{
    Q_ASSERT(networkManager);
    // Connect to location provider signals
    if (m_locationProvider) {
        connect(m_locationProvider, &LocationProvider::locationChanged,
                this, &ShotReporter::onLocationChanged);
        connect(m_locationProvider, &LocationProvider::locationError,
                this, &ShotReporter::onLocationError);
        connect(m_locationProvider, &LocationProvider::manualCityChanged,
                this, &ShotReporter::manualCityChanged);
    }

    // Load enabled state from settings
    if (m_settings) {
        m_enabled = m_settings->value("shotmap/enabled", true).toBool();
    }
}

void ShotReporter::setEnabled(bool enabled)
{
    if (m_enabled == enabled) return;

    m_enabled = enabled;
    emit enabledChanged();

    if (m_settings) {
        m_settings->setValue("shotmap/enabled", enabled);
    }

    // Request location when enabled
    if (enabled && m_locationProvider) {
        m_locationProvider->requestUpdate();
    }

    qDebug() << "ShotReporter:" << (enabled ? "Enabled" : "Disabled");
}

bool ShotReporter::hasLocation() const
{
    return m_locationProvider && m_locationProvider->hasLocation();
}

QString ShotReporter::currentCity() const
{
    return m_locationProvider ? m_locationProvider->city() : QString();
}

QString ShotReporter::currentCountryCode() const
{
    return m_locationProvider ? m_locationProvider->countryCode() : QString();
}

void ShotReporter::refreshLocation()
{
    if (m_locationProvider) {
        m_locationProvider->requestUpdate();
    }
}

void ShotReporter::openLocationSettings()
{
    if (m_locationProvider) {
        m_locationProvider->openLocationSettings();
    }
}

bool ShotReporter::isGpsEnabled() const
{
    return m_locationProvider ? m_locationProvider->isGpsEnabled() : false;
}

void ShotReporter::reportShot(const QString& profileName, const QString& machineModel)
{
    if (!m_enabled) {
        qDebug() << "ShotReporter: Not enabled, skipping";
        return;
    }

    if (!m_locationProvider || !m_locationProvider->hasLocation()) {
        qDebug() << "ShotReporter: No location available, skipping";
        m_lastError = "No location available";
        emit lastErrorChanged();
        return;
    }

    // Build shot event
    ShotEvent event;
    event.city = m_locationProvider->city();
    event.countryCode = m_locationProvider->countryCode();
    event.latitude = m_locationProvider->roundedLatitude();
    event.longitude = m_locationProvider->roundedLongitude();
    event.profileName = profileName;
    event.softwareName = "Decenza|DE1";
    event.softwareVersion = VERSION_STRING;
    event.machineModel = machineModel.isEmpty() ? "Decent DE1" : machineModel;
    event.timestampMs = QDateTime::currentMSecsSinceEpoch();

    qDebug() << "ShotReporter: Reporting shot -"
             << "City:" << event.city
             << "Profile:" << event.profileName;

    sendShotEvent(event);
}

void ShotReporter::sendShotEvent(const ShotEvent& event)
{
    // Build JSON payload
    QJsonObject json;
    json["city"] = event.city;
    if (!event.countryCode.isEmpty()) {
        json["country_code"] = event.countryCode;
    }
    json["lat"] = event.latitude;
    json["lon"] = event.longitude;
    json["profile"] = event.profileName;
    json["software_name"] = event.softwareName;
    json["software_version"] = event.softwareVersion;
    json["machine_model"] = event.machineModel;
    json["ts"] = event.timestampMs;

    QByteArray payload = QJsonDocument(json).toJson(QJsonDocument::Compact);

    // Create request
    QUrl url(API_URL);
    QNetworkRequest request;
    request.setUrl(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QString("%1/%2 (%3)")
                          .arg(event.softwareName)
                          .arg(event.softwareVersion)
                          .arg(event.machineModel));

    // Send request
    QNetworkReply* reply = m_networkManager->post(request, payload);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReplyFinished(reply);
    });

    qDebug() << "ShotReporter: Sending to" << API_URL;
}

void ShotReporter::onReplyFinished(QNetworkReply* reply)
{
    reply->deleteLater();

    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray response = reply->readAll();

    if (reply->error() == QNetworkReply::NoError && (statusCode == 200 || statusCode == 202)) {
        QJsonDocument doc = QJsonDocument::fromJson(response);
        QJsonObject obj = doc.object();

        if (obj["ok"].toBool()) {
            QString eventId = obj["event_id"].toString();
            qDebug() << "ShotReporter: Success - event_id:" << eventId;
            m_lastError.clear();
            emit lastErrorChanged();
            emit shotReported(eventId);
        } else {
            QString error = obj["error"].toString();
            qDebug() << "ShotReporter: API error -" << error;
            m_lastError = error;
            emit lastErrorChanged();
            emit shotReportFailed(error);
        }
    } else if (statusCode == 409) {
        // Duplicate idempotency key - treat as success
        qDebug() << "ShotReporter: Duplicate event (409), treating as success";
        m_lastError.clear();
        emit lastErrorChanged();
        emit shotReported("");
    } else {
        QString error;
        if (statusCode == 400) {
            QJsonDocument doc = QJsonDocument::fromJson(response);
            error = doc.object()["error"].toString();
        } else if (statusCode == 429) {
            error = "Rate limited - too many requests";
        } else {
            error = QString("HTTP %1: %2").arg(statusCode).arg(reply->errorString());
        }

        qDebug() << "ShotReporter: Failed -" << error;
        m_lastError = error;
        emit lastErrorChanged();
        emit shotReportFailed(error);
    }
}

void ShotReporter::onLocationChanged()
{
    qDebug() << "ShotReporter: Location updated -"
             << m_locationProvider->city()
             << m_locationProvider->countryCode();
    emit locationStatusChanged();
}

void ShotReporter::onLocationError(const QString& error)
{
    bool gpsEnabled = m_locationProvider ? m_locationProvider->isGpsEnabled() : false;
    qDebug() << "ShotReporter: Location error -" << error
             << "hasPrompted:" << m_hasPromptedForLocation
             << "hasLocation:" << (m_locationProvider ? m_locationProvider->hasLocation() : false)
             << "gpsEnabled:" << gpsEnabled;
    m_lastError = error;
    emit lastErrorChanged();

    // Auto-open Location Settings if:
    // - Shot map is enabled
    // - Haven't prompted yet this session
    // - GPS is actually disabled at system level
    if (m_enabled && !m_hasPromptedForLocation && m_locationProvider && !gpsEnabled) {
        m_hasPromptedForLocation = true;
        qDebug() << "ShotReporter: GPS disabled at system level, opening Location Settings";
        m_locationProvider->openLocationSettings();
    }
}

QString ShotReporter::manualCity() const
{
    return m_locationProvider ? m_locationProvider->manualCity() : QString();
}

void ShotReporter::setManualCity(const QString& city)
{
    if (m_locationProvider) {
        m_locationProvider->setManualCity(city);
    }
}

bool ShotReporter::usingManualCity() const
{
    return m_locationProvider && m_locationProvider->useManualCity();
}

double ShotReporter::latitude() const
{
    return m_locationProvider ? m_locationProvider->roundedLatitude() : 0.0;
}

double ShotReporter::longitude() const
{
    return m_locationProvider ? m_locationProvider->roundedLongitude() : 0.0;
}
