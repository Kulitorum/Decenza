#include "weathermanager.h"
#include "../network/locationprovider.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QTimeZone>
#include <QHash>
#include <QLocale>
#include <cmath>

const QString WeatherManager::USER_AGENT = QStringLiteral("DecenzaDE1/1.0 (github.com/Kulitorum/de1-qt)");

// ─── HourlyForecast ──────────────────────────────────────────────────────────

QVariantMap HourlyForecast::toVariantMap() const
{
    return {
        {"time",                      time.toString(Qt::ISODate)},
        {"timeMs",                    time.toMSecsSinceEpoch()},
        {"hour",                      time.toString("HH:mm")},
        {"temperature",               temperature},
        {"apparentTemperature",       apparentTemperature},
        {"relativeHumidity",          relativeHumidity},
        {"windSpeed",                 windSpeed},
        {"windDirection",             windDirection},
        {"precipitation",             precipitation},
        {"precipitationProbability",  precipitationProbability},
        {"weatherCode",               weatherCode},
        {"cloudCover",                cloudCover},
        {"uvIndex",                   uvIndex},
        {"isDaytime",                  isDaytime},
        {"weatherDescription",        WeatherManager::weatherDescription(weatherCode)},
        {"weatherIcon",               WeatherManager::weatherIconName(weatherCode)}
    };
}

// ─── WeatherManager ──────────────────────────────────────────────────────────

WeatherManager::WeatherManager(QObject* parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
    m_refreshTimer.setInterval(REFRESH_INTERVAL_MS);
    connect(&m_refreshTimer, &QTimer::timeout, this, &WeatherManager::onRefreshTimer);
}

void WeatherManager::setLocationProvider(LocationProvider* provider)
{
    if (m_locationProvider == provider)
        return;

    if (m_locationProvider)
        disconnect(m_locationProvider, nullptr, this, nullptr);

    m_locationProvider = provider;

    if (m_locationProvider) {
        connect(m_locationProvider, &LocationProvider::locationChanged,
                this, &WeatherManager::onLocationChanged);

        // If location is already available, fetch immediately
        if (m_locationProvider->hasLocation()) {
            // Delay slightly to let other init finish
            QTimer::singleShot(2000, this, &WeatherManager::onLocationChanged);
        }
    }
}

QString WeatherManager::providerName() const
{
    switch (m_provider) {
    case WeatherProvider::OpenMeteo:  return QStringLiteral("Open-Meteo");
    case WeatherProvider::NWS:        return QStringLiteral("NWS");
    case WeatherProvider::MetNorway:  return QStringLiteral("MET Norway");
    default:                          return QString();
    }
}

bool WeatherManager::useImperialUnits() const
{
    if (!m_locationProvider)
        return false;
    QString country = m_locationProvider->countryCode().toLower();
    // Countries that use Fahrenheit: US + territories, plus other Fahrenheit nations
    static const QStringList fahrenheitCountries = {
        "us", "pr", "vi", "gu", "as", "mp",  // US + territories
        "bs", "ky", "lr", "pw", "fm", "mh"   // Bahamas, Cayman Islands, Liberia, Palau, Micronesia, Marshall Islands
    };
    return fahrenheitCountries.contains(country);
}

bool WeatherManager::use12HourTime() const
{
    // Check system locale's time format for AM/PM indicator
    QString fmt = QLocale::system().timeFormat(QLocale::ShortFormat);
    return fmt.contains("AP", Qt::CaseInsensitive);
}

QVariantList WeatherManager::hourlyForecast() const
{
    QVariantList list;
    list.reserve(m_forecasts.size());
    for (const auto& f : m_forecasts)
        list.append(f.toVariantMap());
    return list;
}

void WeatherManager::refresh()
{
    fetchWeather();
}

// ─── Location handling ───────────────────────────────────────────────────────

