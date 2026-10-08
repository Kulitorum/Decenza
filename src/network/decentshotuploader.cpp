#include "decentshotuploader.h"
#include "core/gzip.h"

#include "core/dbutils.h"
#include "core/diagnosticlogging.h"
#include "history/shothistorystorage.h"
#include "network/decentaccount.h"
#include "network/shotpayloadhelpers.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

DecentShotUploader::DecentShotUploader(QNetworkAccessManager* network, DecentAccount* account,
                                       ShotHistoryStorage* storage, QObject* parent)
    : QObject(parent)
    , m_network(network)
    , m_account(account)
    , m_storage(storage)
{
}

ShotUploadDestination::Outcome DecentShotUploader::outcome(Result result) {
    switch (result) {
    // A replace answered "duplicate" reached the account, which kept its copy;
    // the edit stays pending (replace-pending), so it is not a failure.
    case Result::Uploaded:
    case Result::NotReplaced: return Outcome::Sent;
    case Result::Failed: return Outcome::Transient;
    case Result::Rejected: return Outcome::Rejected;
    case Result::NeedsSignIn: return Outcome::AuthFailed;
    case Result::NotRegistered: return Outcome::AccountRefused;
    case Result::None:
    case Result::Maintenance:
    case Result::TooShort:
    case Result::NotLinked:
    case Result::NoMachine:
    case Result::NoSerial:
    case Result::NotFound: return Outcome::NothingToSend;
    }
    return Outcome::NothingToSend;
}

QString DecentShotUploader::shotViewUrl(const QString& serial, const QString& serverShotId) {
    if (serial.isEmpty() || serverShotId.isEmpty()) return QString();
    QUrl url(QString::fromLatin1(DecentAccount::kBaseUrl) + QString::fromLatin1(DecentAccount::kAccountPath));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("view"), QStringLiteral("chart"));
    query.addQueryItem(QStringLiteral("sn"), serial);
    query.addQueryItem(QStringLiteral("id"), serverShotId);
    url.setQuery(query);
    return url.toString();
}

bool DecentShotUploader::isActive() const {
    return m_account->uploadsActive();
}

bool DecentShotUploader::holdsShot(QSqlDatabase& db, qint64 shotId) const {
    DecentUploadState state;
    return ShotHistoryStorage::loadDecentUploadStateStatic(db, shotId, &state) && state.uploaded();
}

void DecentShotUploader::noteEdited(qint64 shotId) {
    // An edit during this shot's own upload may not be in the body that went out.
    if (m_uploading && m_current.shotId == shotId) m_editedInFlight = true;
    m_storage->requestMarkDecentReplacePending(shotId);
}

void DecentShotUploader::setUploading(bool uploading) {
    if (m_uploading == uploading) return;
    m_uploading = uploading;
    emit uploadingChanged();
}

void DecentShotUploader::attemptSavedShot(qint64 shotId, Send how) {
    // Each attempt reads the row again, so it carries every edit saved before it.
    m_editedInFlight = false;
    m_current = Prepared{};
    m_current.shotId = shotId;
    setUploading(true);

    if (m_account->state() != DecentAccount::State::Linked) {
        endAttempt(m_account->state() == DecentAccount::State::NeedsSignIn ? Result::NeedsSignIn : Result::NotLinked);
        return;
    }

    const DecentMachineIdentity connected = m_machineIdentity ? m_machineIdentity() : DecentMachineIdentity{};
    const double minDuration = m_minDuration ? m_minDuration() : 0.0;
    const QString dbPath = m_storage->databasePath();
    auto destroyed = m_destroyed;
    const bool updateOnly = how == Send::UpdateOnly;
    m_storage->runAfterQueuedWrites([this, destroyed, dbPath, shotId, connected, minDuration, updateOnly]() {
        Prepared p;
        p.shotId = shotId;
        p.error = Result::NotFound;
        p.failure = QStringLiteral("could not open the shot database");
        withTempDb(dbPath, "decent_upload", [&](QSqlDatabase& db) {
            const ShotRecord record = ShotHistoryStorage::loadShotRecordStatic(db, shotId, nullptr, Q_FUNC_INFO);
            if (record.summary.id <= 0) { p.failure = QStringLiteral("no such shot"); return; }
            DecentUploadState state;
            if (!ShotHistoryStorage::loadDecentUploadStateStatic(db, shotId, &state)) {
                p.failure = QStringLiteral("its Decent upload state could not be read");
                return;
            }
            if (updateOnly && !state.uploaded()) {
                p.error = Result::None;
                p.skip = true;
                return;
            }
            const ShotProjection shot = ShotHistoryStorage::convertShotRecord(record);
            switch (uploadIneligibility(shot, minDuration)) {
            case UploadIneligible::Maintenance: p.error = Result::Maintenance; return;
            case UploadIneligible::TooShort: p.error = Result::TooShort; return;
            case UploadIneligible::None: break;
            }
            if (!shot.profileJson.isEmpty() && !QJsonDocument::fromJson(shot.profileJson.toUtf8()).isObject()) {
                p.failure = QStringLiteral("its stored profile is unreadable");
                return;
            }

            // A replacement goes to the machine the shot was first filed under.
            DecentMachineIdentity machine = connected;
            if (state.uploaded() && !state.serial.isEmpty()) {
                p.replace = true;
                if (machine.serialNumber != state.serial) machine = DecentMachineIdentity{state.serial, {}, {}};
            } else if (connected.serialNumber.isEmpty()) {
                p.error = connected.serialUnreported ? Result::NoSerial : Result::NoMachine;
                return;
            }
            p.serial = machine.serialNumber;
            p.uuid = shot.uuid;
            p.body = DecentShotRecord::build(shot, machine);
            p.error = Result::None;
        });
        if (p.error == Result::None && !p.skip) {
            writeDebugFile(QStringLiteral("last_decent_upload.json"), QJsonDocument::fromJson(p.body).toJson(QJsonDocument::Indented));
            // Decent asked for gzip uploads. A body that cannot be compressed goes plain.
            if (QByteArray gzipped = Gzip::compress(p.body); !gzipped.isEmpty()) {
                p.body = std::move(gzipped);
                p.gzipped = true;
            }
        }
        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, destroyed, p]() {
            if (!*destroyed) onPrepared(p);
        }, Qt::QueuedConnection);
    });
}

