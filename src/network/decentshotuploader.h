#pragma once

#include "decentshotrecord.h"
#include "shotuploaddestination.h"

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>
#include <atomic>
#include <functional>
#include <memory>

class DecentAccount;
class QNetworkAccessManager;
class QNetworkReply;
class ShotHistoryStorage;

// The Decent account as an upload destination (POST /support/api/shot_upload).
// ShotUploads decides when; this builds and sends one shot at a time.
class DecentShotUploader : public QObject, public ShotUploadDestination {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("DecentShotUploader is created in C++ and reached via MainController")

    Q_PROPERTY(bool uploading READ uploading NOTIFY uploadingChanged FINAL)
    Q_PROPERTY(qint64 lastShotId READ lastShotId NOTIFY lastResultChanged FINAL)
    Q_PROPERTY(Result lastResult READ lastResult NOTIFY lastResultChanged FINAL)
    Q_PROPERTY(int lastHttpStatus READ lastHttpStatus NOTIFY lastResultChanged FINAL)
    Q_PROPERTY(QString lastSerial READ lastSerial NOTIFY lastResultChanged FINAL)

public:
    enum class Result {
        None,
        Uploaded,        // 2xx, including the server's duplicate answer to a first upload
        NotReplaced,     // a replace answered "duplicate": the server kept its earlier copy
        Maintenance,     // a cleaning or descaling record (uploadIneligibility)
        TooShort,        // shorter than the shared minimum length
        NotLinked,       // no linked account
        NoMachine,       // first upload with no DE1 connected
        NotFound,        // the shot could not be loaded
        Rejected,        // permanent 4xx; recorded on the shot
        NeedsSignIn,     // the stored credentials were refused (401), now or earlier
        NotRegistered,   // 403: the serial is not in the account
        Failed,          // a transient response (classify) on every attempt
    };
    Q_ENUM(Result)

    // What one HTTP response means, by Decaid's classes.
    enum class ResponseClass { Success, Transient, AuthFailed, NotRegistered, Permanent };
    static ResponseClass classify(int httpStatus, bool transportError);

    static constexpr int kAttempts = 3;
    // decentespresso.com took 25-29 s to answer on 2026-10-04.
    static constexpr int kUploadTimeoutMs = 60000;

    DecentShotUploader(QNetworkAccessManager* network, DecentAccount* account,
                       ShotHistoryStorage* storage, QObject* parent = nullptr);
    ~DecentShotUploader() override { *m_destroyed = true; }

    // The connected real machine, read when an upload starts; an empty serial
    // means none (disconnected). The simulator reports DE1Device::kSimulatedSerial.
    void setMachineIdentityProvider(std::function<DecentMachineIdentity()> provider) {
        m_machineIdentity = std::move(provider);
    }
    // The shared minimum shot length (SettingsUpload::minDuration), read when an upload starts.
    void setMinDurationProvider(std::function<double()> provider) { m_minDuration = std::move(provider); }
    // First retry waits this long, the second twice as long (Decaid: 2 s, 4 s).
    void setRetryDelayMs(int ms) { m_retryDelayMs = ms; }

    bool uploading() const { return m_uploading; }
    qint64 lastShotId() const { return m_lastShotId; }
    Result lastResult() const { return m_lastResult; }
    int lastHttpStatus() const { return m_lastHttpStatus; }
    QString lastSerial() const { return m_lastSerial; }

    QString name() const override { return QStringLiteral("decent"); }
    bool isActive() const override;
    bool busy() const override { return m_uploading; }
    // An already-uploaded shot is re-sent with ?replace=1 under the serial it was
    // first uploaded with; a rejected shot is tried again.
    void sendSavedShot(qint64 shotId, Send how) override;
    // Persists that an uploaded shot's edit still has to reach Decent, so it
    // survives uploads being off, offline or signed out (the Stage 3 drain sends it).
    void noteEdited(qint64 shotId) override;

    // The shot's page in the Decent account.
    Q_INVOKABLE static QString shotViewUrl(const QString& serial, const QString& serverShotId);

signals:
    void uploadingChanged();
    void lastResultChanged();
    void uploadFinished(qint64 shotId, DecentShotUploader::Result result);

private:
    struct Prepared {
        qint64 shotId = 0;
        QByteArray body;
        QString uuid;
        QString serial;
        bool replace = false;
        bool skip = false;  // an UpdateOnly for a shot not in the account
        Result error = Result::None;
        QString failure;  // for the log when error is NotFound
    };
    void onPrepared(const Prepared& prepared);
    void send();
    void onReplyFinished(QNetworkReply* reply);
    void finish(Result result, int httpStatus = 0);
    static void writeDebugFile(const QString& name, const QByteArray& content);

    QNetworkAccessManager* m_network;
    DecentAccount* m_account;
    ShotHistoryStorage* m_storage;
    std::function<DecentMachineIdentity()> m_machineIdentity;
    std::function<double()> m_minDuration;
    int m_retryDelayMs = 2000;

    bool m_uploading = false;
    Prepared m_current;
    int m_attempt = 0;

    qint64 m_lastShotId = 0;
    Result m_lastResult = Result::None;
    int m_lastHttpStatus = 0;
    QString m_lastSerial;
    // Guards the result posted back from the DB worker, as ShotHistoryStorage does.
    std::shared_ptr<std::atomic<bool>> m_destroyed = std::make_shared<std::atomic<bool>>(false);
};