void WeatherManager::onLocationChanged()
{
    if (!m_locationProvider || !m_locationProvider->hasLocation())
        return;

    double lat = effectiveLatitude();
    double lon = effectiveLongitude();

    // Skip if location hasn't changed significantly since last fetch
    if (m_valid &&
        std::abs(lat - m_lastFetchLat) < LOCATION_CHANGE_THRESHOLD &&
        std::abs(lon - m_lastFetchLon) < LOCATION_CHANGE_THRESHOLD) {
        return;
    }

    fetchWeather();
}

void WeatherManager::onRefreshTimer()
{
    fetchWeather();
}

double WeatherManager::effectiveLatitude() const
{
    if (!m_locationProvider)
        return 0.0;
    return m_locationProvider->roundedLatitude();
}

double WeatherManager::effectiveLongitude() const
{
    if (!m_locationProvider)
        return 0.0;
    return m_locationProvider->roundedLongitude();
}

// ─── Provider selection ──────────────────────────────────────────────────────

WeatherProvider WeatherManager::selectProvider() const
{
    if (!m_locationProvider)
        return WeatherProvider::OpenMeteo;

    QString country = m_locationProvider->countryCode().toLower();

    if (country == "us")
        return WeatherProvider::NWS;

    // Nordic countries - MET Norway has excellent data
    static const QStringList nordicCountries = {"no", "se", "fi", "dk", "is"};
    if (nordicCountries.contains(country))
        return WeatherProvider::MetNorway;

    return WeatherProvider::OpenMeteo;
}

// ─── Fetch orchestration ─────────────────────────────────────────────────────

void WeatherManager::fetchWeather()
{
    if (!m_locationProvider || !m_locationProvider->hasLocation()) {
        qDebug() << "WeatherManager: No location available, skipping fetch";
        return;
    }

    if (m_fetchInProgress) {
        qDebug() << "WeatherManager: Fetch already in progress, skipping";
        return;
    }

    double lat = effectiveLatitude();
    double lon = effectiveLongitude();

    if (lat == 0.0 && lon == 0.0) {
        qDebug() << "WeatherManager: Coordinates are 0,0, skipping fetch";
        return;
    }

    m_lastFetchLat = lat;
    m_lastFetchLon = lon;
    m_fetchInProgress = true;
    setLoading(true);

    // Update location name from provider
    m_locationName = m_locationProvider->city();

    WeatherProvider provider = selectProvider();
    qDebug() << "WeatherManager: Fetching weather for" << lat << lon
             << "using" << (provider == WeatherProvider::NWS ? "NWS" :
                           provider == WeatherProvider::MetNorway ? "MET Norway" : "Open-Meteo");

    switch (provider) {
    case WeatherProvider::NWS:
        fetchFromNWS(lat, lon);
        break;
    case WeatherProvider::MetNorway:
        fetchFromMetNorway(lat, lon);
        break;
    default:
        fetchFromOpenMeteo(lat, lon);
        break;
    }

    // Start/restart the hourly timer
    m_refreshTimer.start();
}

void WeatherManager::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

// ─── Open-Meteo (global fallback) ───────────────────────────────────────────

void WeatherManager::fetchFromOpenMeteo(double lat, double lon)
{
    QUrl url(QStringLiteral("https://api.open-meteo.com/v1/forecast"));
    QUrlQuery query;
    query.addQueryItem("latitude", QString::number(lat, 'f', 2));
    query.addQueryItem("longitude", QString::number(lon, 'f', 2));
    query.addQueryItem("hourly",
        "temperature_2m,relative_humidity_2m,apparent_temperature,"
        "precipitation_probability,precipitation,weather_code,"
        "wind_speed_10m,wind_direction_10m,cloud_cover,uv_index,is_day");
    query.addQueryItem("timezone", "auto");
    query.addQueryItem("forecast_hours", QString::number(MAX_HOURLY_ENTRIES));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, USER_AGENT);

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, lat, lon]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "WeatherManager: Open-Meteo request failed:" << reply->errorString();
            m_fetchInProgress = false;
            setLoading(false);
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QList<HourlyForecast> forecasts = parseOpenMeteoResponse(doc);

        if (forecasts.isEmpty()) {
            qWarning() << "WeatherManager: Open-Meteo returned no forecast data";
            m_fetchInProgress = false;
            setLoading(false);
            return;
        }

        storeForecasts(forecasts, WeatherProvider::OpenMeteo);
    });
}

