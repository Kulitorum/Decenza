#pragma once

#include <QObject>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QDateTime>
#include <QVariantList>

class LocationProvider;

// Hourly forecast data point - all units metric
struct HourlyForecast {
    QDateTime time;
    double temperature = 0.0;             // Celsius
    double apparentTemperature = 0.0;     // Celsius (feels like)
    int relativeHumidity = 0;             // %
    double windSpeed = 0.0;               // km/h
    int windDirection = 0;                // degrees (0-360)
    double precipitation = 0.0;           // mm
    int precipitationProbability = 0;     // %
    int weatherCode = -1;                 // WMO standard code (0-99)
    double cloudCover = 0.0;              // %
    double uvIndex = 0.0;
    bool isDaytime = true;                // true if sun is up

    QVariantMap toVariantMap() const;
};

// Weather data provider identifier
enum class WeatherProvider {
    None,
    OpenMeteo,
    NWS,        // US National Weather Service
    MetNorway   // Norwegian Meteorological Institute (yr.no)
};

class WeatherManager : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool valid READ valid NOTIFY weatherChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString provider READ providerName NOTIFY weatherChanged)
    Q_PROPERTY(QString locationName READ locationName NOTIFY weatherChanged)
    Q_PROPERTY(QDateTime lastUpdate READ lastUpdate NOTIFY weatherChanged)
    Q_PROPERTY(QVariantList hourlyForecast READ hourlyForecast NOTIFY weatherChanged)
    Q_PROPERTY(bool useImperialUnits READ useImperialUnits NOTIFY weatherChanged)
    Q_PROPERTY(bool use12HourTime READ use12HourTime NOTIFY weatherChanged)

public:
    explicit WeatherManager(QNetworkAccessManager* networkManager, QObject* parent = nullptr);

    void setLocationProvider(LocationProvider* provider);

    // Property getters
    bool valid() const { return m_valid; }
    bool loading() const { return m_loading; }
    QString providerName() const;
    QString locationName() const { return m_locationName; }
    QDateTime lastUpdate() const { return m_lastUpdate; }
    QVariantList hourlyForecast() const;
    bool useImperialUnits() const;
    bool use12HourTime() const;

    // Force a refresh
    Q_INVOKABLE void refresh();

    // WMO weather code helpers
    static QString weatherDescription(int wmoCode);
    static QString weatherIconName(int wmoCode);

signals:
    void weatherChanged();
    void loadingChanged();

private slots:
    void onLocationChanged();
    void onRefreshTimer();

private:
    // Provider selection
    WeatherProvider selectProvider() const;

    // Fetch methods - each makes HTTP request and calls storeForecasts() on success
    void fetchWeather();
    void fetchFromOpenMeteo(double lat, double lon);
    void fetchFromNWS(double lat, double lon);
    void fetchFromMetNorway(double lat, double lon);

    // NWS two-step: first resolve grid point, then fetch hourly forecast
    void fetchNWSHourlyFromGridUrl(const QString& forecastHourlyUrl);

    // Parse responses into HourlyForecast list
    QList<HourlyForecast> parseOpenMeteoResponse(const QJsonDocument& doc);
    QList<HourlyForecast> parseNWSResponse(const QJsonDocument& doc);
    QList<HourlyForecast> parseMetNorwayResponse(const QJsonDocument& doc);

    // Store parsed data and emit signals
    void storeForecasts(const QList<HourlyForecast>& forecasts, WeatherProvider provider);

    // Fetch sunrise/sunset from Open-Meteo and apply to stored forecasts
    void fetchSunTimes(double lat, double lon);
    void applySunTimes();
    bool isDaytimeAt(const QDateTime& time) const;

    // Fallback to Open-Meteo on primary provider failure
    void fallbackToOpenMeteo(double lat, double lon, const QString& reason);

    // Convert MET Norway symbol_code to WMO weather code
    static int metNorwaySymbolToWMO(const QString& symbolCode);

    // Convert NWS icon URL to WMO weather code
    static int nwsIconToWMO(const QString& iconUrl);

    void setLoading(bool loading);

    // Current effective coordinates from LocationProvider
    double effectiveLatitude() const;
    double effectiveLongitude() const;

    LocationProvider* m_locationProvider = nullptr;
    QNetworkAccessManager* m_networkManager;
    QTimer m_refreshTimer;

    // Stored forecast data
    QList<HourlyForecast> m_forecasts;
    WeatherProvider m_provider = WeatherProvider::None;
    QString m_locationName;
    QDateTime m_lastUpdate;
    bool m_valid = false;
    bool m_loading = false;

    // Sunrise/sunset times per day (for isDaytime calculation)
    QVector<QPair<QDateTime, QDateTime>> m_sunTimes;

    // Cached locale check (avoids creating ICU objects on every call)
    bool m_use12HourTime = false;

    // Track last fetch coordinates to detect significant moves
    double m_lastFetchLat = 0.0;
    double m_lastFetchLon = 0.0;

    // Prevent concurrent fetches
    bool m_fetchInProgress = false;

    static constexpr int REFRESH_INTERVAL_MS = 60 * 60 * 1000;  // 1 hour
    static constexpr double LOCATION_CHANGE_THRESHOLD = 0.1;     // ~11km
    static constexpr int MAX_HOURLY_ENTRIES = 73;                 // 3 days + current hour

    static const QString USER_AGENT;
};
