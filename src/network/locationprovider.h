#pragma once

#include <QObject>
#include <QGeoPositionInfoSource>
#include <QNetworkAccessManager>
#include <QTimer>

// Location data with city and coordinates
struct LocationInfo {
    QString city;
    QString countryCode;
    double latitude = 0.0;
    double longitude = 0.0;
    bool valid = false;
};

class LocationProvider : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool available READ isAvailable NOTIFY availableChanged)
    Q_PROPERTY(bool hasLocation READ hasLocation NOTIFY locationChanged)
    Q_PROPERTY(QString city READ city NOTIFY locationChanged)
    Q_PROPERTY(QString countryCode READ countryCode NOTIFY locationChanged)
    Q_PROPERTY(QString manualCity READ manualCity WRITE setManualCity NOTIFY manualCityChanged)
    Q_PROPERTY(bool useManualCity READ useManualCity NOTIFY locationChanged)

public:
    explicit LocationProvider(QObject* parent = nullptr);
    ~LocationProvider();

    bool isAvailable() const { return m_source != nullptr; }
    bool hasLocation() const { return m_currentLocation.valid || !m_manualCity.isEmpty(); }
    QString city() const;
    QString countryCode() const;

    LocationInfo currentLocation() const { return m_currentLocation; }

    // Get rounded coordinates for privacy (1 decimal ~11km)
    double roundedLatitude() const;
    double roundedLongitude() const;

    // Manual city override (takes precedence over GPS when set)
    QString manualCity() const { return m_manualCity; }
    void setManualCity(const QString& city);
    bool useManualCity() const { return !m_manualCity.isEmpty(); }

    // Geocode a city name to coordinates (for manual city)
    Q_INVOKABLE void geocodeManualCity();

    // Request a location update (async)
    Q_INVOKABLE void requestUpdate();

    // Open Android Location Settings (for user to enable GPS)
    Q_INVOKABLE void openLocationSettings();

    // Check if GPS provider is enabled at system level (Android only)
    Q_INVOKABLE bool isGpsEnabled() const;

signals:
    void availableChanged();
    void locationChanged();
    void locationError(const QString& error);
    void manualCityChanged();

private slots:
    void onPositionUpdated(const QGeoPositionInfo& info);
    void onPositionError(QGeoPositionInfoSource::Error error);
    void onReverseGeocodeFinished(QNetworkReply* reply);

private:
    void reverseGeocode(double lat, double lon);
    void forwardGeocode(const QString& city);
    void onForwardGeocodeFinished(QNetworkReply* reply);

    QGeoPositionInfoSource* m_source = nullptr;
    QNetworkAccessManager* m_networkManager;
    LocationInfo m_currentLocation;
    QString m_manualCity;

    // Manual city geocoded coordinates
    double m_manualLat = 0.0;
    double m_manualLon = 0.0;
    QString m_manualCountryCode;
    bool m_manualGeocoded = false;

    // Throttle reverse geocoding (don't query if position hasn't changed much)
    double m_lastGeocodedLat = 0.0;
    double m_lastGeocodedLon = 0.0;
    static constexpr double GEOCODE_THRESHOLD_DEGREES = 0.01;  // ~1km
};