QList<HourlyForecast> WeatherManager::parseOpenMeteoResponse(const QJsonDocument& doc)
{
    QList<HourlyForecast> result;
    QJsonObject root = doc.object();
    QJsonObject hourly = root["hourly"].toObject();

    QJsonArray times = hourly["time"].toArray();
    QJsonArray temps = hourly["temperature_2m"].toArray();
    QJsonArray humidity = hourly["relative_humidity_2m"].toArray();
    QJsonArray apparent = hourly["apparent_temperature"].toArray();
    QJsonArray precipProb = hourly["precipitation_probability"].toArray();
    QJsonArray precip = hourly["precipitation"].toArray();
    QJsonArray codes = hourly["weather_code"].toArray();
    QJsonArray windSpeed = hourly["wind_speed_10m"].toArray();
    QJsonArray windDir = hourly["wind_direction_10m"].toArray();
    QJsonArray cloud = hourly["cloud_cover"].toArray();
    QJsonArray uv = hourly["uv_index"].toArray();
    QJsonArray isDay = hourly["is_day"].toArray();

    int count = qMin(static_cast<int>(times.size()), MAX_HOURLY_ENTRIES);
    result.reserve(count);

    for (int i = 0; i < count; ++i) {
        HourlyForecast f;
        f.time = QDateTime::fromString(times[i].toString(), Qt::ISODate);
        f.temperature = temps[i].toDouble();
        f.relativeHumidity = humidity[i].toInt();
        f.apparentTemperature = apparent[i].toDouble();
        f.precipitationProbability = precipProb[i].toInt();
        f.precipitation = precip[i].toDouble();
        f.weatherCode = codes[i].toInt();
        f.windSpeed = windSpeed[i].toDouble();
        f.windDirection = windDir[i].toInt();
        f.cloudCover = cloud[i].toDouble();
        f.uvIndex = uv[i].toDouble();
        f.isDaytime = isDay[i].toInt() == 1;
        result.append(f);
    }

    return result;
}

// ─── NWS cardinal wind direction → degrees ──────────────────────────────────

static int nwsCardinalToDirection(const QString& cardinal)
{
    static const QHash<QString, int> map = {
        {"N",   0},   {"NNE", 22},  {"NE", 45},  {"ENE", 67},
        {"E",   90},  {"ESE", 112}, {"SE", 135}, {"SSE", 157},
        {"S",   180}, {"SSW", 202}, {"SW", 225}, {"WSW", 247},
        {"W",   270}, {"WNW", 292}, {"NW", 315}, {"NNW", 337},
    };
    return map.value(cardinal.toUpper(), 0);
}

// ─── NWS (US National Weather Service) ───────────────────────────────────────

void WeatherManager::fetchFromNWS(double lat, double lon)
{
    // Step 1: Resolve grid point from coordinates
    QString pointsUrl = QString("https://api.weather.gov/points/%1,%2")
                            .arg(lat, 0, 'f', 4)
                            .arg(lon, 0, 'f', 4);

    QNetworkRequest request{QUrl(pointsUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader, USER_AGENT);
    request.setRawHeader("Accept", "application/geo+json");

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, lat, lon]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "WeatherManager: NWS points request failed:" << reply->errorString();
            fallbackToOpenMeteo(lat, lon, "NWS points lookup failed");
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject props = doc.object()["properties"].toObject();
        QString forecastHourlyUrl = props["forecastHourly"].toString();

        if (forecastHourlyUrl.isEmpty()) {
            qWarning() << "WeatherManager: NWS returned no forecastHourly URL";
            fallbackToOpenMeteo(lat, lon, "NWS missing forecastHourly URL");
            return;
        }

        // Step 2: Fetch the hourly forecast
        fetchNWSHourlyFromGridUrl(forecastHourlyUrl);
    });
}

