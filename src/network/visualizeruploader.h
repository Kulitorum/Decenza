#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QVector>
#include <QPointF>

class ShotDataModel;
class Settings;
class Profile;

// DYE (Describe Your Espresso) metadata for shot uploads
struct ShotMetadata {
    QString beanBrand;
    QString beanType;
    QString roastDate;      // ISO format: YYYY-MM-DD
    QString roastLevel;     // Light, Medium, Dark
    QString grinderModel;
    QString grinderSetting;
    double beanWeight = 0;  // Dose weight in grams
    double drinkWeight = 0; // Output weight in grams
    double drinkTds = 0;
    double drinkEy = 0;
    int espressoEnjoyment = 0;  // 0-100
    QString espressoNotes;
    QString barista;
};

class VisualizerUploader : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool uploading READ isUploading NOTIFY uploadingChanged)
    Q_PROPERTY(QString lastUploadStatus READ lastUploadStatus NOTIFY lastUploadStatusChanged)
    Q_PROPERTY(QString lastShotUrl READ lastShotUrl NOTIFY lastShotUrlChanged)

public:
    explicit VisualizerUploader(Settings* settings, QObject* parent = nullptr);

    bool isUploading() const { return m_uploading; }
    QString lastUploadStatus() const { return m_lastUploadStatus; }
    QString lastShotUrl() const { return m_lastShotUrl; }

    // Upload shot data to visualizer.coffee
    Q_INVOKABLE void uploadShot(ShotDataModel* shotData,
                                 const Profile* profile,
                                 double duration,
                                 double finalWeight = 0,
                                 double doseWeight = 0,
                                 const ShotMetadata& metadata = ShotMetadata());

    // Upload a shot from history (takes QVariantMap from ShotHistoryStorage::getShot())
    Q_INVOKABLE void uploadShotFromHistory(const QVariantMap& shotData);

    // Test connection with current credentials
    Q_INVOKABLE void testConnection();

signals:
    void uploadingChanged();
    void lastUploadStatusChanged();
    void lastShotUrlChanged();
    void uploadSuccess(const QString& shotId, const QString& url);
    void uploadFailed(const QString& error);
    void connectionTestResult(bool success, const QString& message);

private slots:
    void onUploadFinished(QNetworkReply* reply);
    void onTestFinished(QNetworkReply* reply);

private:
    QByteArray buildShotJson(ShotDataModel* shotData,
                             const Profile* profile,
                             double finalWeight,
                             double doseWeight,
                             const ShotMetadata& metadata);

    QJsonObject buildVisualizerProfileJson(const Profile* profile);
    QByteArray buildMultipartData(const QByteArray& jsonData, const QString& boundary);
    QString authHeader() const;

    Settings* m_settings;
    QNetworkAccessManager* m_networkManager;
    bool m_uploading = false;
    QString m_lastUploadStatus;
    QString m_lastShotUrl;

    static constexpr const char* VISUALIZER_API_URL = "https://visualizer.coffee/api/shots/upload";
    static constexpr const char* VISUALIZER_SHOT_URL = "https://visualizer.coffee/shots/";
};
