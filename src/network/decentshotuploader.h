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
// ShotUploads decides when and makes the attempts; this builds and sends one.
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
        Failed,          // transient on every attempt (responseOutcome, ShotUploads::kAttempts)
    };
    // What a result means to ShotUploads (D15).
    static Outcome outcome(Result result);
    Q_ENUM(Result)

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
    // A first upload needs the connected machine's serial; an update goes to the one stored.
    bool backgroundSendReady(Send how) const override {
        return how == Send::UpdateOnly || (m_machineIdentity && !m_machineIdentity().serialNumber.isEmpty());
    }
    // The shared minimum shot length (SettingsUpload::minDuration), read when an upload starts.
    void setMinDurationProvider(std::function<double()> provider) { m_minDuration = std::move(provider); }

    bool uploading() const { return m_uploading; }
    qint64 lastShotId() const { return m_lastShotId; }
    Result lastResult() const { return m_lastResult; }
    int lastHttpStatus() const { return m_lastHttpStatus; }
    QString lastSerial() const { return m_lastSerial; }

    QString name() const override { return QStringLiteral("decent"); }
    bool isActive() const override;
    bool holdsShot(QSqlDatabase& db, qint64 shotId) const override;
    QString heldCondition() const override { return QStringLiteral("decent_uploaded_at IS NOT NULL"); }
    QString unsentEditCondition() const override { return QStringLiteral("decent_replace_pending = 1"); }
    // An already-uploaded shot is re-sent with ?replace=1 under the serial it was
    // first uploaded with; a rejected shot is tried again.
    void attemptSavedShot(qint64 shotId, Send how) override;
    // Publishes the send's result (lastResult, uploadFinished).
    void sendFinished(qint64 shotId, Attempt last) override;
    // Persists that an uploaded shot's edit still has to reach Decent, so it
    // survives uploads being off, offline or signed out (Upload missing shots sends it).
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
        bool gzipped = false;  // body is gzip, sent with Content-Encoding: gzip
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
    // Ends this attempt with `result`; `why` is logged if the send ends on it.
    void endAttempt(Result result, int httpStatus = 0, const QString& why = QString());
    void setUploading(bool uploading);
    static void writeDebugFile(const QString& name, const QByteArray& content);

    QNetworkAccessManager* m_network;
    DecentAccount* m_account;
    ShotHistoryStorage* m_storage;
    std::function<DecentMachineIdentity()> m_machineIdentity;
    std::function<double()> m_minDuration;

    // From a send's first attempt until ShotUploads ends it, retry waits included.
    bool m_uploading = false;
    // The shot was edited while this attempt was out, so the edit stays pending.
    bool m_editedInFlight = false;
    Prepared m_current;
    Result m_attemptResult = Result::None;
    int m_attemptStatus = 0;
    QString m_attemptWhy;
    // A non-API answer's body is logged once per send, not per attempt.
    bool m_loggedOddAnswer = false;

    qint64 m_lastShotId = 0;
    Result m_lastResult = Result::None;
    int m_lastHttpStatus = 0;
    QString m_lastSerial;
    // Guards the result posted back from the DB worker, as ShotHistoryStorage does.
    std::shared_ptr<std::atomic<bool>> m_destroyed = std::make_shared<std::atomic<bool>>(false);
};