void WeatherManager::fetchNWSHourlyFromGridUrl(const QString& forecastHourlyUrl)
{
    QUrl url(forecastHourlyUrl);
    QUrlQuery query(url.query());
    query.addQueryItem("units", "si");  // Get metric units
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, USER_AGENT);
    request.setRawHeader("Accept", "application/geo+json");

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        double lat = m_lastFetchLat;
        double lon = m_lastFetchLon;

        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "WeatherManager: NWS hourly request failed:" << reply->errorString();
            fallbackToOpenMeteo(lat, lon, "NWS hourly forecast failed");
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QList<HourlyForecast> forecasts = parseNWSResponse(doc);

        if (forecasts.isEmpty()) {
            qWarning() << "WeatherManager: NWS returned no hourly periods";
            fallbackToOpenMeteo(lat, lon, "NWS parsing failed");
            return;
        }

        storeForecasts(forecasts, WeatherProvider::NWS);
    });
}

QList<HourlyForecast> WeatherManager::parseNWSResponse(const QJsonDocument& doc)
{
    QList<HourlyForecast> result;
    QJsonObject root = doc.object();
    QJsonArray periods = root["properties"].toObject()["periods"].toArray();

    int count = qMin(static_cast<int>(periods.size()), MAX_HOURLY_ENTRIES);
    result.reserve(count);

    for (int i = 0; i < count; ++i) {
        QJsonObject period = periods[i].toObject();
        HourlyForecast f;

        f.time = QDateTime::fromString(period["startTime"].toString(), Qt::ISODate);

        // With units=si, temperature is in Celsius
        f.temperature = period["temperature"].toDouble();

        // Humidity
        QJsonObject humidityObj = period["relativeHumidity"].toObject();
        f.relativeHumidity = humidityObj["value"].toInt();

        // Wind speed - with units=si it's in km/h as a string like "15 km/h"
        QString windStr = period["windSpeed"].toString();
        f.windSpeed = windStr.split(' ').first().toDouble();

        // Wind direction - NWS gives cardinal (N, NE, etc.)
        f.windDirection = nwsCardinalToDirection(period["windDirection"].toString());

        // Precipitation probability
        QJsonObject precipObj = period["probabilityOfPrecipitation"].toObject();
        f.precipitationProbability = precipObj["value"].toInt();

        // Weather code from icon URL
        QString iconUrl = period["icon"].toString();
        f.weatherCode = nwsIconToWMO(iconUrl);

        // Daytime flag
        f.isDaytime = period["isDaytime"].toBool(true);

        // NWS doesn't provide these directly with the hourly endpoint
        f.apparentTemperature = f.temperature;  // Approximate
        f.precipitation = 0.0;
        f.cloudCover = 0.0;
        f.uvIndex = 0.0;

        result.append(f);
    }

    return result;
}

// ─── MET Norway (Yr.no) ─────────────────────────────────────────────────────

void WeatherManager::fetchFromMetNorway(double lat, double lon)
{
    QUrl url(QStringLiteral("https://api.met.no/weatherapi/locationforecast/2.0/compact"));
    QUrlQuery query;
    query.addQueryItem("lat", QString::number(lat, 'f', 2));
    query.addQueryItem("lon", QString::number(lon, 'f', 2));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, USER_AGENT);

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, lat, lon]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "WeatherManager: MET Norway request failed:" << reply->errorString();
            fallbackToOpenMeteo(lat, lon, "MET Norway request failed");
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QList<HourlyForecast> forecasts = parseMetNorwayResponse(doc);

        if (forecasts.isEmpty()) {
            qWarning() << "WeatherManager: MET Norway returned no timeseries data";
            fallbackToOpenMeteo(lat, lon, "MET Norway parsing failed");
            return;
        }

        storeForecasts(forecasts, WeatherProvider::MetNorway);
    });
}

