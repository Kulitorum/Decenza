#include "locationprovider.h"

#include <QGeoPositionInfo>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>
#include <QDebug>
#include <cmath>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
#include <QDesktopServices>
#include <QPermissions>
#endif

LocationProvider::LocationProvider(QObject* parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
    // Load saved manual city and coordinates
    QSettings settings;
    m_manualCity = settings.value("shotMap/manualCity", "").toString();
    m_manualLat = settings.value("shotMap/manualLat", 0.0).toDouble();
    m_manualLon = settings.value("shotMap/manualLon", 0.0).toDouble();
    m_manualCountryCode = settings.value("shotMap/manualCountryCode", "").toString();
    m_manualGeocoded = settings.value("shotMap/manualGeocoded", false).toBool();

    // Try to create a position source
    m_source = QGeoPositionInfoSource::createDefaultSource(this);

    if (m_source) {
        connect(m_source, &QGeoPositionInfoSource::positionUpdated,
                this, &LocationProvider::onPositionUpdated);
        connect(m_source, &QGeoPositionInfoSource::errorOccurred,
                this, &LocationProvider::onPositionError);

        // Prefer low accuracy (network-based) for faster initial fix, especially indoors
        m_source->setPreferredPositioningMethods(QGeoPositionInfoSource::AllPositioningMethods);

        qDebug() << "LocationProvider: GPS source available:" << m_source->sourceName()
                 << "methods:" << m_source->supportedPositioningMethods();

        // Try to get last known position immediately (might be cached from previous app run)
        QGeoPositionInfo lastPos = m_source->lastKnownPosition();
        if (lastPos.isValid()) {
            QGeoCoordinate coord = lastPos.coordinate();
            qDebug() << "LocationProvider: Last known position available -"
                     << "Lat:" << coord.latitude() << "Lon:" << coord.longitude()
                     << "Age:" << lastPos.timestamp().secsTo(QDateTime::currentDateTime()) << "seconds";
        } else {
            qDebug() << "LocationProvider: No last known position available";
        }
    } else {
        qDebug() << "LocationProvider: No GPS source available";
    }

    // Re-request location when app returns to foreground (e.g. after user grants
    // permission in System Settings on macOS/iOS)
    connect(qApp, &QGuiApplication::applicationStateChanged,
            this, &LocationProvider::onAppStateChanged);

    if (!m_manualCity.isEmpty()) {
        qDebug() << "LocationProvider: Manual city configured:" << m_manualCity
                 << "at" << m_manualLat << m_manualLon << m_manualCountryCode;

        // Re-geocode if country code is missing (migration from older versions)
        if (m_manualGeocoded && m_manualCountryCode.isEmpty()) {
            qDebug() << "LocationProvider: Re-geocoding manual city to obtain country code";
            QTimer::singleShot(0, this, &LocationProvider::geocodeManualCity);
        }
    }
}

LocationProvider::~LocationProvider()
{
    if (m_source) {
        m_source->stopUpdates();
    }
}

double LocationProvider::roundedLatitude() const
{
    // Manual city takes precedence over GPS
    if (!m_manualCity.isEmpty() && m_manualGeocoded)
        return std::round(m_manualLat * 10.0) / 10.0;
    if (m_currentLocation.valid)
        return std::round(m_currentLocation.latitude * 10.0) / 10.0;
    return 0.0;
}

double LocationProvider::roundedLongitude() const
{
    // Manual city takes precedence over GPS
    if (!m_manualCity.isEmpty() && m_manualGeocoded)
        return std::round(m_manualLon * 10.0) / 10.0;
    if (m_currentLocation.valid)
        return std::round(m_currentLocation.longitude * 10.0) / 10.0;
    return 0.0;
}