void DecentShotUploader::onPrepared(const Prepared& prepared) {
    m_current = prepared;
    if (prepared.skip) {
        endAttempt(Result::None);
        return;
    }
    if (prepared.error != Result::None) {
        endAttempt(prepared.error);
        return;
    }
    send();
}

void DecentShotUploader::send() {
    QUrl url(QString::fromLatin1(DecentAccount::kBaseUrl) + QStringLiteral("/support/api/shot_upload"));
    if (m_current.replace) url.setQuery(QStringLiteral("replace=1"));
    QNetworkRequest request(url);
    // Exactly what Decaid's proxy and Decent's API docs send. JSON is UTF-8 by
    // definition (RFC 8259), so a charset parameter adds nothing.
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (m_current.gzipped) request.setRawHeader("Content-Encoding", "gzip");
    request.setTransferTimeout(kUploadTimeoutMs);
    if (!m_account->applyAuth(request)) {
        // Signed out or refused while the row was being read.
        endAttempt(m_account->state() == DecentAccount::State::NeedsSignIn ? Result::NeedsSignIn : Result::NotLinked);
        return;
    }
    QNetworkReply* reply = m_network->post(request, m_current.body);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onReplyFinished(reply); });
}

// A reply excerpt on one log line, with its size: a body of only "\n" logged
// raw printed as a blank second line.
static QString bodyForLog(const QByteArray& body) {
    return QStringLiteral("(%1 bytes) %2").arg(body.size()).arg(QString::fromUtf8(body.left(300)).simplified());
}