QList<HourlyForecast> WeatherManager::parseMetNorwayResponse(const QJsonDocument& doc)
{
    QList<HourlyForecast> result;
    QJsonObject root = doc.object();
    QJsonArray timeseries = root["properties"].toObject()["timeseries"].toArray();

    int count = qMin(static_cast<int>(timeseries.size()), MAX_HOURLY_ENTRIES);
    result.reserve(count);

    for (int i = 0; i < count; ++i) {
        QJsonObject entry = timeseries[i].toObject();
        HourlyForecast f;

        f.time = QDateTime::fromString(entry["time"].toString(), Qt::ISODate);

        QJsonObject data = entry["data"].toObject();
        QJsonObject instant = data["instant"].toObject()["details"].toObject();

        f.temperature = instant["air_temperature"].toDouble();
        f.relativeHumidity = qRound(instant["relative_humidity"].toDouble());
        f.windSpeed = instant["wind_speed"].toDouble() * 3.6;  // m/s → km/h
        f.windDirection = qRound(instant["wind_from_direction"].toDouble());
        f.cloudCover = instant["cloud_area_fraction"].toDouble();

        // Precipitation and symbol from next_1_hours (preferred) or next_6_hours
        QJsonObject next1h = data["next_1_hours"].toObject();
        QJsonObject next6h = data["next_6_hours"].toObject();

        QString symbolCode;
        if (!next1h.isEmpty()) {
            symbolCode = next1h["summary"].toObject()["symbol_code"].toString();
            f.precipitation = next1h["details"].toObject()["precipitation_amount"].toDouble();
        } else if (!next6h.isEmpty()) {
            symbolCode = next6h["summary"].toObject()["symbol_code"].toString();
            f.precipitation = next6h["details"].toObject()["precipitation_amount"].toDouble();
        }

        f.weatherCode = metNorwaySymbolToWMO(symbolCode);
        f.apparentTemperature = f.temperature;  // MET Norway doesn't provide feels-like in compact
        f.precipitationProbability = 0;          // Not in compact format
        f.uvIndex = instant["ultraviolet_index_clear_sky"].toDouble();

        result.append(f);
    }

    return result;
}

// ─── Fallback & storage ──────────────────────────────────────────────────────

void WeatherManager::fallbackToOpenMeteo(double lat, double lon, const QString& reason)
{
    qDebug() << "WeatherManager: Falling back to Open-Meteo -" << reason;
    fetchFromOpenMeteo(lat, lon);
}

void WeatherManager::storeForecasts(const QList<HourlyForecast>& forecasts, WeatherProvider provider)
{
    m_forecasts = forecasts;
    m_provider = provider;
    m_lastUpdate = QDateTime::currentDateTime();
    m_valid = true;
    m_fetchInProgress = false;

    setLoading(false);
    emit weatherChanged();

    // Fetch accurate sunrise/sunset times to fix isDaytime
    fetchSunTimes(m_lastFetchLat, m_lastFetchLon);

    qDebug() << "WeatherManager: Stored" << forecasts.size() << "hourly forecasts from"
             << providerName() << "- current temp:"
             << (forecasts.isEmpty() ? 0.0 : forecasts.first().temperature) << "°C";
}

// ─── Sunrise/sunset from Open-Meteo ──────────────────────────────────────────

void WeatherManager::fetchSunTimes(double lat, double lon)
{
    QUrl url(QStringLiteral("https://api.open-meteo.com/v1/forecast"));
    QUrlQuery query;
    query.addQueryItem("latitude", QString::number(lat, 'f', 2));
    query.addQueryItem("longitude", QString::number(lon, 'f', 2));
    query.addQueryItem("daily", "sunrise,sunset");
    query.addQueryItem("timezone", "auto");
    query.addQueryItem("forecast_days", "4");
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, USER_AGENT);

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "WeatherManager: Sun times request failed:" << reply->errorString();
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject daily = doc.object()["daily"].toObject();
        QJsonArray sunrises = daily["sunrise"].toArray();
        QJsonArray sunsets = daily["sunset"].toArray();

        m_sunTimes.clear();
        int count = qMin(sunrises.size(), sunsets.size());
        for (int i = 0; i < count; ++i) {
            QDateTime rise = QDateTime::fromString(sunrises[i].toString(), Qt::ISODate);
            QDateTime set = QDateTime::fromString(sunsets[i].toString(), Qt::ISODate);
            m_sunTimes.append({rise, set});
        }

        qDebug() << "WeatherManager: Got sun times for" << count << "days";

        // Re-apply isDaytime to stored forecasts
        applySunTimes();
    });
}

