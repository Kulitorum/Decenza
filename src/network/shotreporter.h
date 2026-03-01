#pragma once

#include <QObject>
#include <QNetworkAccessManager>

class LocationProvider;
class Settings;

// Shot event data for the shot map API
struct ShotEvent {
    QString city;
    QString countryCode;
    double latitude = 0.0;
    double longitude = 0.0;
    QString profileName;
    QString softwareName;
    QString softwareVersion;
    QString machineModel;
    qint64 timestampMs = 0;
};

class ShotReporter : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool hasLocation READ hasLocation NOTIFY locationStatusChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString manualCity READ manualCity WRITE setManualCity NOTIFY manualCityChanged)
    Q_PROPERTY(bool usingManualCity READ usingManualCity NOTIFY locationStatusChanged)
    Q_PROPERTY(double latitude READ latitude NOTIFY locationStatusChanged)
    Q_PROPERTY(double longitude READ longitude NOTIFY locationStatusChanged)

public:
    explicit ShotReporter(QNetworkAccessManager* networkManager, Settings* settings,
                          LocationProvider* locationProvider, QObject* parent = nullptr);

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    bool hasLocation() const;
    QString lastError() const { return m_lastError; }

    // Report a shot to the shot map
    // Uses current location from LocationProvider
    Q_INVOKABLE void reportShot(const QString& profileName, const QString& machineModel);

    // Request location update (call this at app start or when settings change)
    Q_INVOKABLE void refreshLocation();

    // Open Android Location Settings (for user to enable GPS)
    Q_INVOKABLE void openLocationSettings();

    // Check if GPS is enabled at system level
    Q_INVOKABLE bool isGpsEnabled() const;

    // Get location info for display
    Q_INVOKABLE QString currentCity() const;
    Q_INVOKABLE QString currentCountryCode() const;

    // Manual city fallback
    QString manualCity() const;
    void setManualCity(const QString& city);
    bool usingManualCity() const;

    // Get current coordinates (GPS or manual)
    double latitude() const;
    double longitude() const;

signals:
    void manualCityChanged();
    void enabledChanged();
    void locationStatusChanged();
    void lastErrorChanged();
    void shotReported(const QString& eventId);
    void shotReportFailed(const QString& error);

private slots:
    void onReplyFinished(QNetworkReply* reply);
    void onLocationChanged();
    void onLocationError(const QString& error);

private:
    void sendShotEvent(const ShotEvent& event);

    Settings* m_settings;
    LocationProvider* m_locationProvider;
    QNetworkAccessManager* m_networkManager;

    bool m_enabled = false;
    QString m_lastError;
    bool m_hasPromptedForLocation = false;  // Only prompt once per session

    static constexpr const char* API_URL = "https://api.decenza.coffee/v1/shots";
};