void DecentShotUploader::onReplyFinished(QNetworkReply* reply) {
    reply->deleteLater();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool transportError = reply->error() != QNetworkReply::NoError && status == 0;
    const QByteArray body = reply->readAll();
    const qint64 shotId = m_current.shotId;
    writeDebugFile(QStringLiteral("last_decent_upload_response.txt"),
                   "POST " + reply->url().toEncoded() + "\nHTTP " + QByteArray::number(status) + "\n\n" + body);

    const QJsonObject json = QJsonDocument::fromJson(body).object();
    Outcome answer = responseOutcome(status, transportError);
    QString why = status == 0 ? reply->errorString() : QStringLiteral("HTTP %1").arg(status);
    // A 2xx that is not the API's {"ok":true,...} — a captive portal's page, a
    // proxy, a changed API — has not stored anything.
    if (answer == Outcome::Sent && !json.value(QStringLiteral("ok")).toBool()) {
        why = QStringLiteral("HTTP %1 that is not the upload API's answer").arg(status);
        if (!m_loggedOddAnswer)
            DIAG_WARN(DECENT, "DecentShotUploader") << "shot" << shotId << why << bodyForLog(body);
        m_loggedOddAnswer = true;
        answer = Outcome::Transient;
    }

    switch (answer) {
    case Outcome::Sent: {
        QString serverId = json.value(QStringLiteral("id")).toString();
        if (serverId.isEmpty()) serverId = m_current.uuid;
        const bool duplicate = json.value(QStringLiteral("duplicate")).toBool();
        if (m_current.replace && duplicate) {
            // Seen from decentespresso.com on 2026-10-04 with ?replace=1 sent exactly as
            // its API docs and Decaid send it: the edit is not stored. Keep it pending.
            m_storage->requestMarkDecentReplacePending(shotId);
            DIAG_WARN(DECENT, "DecentShotUploader") << QStringLiteral(
                "shot %1: Decent answered a replace with \"duplicate\" and kept its earlier copy (serial %2, server id %3)")
                .arg(QString::number(shotId), m_current.serial, serverId);
            endAttempt(Result::NotReplaced, status);
            return;
        }
        m_storage->requestRecordDecentUpload(shotId, serverId, m_current.serial, m_editedInFlight);
        const QString how = duplicate ? QStringLiteral(" (already on the server)")
                            : m_current.replace ? QStringLiteral(" (replace)") : QString();
        // Logged by sendFinished(), which knows whether Upload missing shots sent it.
        m_uploadedNote = QStringLiteral("uploaded%1, serial %2, server id %3").arg(how, m_current.serial, serverId);
        endAttempt(Result::Uploaded, status);
        return;
    }
    case Outcome::NothingToSend:  // not an HTTP answer
    case Outcome::Transient:
        DIAG_DEBUG(DECENT, "DecentShotUploader") << "shot" << shotId << QStringLiteral("attempt failed (%1)").arg(why);
        endAttempt(Result::Failed, status, why);
        return;
    case Outcome::AuthFailed:
        m_account->reportAuthFailure();
        // Unlinked while the request was out: there is nothing to sign in to.
        endAttempt(m_account->state() == DecentAccount::State::NeedsSignIn ? Result::NeedsSignIn : Result::NotLinked, status);
        return;
    case Outcome::AccountRefused:
        DIAG_WARN(DECENT, "DecentShotUploader") << "serial" << m_current.serial
                                                << "is not registered to the linked Decent account (HTTP 403)";
        endAttempt(Result::NotRegistered, status);
        return;
    case Outcome::Rejected:
        DIAG_WARN(DECENT, "DecentShotUploader") << "shot" << shotId << "rejected (HTTP" << status << "):"
                                                << bodyForLog(body);
        endAttempt(Result::Rejected, status);
        return;
    }
}

void DecentShotUploader::writeDebugFile(const QString& name, const QByteArray& content) {
    QFile file(uploadDebugFilePath(name));
    if (file.open(QIODevice::WriteOnly) && file.write(content) == content.size()) return;
    // A stale file from an earlier upload would be read as this one's.
    DIAG_DEBUG(DECENT, "DecentShotUploader") << "could not write" << name << file.errorString();
    file.close();
    QFile::remove(file.fileName());
}

void DecentShotUploader::endAttempt(Result result, int httpStatus, const QString& why) {
    m_attemptResult = result;
    m_attemptStatus = httpStatus;
    m_attemptWhy = why;
    finishAttempt({outcome(result), httpStatus});
}

void DecentShotUploader::sendFinished(qint64 shotId, Attempt last) {
    const Result result = m_attemptResult;
    // A shot sent by Upload missing shots is DEBUG: the run's start and end lines tell
    // its story at INFO, where a line per shot buried them (1,133 in one run).
    const auto note = [shotId, &last](const QString& text) {
        const QString line = QStringLiteral("shot %1 %2").arg(shotId).arg(text);
        if (last.background)
            DIAG_DEBUG(DECENT, "DecentShotUploader") << line;
        else
            DIAG_INFO(DECENT, "DecentShotUploader") << line;
    };
    switch (result) {
    case Result::Uploaded:
        note(m_uploadedNote); break;
    case Result::NoMachine:
        note(QStringLiteral("not uploaded: no DE1 connected")); break;
    case Result::NoSerial:
        note(QStringLiteral("not uploaded: the DE1 reports no serial number and the account settled none for it")); break;
    case Result::NotFound:
        DIAG_WARN(DECENT, "DecentShotUploader") << "shot" << shotId << "not uploaded:" << m_current.failure; break;
    case Result::NotLinked:
        note(QStringLiteral("not uploaded: no Decent account linked")); break;
    case Result::NeedsSignIn:
        note(QStringLiteral("not uploaded: the Decent account needs signing in again")); break;
    case Result::Maintenance:
        note(QStringLiteral("not uploaded: maintenance cycle")); break;
    case Result::TooShort:
        note(QStringLiteral("not uploaded: shorter than the minimum length")); break;
    case Result::Failed:
        DIAG_WARN(DECENT, "DecentShotUploader") << "shot" << shotId << "not uploaded after" << last.attempts
                                                << QStringLiteral("attempt(s) (%1)").arg(m_attemptWhy); break;
    default: break;
    }
    m_attemptResult = Result::None;
    m_loggedOddAnswer = false;
    setUploading(false);
    // An UpdateOnly for a shot not in the account sent nothing and changes nothing shown.
    if (result == Result::None) return;
    m_lastShotId = shotId;
    m_lastResult = result;
    m_lastHttpStatus = m_attemptStatus;
    m_lastSerial = m_current.serial;
    emit lastResultChanged();
    emit uploadFinished(m_lastShotId, result);
}