void LocationProvider::requestUpdate()
{
    if (!m_source) {
        emit locationError("No GPS source available");
        return;
    }

#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    // Qt 6 requires explicit permission request before using CoreLocation
    QLocationPermission locationPermission;
    locationPermission.setAccuracy(QLocationPermission::Approximate);

    auto status = qApp->checkPermission(locationPermission);
    if (status == Qt::PermissionStatus::Undetermined) {
        qDebug() << "LocationProvider: Requesting location permission...";
        qApp->requestPermission(locationPermission, this, [this](const QPermission& permission) {
            if (permission.status() == Qt::PermissionStatus::Granted) {
                qDebug() << "LocationProvider: Location permission granted";
                m_source->requestUpdate(60000);
            } else {
                qDebug() << "LocationProvider: Location permission denied by user";
                emit locationError("Location permission denied");
            }
        });
        return;
    } else if (status == Qt::PermissionStatus::Denied) {
        qDebug() << "LocationProvider: Location permission previously denied";
        emit locationError("Location permission denied - enable in System Settings");
        return;
    }
#endif

    qDebug() << "LocationProvider: Requesting position update (60s timeout)...";
    m_source->requestUpdate(60000);  // 60 second timeout (GPS cold start can take a while)
}

void LocationProvider::onPositionUpdated(const QGeoPositionInfo& info)
{
    if (!info.isValid()) {
        qDebug() << "LocationProvider: Received invalid position";
        return;
    }

    QGeoCoordinate coord = info.coordinate();
    qDebug() << "LocationProvider: Position updated -"
             << "Lat:" << coord.latitude()
             << "Lon:" << coord.longitude();

    m_currentLocation.latitude = coord.latitude();
    m_currentLocation.longitude = coord.longitude();

    // Check if we need to reverse geocode (position changed significantly)
    double latDiff = std::abs(coord.latitude() - m_lastGeocodedLat);
    double lonDiff = std::abs(coord.longitude() - m_lastGeocodedLon);

    if (latDiff > GEOCODE_THRESHOLD_DEGREES || lonDiff > GEOCODE_THRESHOLD_DEGREES
        || m_currentLocation.city.isEmpty()) {
        reverseGeocode(coord.latitude(), coord.longitude());
    } else {
        // Position hasn't changed much, just update coordinates
        m_currentLocation.valid = true;
        emit locationChanged();
    }
}

void LocationProvider::onPositionError(QGeoPositionInfoSource::Error error)
{
    QString errorStr;
    switch (error) {
    case QGeoPositionInfoSource::AccessError:
        errorStr = "Location permission denied";
        break;
    case QGeoPositionInfoSource::ClosedError:
        errorStr = "Location source closed";
        break;
    case QGeoPositionInfoSource::NoError:
        return;
    case QGeoPositionInfoSource::UpdateTimeoutError:
        // Try to use last known position as fallback
        if (m_source) {
            QGeoPositionInfo lastPos = m_source->lastKnownPosition();
            if (lastPos.isValid()) {
                qDebug() << "LocationProvider: GPS timeout, using last known position";
                onPositionUpdated(lastPos);
                return;
            }
        }
        errorStr = "GPS timeout - no satellite fix (try outdoors or set city manually)";
        break;
    case QGeoPositionInfoSource::UnknownSourceError:
        errorStr = "Unknown GPS source error";
        break;
    default:
        errorStr = QString("Unknown location error (code: %1)").arg(static_cast<int>(error));
        break;
    }

    qDebug() << "LocationProvider: Error -" << errorStr;
    emit locationError(errorStr);
}

void LocationProvider::reverseGeocode(double lat, double lon)
{
    // Use Nominatim for reverse geocoding (free, no API key needed)
    // Note: Must respect usage policy - max 1 request/second, include User-Agent
    QString url = QString("https://nominatim.openstreetmap.org/reverse?format=json&lat=%1&lon=%2&zoom=10")
                      .arg(lat, 0, 'f', 6)
                      .arg(lon, 0, 'f', 6);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Decenza_DE1/1.0 (espresso app)");

    qDebug() << "LocationProvider: Reverse geocoding...";

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReverseGeocodeFinished(reply);
    });

    // Remember this position to avoid re-geocoding
    m_lastGeocodedLat = lat;
    m_lastGeocodedLon = lon;
}