void WeatherManager::applySunTimes()
{
    if (m_sunTimes.isEmpty() || m_forecasts.isEmpty())
        return;

    for (auto& f : m_forecasts)
        f.isDaytime = isDaytimeAt(f.time);

    emit weatherChanged();
}

bool WeatherManager::isDaytimeAt(const QDateTime& time) const
{
    for (const auto& pair : m_sunTimes) {
        if (time.date() == pair.first.date())
            return time >= pair.first && time < pair.second;
    }
    // No sun data for this day — fallback to hour
    int hour = time.time().hour();
    return hour >= 7 && hour < 19;
}

// ─── NWS icon URL → WMO weather code ────────────────────────────────────────
// NWS icon URLs look like: https://api.weather.gov/icons/land/day/skc?size=small
// The condition code is the path segment after day/ or night/ (e.g., "skc", "few", "rain")

int WeatherManager::nwsIconToWMO(const QString& iconUrl)
{
    // Extract condition from URL path: .../day/CODE or .../night/CODE
    QString condition;
    QStringList parts = iconUrl.split('/');
    for (int i = 0; i < parts.size(); ++i) {
        if ((parts[i] == "day" || parts[i] == "night") && i + 1 < parts.size()) {
            condition = parts[i + 1].split('?').first().split(',').first();
            break;
        }
    }

    if (condition.isEmpty())
        return 0;

    // NWS condition codes → WMO
    static const QHash<QString, int> map = {
        {"skc",       0},   // Clear
        {"few",       1},   // Few clouds
        {"sct",       2},   // Scattered clouds
        {"bkn",       3},   // Broken clouds (overcast)
        {"ovc",       3},   // Overcast
        {"wind_skc",  0},   // Clear + windy
        {"wind_few",  1},   // Few clouds + windy
        {"wind_sct",  2},   // Scattered + windy
        {"wind_bkn",  3},   // Broken + windy
        {"wind_ovc",  3},   // Overcast + windy
        {"fog",       45},  // Fog
        {"haze",      45},  // Haze (treat as fog)
        {"smoke",     45},  // Smoke
        {"dust",      45},  // Dust
        {"rain",      61},  // Rain
        {"rain_showers",     80},  // Rain showers
        {"rain_showers_hi",  80},  // Rain showers (high chance)
        {"tsra",      95},  // Thunderstorm
        {"tsra_sct",  95},  // Scattered thunderstorms
        {"tsra_hi",   95},  // Thunderstorm (high chance)
        {"snow",      71},  // Snow
        {"rain_snow", 67},  // Rain/snow mix (freezing rain)
        {"rain_sleet",66},  // Rain/sleet
        {"snow_sleet",77},  // Snow/sleet
        {"fzra",      66},  // Freezing rain
        {"rain_fzra", 66},  // Rain/freezing rain
        {"snow_fzra", 77},  // Snow/freezing rain
        {"sleet",     77},  // Sleet
        {"blizzard",  75},  // Blizzard
        {"cold",      0},   // Cold (no WMO equivalent)
        {"hot",       0},   // Hot (no WMO equivalent)
    };

    return map.value(condition, 0);
}

// ─── MET Norway symbol_code → WMO weather code ──────────────────────────────
// Symbol codes like "clearsky_day", "rain", "heavysnow" etc.
// Strip _day/_night/_polartwilight suffix, then map base symbol

