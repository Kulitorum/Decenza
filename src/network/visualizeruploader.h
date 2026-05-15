#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QVector>
#include <QPointF>

#include "../history/shotprojection.h"

class ShotDataModel;
class Settings;
class Profile;
class DE1Device;

// DYE (Describe Your Espresso) metadata for shot uploads
struct ShotMetadata {
    QString beanBrand;
    QString beanType;
    QString roastDate;      // ISO format: YYYY-MM-DD
    QString roastLevel;     // Light, Medium, Dark
    QString grinderBrand;
    QString grinderModel;
    QString grinderBurrs;
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
    explicit VisualizerUploader(QNetworkAccessManager* networkManager, Settings* settings, QObject* parent = nullptr);

    bool isUploading() const { return m_uploading; }
    QString lastUploadStatus() const { return m_lastUploadStatus; }
    QString lastShotUrl() const { return m_lastShotUrl; }

    void setDevice(DE1Device* device) { m_device = device; }

    // Upload shot data to visualizer.coffee.
    // `dbShotId` is the local shots.id this upload is for, so a successful
    // upload can persist its returned Visualizer id to the right row from
    // C++ (MainController) without depending on any UI page. Live shots
    // are saved on a separate async path, so the caller passes the id it
    // captured from shotSaved.
    Q_INVOKABLE void uploadShot(ShotDataModel* shotData,
                                 const Profile* profile,
                                 double duration,
                                 double finalWeight = 0,
                                 double doseWeight = 0,
                                 const ShotMetadata& metadata = ShotMetadata(),
                                 const QString& debugLog = QString(),
                                 qint64 shotEpoch = 0,
                                 qint64 dbShotId = 0);

    // Upload a shot from history (takes the typed projection from
    // ShotHistoryStorage::convertShotRecord).
    Q_INVOKABLE void uploadShotFromHistory(const ShotProjection& shotData);

    // Upload with metadata overrides applied on top of a base ShotProjection.
    // Use from QML instead of Object.assign({}, editShotData, overrides): V4 can
    // pass a QQmlValueTypeWrapper as ShotProjection, but Object.assign on a
    // Q_GADGET yields a plain object that omits id/durationSec/frames, causing
    // isValid() to fail silently with no UI feedback.
    Q_INVOKABLE void uploadShotFromHistoryWithOverrides(
        const ShotProjection& baseShot, const QVariantMap& overrides);

    // Update metadata on an already-uploaded shot (PATCH to visualizer.coffee)
    Q_INVOKABLE void updateShotOnVisualizer(const QString& visualizerId, const ShotProjection& shotData);

    // PATCH with overrides applied on top of a base ShotProjection.
    // Pass the Q_GADGET directly so fields not present in overrides (notably profileName)
    // are taken from the original shot record rather than left empty.
    Q_INVOKABLE void updateShotOnVisualizerWithOverrides(
        const QString& visualizerId,
        const ShotProjection& baseShot,
        const QVariantMap& overrides);

    // Test connection with current credentials
    Q_INVOKABLE void testConnection();

    // One-time reconciliation support: fetch the user's shot list
    // (GET /api/shots, paged) for shots whose start time (clock) is at
    // or after windowStartEpoch. Emits shotListFetched with a
    // QVariantList of {visualizerId, url, clockEpoch} maps, or
    // shotListFailed on any HTTP/parse error (fail safe — caller does
    // not advance its run-once flag on failure).
    void fetchShotListSince(qint64 windowStartEpoch);

    // Build a visualizer-compatible JSON payload from a ShotProjection.
    // Thread-safe; does not touch instance state. Reused by ShotHistoryExporter.
    static QByteArray buildHistoryShotJson(const ShotProjection& shotData);

signals:
    void uploadingChanged();
    void lastUploadStatusChanged();
    void lastShotUrlChanged();
    void uploadSuccess(const QString& shotId, const QString& url);
    // Like uploadSuccess but carries the originating local shots.id so
    // MainController can persist the link without a UI page. dbShotId is
    // 0 when the upload had no associated local row (should not happen
    // on the normal paths).
    void uploadSucceededForShot(qint64 dbShotId, const QString& visualizerId, const QString& url);
    void updateSuccess(const QString& visualizerId);
    void uploadFailed(const QString& error);
    void connectionTestResult(bool success, const QString& message);
    // Reconciliation list fetch results.
    void shotListFetched(const QVariantList& shots);
    void shotListFailed(const QString& error);

private slots:
    void onUploadFinished(QNetworkReply* reply);
    void onUpdateFinished(QNetworkReply* reply, const QString& visualizerId);
    void onTestFinished(QNetworkReply* reply);

private:
    QByteArray buildShotJson(ShotDataModel* shotData,
                             const Profile* profile,
                             double finalWeight,
                             double doseWeight,
                             const ShotMetadata& metadata,
                             const QString& debugLog,
                             qint64 shotEpoch = 0);

    QJsonObject buildVisualizerProfileJson(const Profile* profile);
    QByteArray buildMultipartData(const QByteArray& jsonData, const QString& boundary);
    QString authHeader() const;

    static QJsonObject buildAppInfoJson();
    static QJsonObject buildProfileSettings(const Profile* profile);
    bool validateUpload(const QString& beverageType, double duration);
    void sendUpload(const QByteArray& jsonData);

    // Paged reconciliation fetch: accumulates results across pages,
    // recurses until older than m_reconcileWindowStartEpoch or paging
    // exhausted, then emits shotListFetched once.
    void fetchShotListPage(int page, qint64 windowStartEpoch, QVariantList accumulated);

    Settings* m_settings;
    QNetworkAccessManager* m_networkManager;
    DE1Device* m_device = nullptr;
    bool m_uploading = false;
    QString m_lastUploadStatus;
    QString m_lastShotUrl;
    // The local shots.id the in-flight upload is for; emitted with
    // uploadSucceededForShot. Uploads are strictly serial (m_uploading
    // guards), so a single member suffices. Reset after each terminal
    // outcome.
    qint64 m_uploadingDbShotId = 0;

    static constexpr const char* VISUALIZER_API_URL = "https://visualizer.coffee/api/shots/upload";
    static constexpr const char* VISUALIZER_SHOTS_API_URL = "https://visualizer.coffee/api/shots/";
    static constexpr const char* VISUALIZER_SHOT_URL = "https://visualizer.coffee/shots/";
};