void LocationProvider::onReverseGeocodeFinished(QNetworkReply* reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        qDebug() << "LocationProvider: Reverse geocode failed -" << reply->errorString();
        // Still mark as valid - we have coordinates, just no city name
        m_currentLocation.valid = true;
        emit locationChanged();
        return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject obj = doc.object();

    // Extract city and country from address
    QJsonObject address = obj["address"].toObject();

    // Try different fields for city (Nominatim uses different names depending on location)
    QString city = address["city"].toString();
    if (city.isEmpty()) city = address["town"].toString();
    if (city.isEmpty()) city = address["village"].toString();
    if (city.isEmpty()) city = address["municipality"].toString();
    if (city.isEmpty()) city = address["county"].toString();
    if (city.isEmpty()) city = address["state"].toString();

    QString countryCode = address["country_code"].toString().toUpper();

    qDebug() << "LocationProvider: Geocoded to" << city << countryCode;

    m_currentLocation.city = city;
    m_currentLocation.countryCode = countryCode;
    m_currentLocation.valid = true;

    emit locationChanged();
}

QString LocationProvider::countryCode() const
{
    // Manual city takes precedence over GPS
    if (!m_manualCity.isEmpty() && !m_manualCountryCode.isEmpty())
        return m_manualCountryCode;
    return m_currentLocation.countryCode;
}

QString LocationProvider::city() const
{
    // Manual city takes precedence over GPS
    if (!m_manualCity.isEmpty())
        return m_manualCity;
    if (m_currentLocation.valid)
        return m_currentLocation.city;
    return QString();
}

void LocationProvider::setManualCity(const QString& city)
{
    if (m_manualCity != city) {
        m_manualCity = city;
        m_manualGeocoded = false;
        m_manualLat = 0.0;
        m_manualLon = 0.0;
        m_manualCountryCode.clear();

        // Save to settings
        QSettings settings;
        settings.setValue("shotMap/manualCity", city);
        settings.setValue("shotMap/manualGeocoded", false);
        settings.setValue("shotMap/manualLat", 0.0);
        settings.setValue("shotMap/manualLon", 0.0);
        settings.setValue("shotMap/manualCountryCode", "");

        qDebug() << "LocationProvider: Manual city set to:" << city;

        emit manualCityChanged();
        emit locationChanged();

        // Auto-geocode if city is not empty
        if (!city.isEmpty()) {
            geocodeManualCity();
        }
    }
}

void LocationProvider::geocodeManualCity()
{
    if (m_manualCity.isEmpty()) {
        qDebug() << "LocationProvider: No manual city to geocode";
        return;
    }

    forwardGeocode(m_manualCity);
}

void LocationProvider::forwardGeocode(const QString& city)
{
    // Use Nominatim for forward geocoding
    QString encodedCity = QUrl::toPercentEncoding(city);
    QString url = QString("https://nominatim.openstreetmap.org/search?format=json&addressdetails=1&q=%1&limit=1")
                      .arg(encodedCity);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Decenza_DE1/1.0 (espresso app)");

    qDebug() << "LocationProvider: Forward geocoding:" << city;

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onForwardGeocodeFinished(reply);
    });
}

void LocationProvider::onForwardGeocodeFinished(QNetworkReply* reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        qDebug() << "LocationProvider: Forward geocode failed -" << reply->errorString();
        emit locationError("Failed to geocode city: " + reply->errorString());
        return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray results = doc.array();

    if (results.isEmpty()) {
        qDebug() << "LocationProvider: No geocoding results for" << m_manualCity;
        emit locationError("City not found: " + m_manualCity);
        return;
    }

    QJsonObject result = results.first().toObject();
    m_manualLat = result["lat"].toString().toDouble();
    m_manualLon = result["lon"].toString().toDouble();
    m_manualCountryCode = result["address"].toObject()["country_code"].toString().toUpper();
    m_manualGeocoded = true;

    // Save to settings
    QSettings settings;
    settings.setValue("shotMap/manualLat", m_manualLat);
    settings.setValue("shotMap/manualLon", m_manualLon);
    settings.setValue("shotMap/manualCountryCode", m_manualCountryCode);
    settings.setValue("shotMap/manualGeocoded", true);

    QString displayName = result["display_name"].toString();
    qDebug() << "LocationProvider: Geocoded" << m_manualCity << "to"
             << m_manualLat << m_manualLon << m_manualCountryCode << "-" << displayName;

    emit locationChanged();
}