int WeatherManager::metNorwaySymbolToWMO(const QString& symbolCode)
{
    // Strip time-of-day suffix
    QString base = symbolCode;
    base.remove("_day").remove("_night").remove("_polartwilight");

    static const QHash<QString, int> map = {
        {"clearsky",               0},
        {"fair",                   1},
        {"partlycloudy",           2},
        {"cloudy",                 3},
        {"fog",                    45},
        {"lightrain",              61},
        {"rain",                   63},
        {"heavyrain",              65},
        {"lightrainshowers",       80},
        {"rainshowers",            80},
        {"heavyrainshowers",       82},
        {"lightsleet",             66},
        {"sleet",                  66},
        {"heavysleet",             67},
        {"lightsleetshowers",      66},
        {"sleetshowers",           66},
        {"heavysleetshowers",      67},
        {"lightsnow",              71},
        {"snow",                   73},
        {"heavysnow",              75},
        {"lightsnowshowers",       85},
        {"snowshowers",            85},
        {"heavysnowshowers",       86},
        {"lightrainandthunder",    95},
        {"rainandthunder",         95},
        {"heavyrainandthunder",    95},
        {"lightrainshowersandthunder",  95},
        {"rainshowersandthunder",       96},
        {"heavyrainshowersandthunder",  96},
        {"lightsleetandthunder",   95},
        {"sleetandthunder",        95},
        {"heavysleetandthunder",   95},
        {"lightsleetshowersandthunder", 95},
        {"sleetshowersandthunder",      95},
        {"heavysleetshowersandthunder", 95},
        {"lightsnowandthunder",    95},
        {"snowandthunder",         95},
        {"heavysnowandthunder",    95},
        {"lightsnowshowersandthunder",  95},
        {"snowshowersandthunder",       95},
        {"heavysnowshowersandthunder",  95},
    };

    return map.value(base, 0);
}

// ─── WMO weather code → human description ───────────────────────────────────

QString WeatherManager::weatherDescription(int wmoCode)
{
    switch (wmoCode) {
    case 0:  return tr("Clear sky");
    case 1:  return tr("Mainly clear");
    case 2:  return tr("Partly cloudy");
    case 3:  return tr("Overcast");
    case 45: return tr("Fog");
    case 48: return tr("Depositing rime fog");
    case 51: return tr("Light drizzle");
    case 53: return tr("Moderate drizzle");
    case 55: return tr("Dense drizzle");
    case 56: return tr("Light freezing drizzle");
    case 57: return tr("Dense freezing drizzle");
    case 61: return tr("Slight rain");
    case 63: return tr("Moderate rain");
    case 65: return tr("Heavy rain");
    case 66: return tr("Light freezing rain");
    case 67: return tr("Heavy freezing rain");
    case 71: return tr("Slight snow");
    case 73: return tr("Moderate snow");
    case 75: return tr("Heavy snow");
    case 77: return tr("Snow grains");
    case 80: return tr("Slight rain showers");
    case 81: return tr("Moderate rain showers");
    case 82: return tr("Violent rain showers");
    case 85: return tr("Slight snow showers");
    case 86: return tr("Heavy snow showers");
    case 95: return tr("Thunderstorm");
    case 96: return tr("Thunderstorm with slight hail");
    case 99: return tr("Thunderstorm with heavy hail");
    default: return tr("Unknown");
    }
}

// ─── WMO weather code → icon name ───────────────────────────────────────────
// Returns a semantic icon name that QML can map to actual icons/emojis

QString WeatherManager::weatherIconName(int wmoCode)
{
    if (wmoCode == 0)                          return QStringLiteral("clear");
    if (wmoCode >= 1 && wmoCode <= 2)          return QStringLiteral("partly-cloudy");
    if (wmoCode == 3)                          return QStringLiteral("overcast");
    if (wmoCode == 45 || wmoCode == 48)        return QStringLiteral("fog");
    if (wmoCode >= 51 && wmoCode <= 57)        return QStringLiteral("drizzle");
    if (wmoCode >= 61 && wmoCode <= 65)        return QStringLiteral("rain");
    if (wmoCode >= 66 && wmoCode <= 67)        return QStringLiteral("freezing-rain");
    if (wmoCode >= 71 && wmoCode <= 77)        return QStringLiteral("snow");
    if (wmoCode >= 80 && wmoCode <= 82)        return QStringLiteral("showers");
    if (wmoCode >= 85 && wmoCode <= 86)        return QStringLiteral("snow-showers");
    if (wmoCode >= 95)                         return QStringLiteral("thunderstorm");
    return QStringLiteral("unknown");
}