void LocationProvider::openLocationSettings()
{
#ifdef Q_OS_ANDROID
    qDebug() << "LocationProvider: Opening Android Location Settings";

    QJniObject activity = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "activity",
        "()Landroid/app/Activity;");

    if (!activity.isValid()) {
        qDebug() << "LocationProvider: Failed to get activity";
        return;
    }

    // Create intent for Location Settings
    QJniObject intent("android/content/Intent",
                      "(Ljava/lang/String;)V",
                      QJniObject::fromString("android.settings.LOCATION_SOURCE_SETTINGS").object<jstring>());

    // Add flag for new task
    intent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", 0x10000000); // FLAG_ACTIVITY_NEW_TASK

    // Start the settings activity
    activity.callMethod<void>("startActivity", "(Landroid/content/Intent;)V", intent.object());

    qDebug() << "LocationProvider: Location Settings opened";
#elif defined(Q_OS_MACOS)
    qDebug() << "LocationProvider: Opening macOS Location Settings";
    QDesktopServices::openUrl(QUrl("x-apple.systempreferences:com.apple.preference.security?Privacy_LocationServices"));
#elif defined(Q_OS_IOS)
    qDebug() << "LocationProvider: Opening iOS Settings";
    QDesktopServices::openUrl(QUrl("app-settings:"));
#else
    qDebug() << "LocationProvider: openLocationSettings() not supported on this platform";
#endif
}

bool LocationProvider::isGpsEnabled() const
{
#ifdef Q_OS_ANDROID
    // Get the activity context
    QJniObject activity = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "activity",
        "()Landroid/app/Activity;");

    if (!activity.isValid()) {
        qDebug() << "LocationProvider: Failed to get activity for GPS check";
        return false;
    }

    // Get LocationManager service
    QJniObject locationServiceName = QJniObject::fromString("location");
    QJniObject locationManager = activity.callObjectMethod(
        "getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;",
        locationServiceName.object<jstring>());

    if (!locationManager.isValid()) {
        qDebug() << "LocationProvider: Failed to get LocationManager";
        return false;
    }

    // Check if GPS provider is enabled
    QJniObject gpsProvider = QJniObject::fromString("gps");
    jboolean enabled = locationManager.callMethod<jboolean>(
        "isProviderEnabled",
        "(Ljava/lang/String;)Z",
        gpsProvider.object<jstring>());

    qDebug() << "LocationProvider: GPS provider enabled:" << enabled;
    return enabled;
#else
    // On desktop, assume GPS is available if we have a source
    return m_source != nullptr;
#endif
}

void LocationProvider::onAppStateChanged(Qt::ApplicationState state)
{
    if (state != Qt::ApplicationActive)
        return;

    // If we already have a location (GPS or manual), nothing to do
    if (m_currentLocation.valid || !m_manualCity.isEmpty())
        return;

    // No source yet (permission may have just been granted) — try creating one
    if (!m_source) {
        m_source = QGeoPositionInfoSource::createDefaultSource(this);
        if (m_source) {
            connect(m_source, &QGeoPositionInfoSource::positionUpdated,
                    this, &LocationProvider::onPositionUpdated);
            connect(m_source, &QGeoPositionInfoSource::errorOccurred,
                    this, &LocationProvider::onPositionError);
            m_source->setPreferredPositioningMethods(QGeoPositionInfoSource::AllPositioningMethods);
            qDebug() << "LocationProvider: GPS source now available after app resume:" << m_source->sourceName();
            emit availableChanged();
        }
    }

    qDebug() << "LocationProvider: App resumed without location, re-requesting";
    requestUpdate();
}
