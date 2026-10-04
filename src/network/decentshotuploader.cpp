#include "decentshotuploader.h"

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
#include <QTimer>
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

DecentShotUploader::ResponseClass DecentShotUploader::classify(int httpStatus, bool transportError) {
    if (transportError) return ResponseClass::Transient;
    if (httpStatus >= 200 && httpStatus < 300) return ResponseClass::Success;
    if (httpStatus == 401) return ResponseClass::AuthFailed;
    if (httpStatus == 403) return ResponseClass::NotRegistered;
    // 404/405/410 say the endpoint is wrong, not the shot: never brand it rejected.
    if (httpStatus == 404 || httpStatus == 405 || httpStatus == 408 || httpStatus == 410 || httpStatus == 429)
        return ResponseClass::Transient;
    if (httpStatus >= 400 && httpStatus < 500) return ResponseClass::Permanent;
    return ResponseClass::Transient;
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

void DecentShotUploader::uploadNow(qint64 shotId) {
    if (m_uploading || shotId <= 0) return;
    m_current = Prepared{};
    m_current.shotId = shotId;
    m_uploading = true;
    emit uploadingChanged();

    if (m_account->state() != DecentAccount::State::Linked) {
        finish(m_account->state() == DecentAccount::State::NeedsSignIn ? Result::NeedsSignIn : Result::NotLinked);
        return;
    }

    const DecentMachineIdentity connected = m_machineIdentity ? m_machineIdentity() : DecentMachineIdentity{};
    const double minDuration = m_minDuration ? m_minDuration() : 0.0;
    const QString dbPath = m_storage->databasePath();
    auto destroyed = m_destroyed;
    m_storage->runAfterQueuedWrites([this, destroyed, dbPath, shotId, connected, minDuration]() {
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
            const ShotProjection shot = ShotHistoryStorage::convertShotRecord(record);
            switch (uploadIneligibility(shot.beverageType, shot.durationSec, minDuration)) {
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
                p.error = Result::NoMachine;
                return;
            }
            p.serial = machine.serialNumber;
            p.uuid = shot.uuid;
            p.body = DecentShotRecord::build(shot, machine);
            p.error = Result::None;
        });
        if (p.error == Result::None)
            writeDebugFile(QStringLiteral("last_decent_upload.json"), QJsonDocument::fromJson(p.body).toJson(QJsonDocument::Indented));
        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, destroyed, p]() {
            if (!*destroyed) onPrepared(p);
        }, Qt::QueuedConnection);
    });
}

void DecentShotUploader::onPrepared(const Prepared& prepared) {
    m_current = prepared;
    if (prepared.error != Result::None) {
        finish(prepared.error);
        return;
    }
    m_attempt = 0;
    send();
}

void DecentShotUploader::send() {
    QUrl url(QString::fromLatin1(DecentAccount::kBaseUrl) + QStringLiteral("/support/api/shot_upload"));
    if (m_current.replace) url.setQuery(QStringLiteral("replace=1"));
    QNetworkRequest request(url);
    // Exactly what Decaid's proxy and Decent's API docs send. JSON is UTF-8 by
    // definition (RFC 8259), so a charset parameter adds nothing.
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(kUploadTimeoutMs);
    if (!m_account->applyAuth(request)) {
        // Signed out or refused during a retry delay.
        finish(m_account->state() == DecentAccount::State::NeedsSignIn ? Result::NeedsSignIn : Result::NotLinked);
        return;
    }
    ++m_attempt;
    QNetworkReply* reply = m_network->post(request, m_current.body);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onReplyFinished(reply); });
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
    ResponseClass responseClass = classify(status, transportError);
    QString why = status == 0 ? reply->errorString() : QStringLiteral("HTTP %1").arg(status);
    // A 2xx that is not the API's {"ok":true,...} — a captive portal's page, a
    // proxy, a changed API — has not stored anything.
    if (responseClass == ResponseClass::Success && !json.value(QStringLiteral("ok")).toBool()) {
        why = QStringLiteral("HTTP %1 that is not the upload API's answer").arg(status);
        if (m_attempt == 1)
            DIAG_WARN(DECENT, "DecentShotUploader") << "shot" << shotId << why << QString::fromUtf8(body.left(300));
        responseClass = ResponseClass::Transient;
    }

    switch (responseClass) {
    case ResponseClass::Success: {
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
            finish(Result::NotReplaced, status);
            return;
        }
        m_storage->requestRecordDecentUpload(shotId, serverId, m_current.serial);
        const QString how = duplicate ? QStringLiteral(" (already on the server)")
                            : m_current.replace ? QStringLiteral(" (replace)") : QString();
        DIAG_INFO(DECENT, "DecentShotUploader") << QStringLiteral("shot %1 uploaded%2, serial %3, server id %4")
                                                       .arg(QString::number(shotId), how, m_current.serial, serverId);
        finish(Result::Uploaded, status);
        return;
    }
    case ResponseClass::Transient:
        if (m_attempt < kAttempts) {
            DIAG_DEBUG(DECENT, "DecentShotUploader") << "shot" << shotId << "attempt" << m_attempt
                                                     << QStringLiteral("failed (%1), retrying").arg(why);
            QTimer::singleShot(m_retryDelayMs * m_attempt, this, &DecentShotUploader::send);
            return;
        }
        DIAG_WARN(DECENT, "DecentShotUploader") << "shot" << shotId << "not uploaded after" << kAttempts
                                                << QStringLiteral("attempts (%1)").arg(why);
        finish(Result::Failed, status);
        return;
    case ResponseClass::AuthFailed:
        m_account->reportAuthFailure();
        // Unlinked while the request was out: there is nothing to sign in to.
        finish(m_account->state() == DecentAccount::State::NeedsSignIn ? Result::NeedsSignIn : Result::NotLinked, status);
        return;
    case ResponseClass::NotRegistered:
        DIAG_WARN(DECENT, "DecentShotUploader") << "serial" << m_current.serial
                                                << "is not registered to the linked Decent account (HTTP 403)";
        finish(Result::NotRegistered, status);
        return;
    case ResponseClass::Permanent:
        m_storage->requestRecordDecentRejection(shotId, status);
        DIAG_WARN(DECENT, "DecentShotUploader") << "shot" << shotId << "rejected (HTTP" << status << "):"
                                                << QString::fromUtf8(body.left(300));
        finish(Result::Rejected, status);
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

void DecentShotUploader::finish(Result result, int httpStatus) {
    const qint64 id = m_current.shotId;
    switch (result) {
    case Result::NoMachine:
        DIAG_INFO(DECENT, "DecentShotUploader") << "shot" << id << "not uploaded: no DE1 connected"; break;
    case Result::NotFound:
        DIAG_WARN(DECENT, "DecentShotUploader") << "shot" << id << "not uploaded:" << m_current.failure; break;
    case Result::NotLinked:
        DIAG_INFO(DECENT, "DecentShotUploader") << "shot" << id << "not uploaded: no Decent account linked"; break;
    case Result::NeedsSignIn:
        DIAG_INFO(DECENT, "DecentShotUploader") << "shot" << id
                                                << "not uploaded: the Decent account needs signing in again"; break;
    case Result::Maintenance:
        DIAG_INFO(DECENT, "DecentShotUploader") << "shot" << id << "not uploaded: maintenance cycle"; break;
    case Result::TooShort:
        DIAG_INFO(DECENT, "DecentShotUploader") << "shot" << id << "not uploaded: shorter than the minimum length"; break;
    default: break;
    }
    m_lastShotId = m_current.shotId;
    m_lastResult = result;
    m_lastHttpStatus = httpStatus;
    m_lastSerial = m_current.serial;
    m_uploading = false;
    emit uploadingChanged();
    emit lastResultChanged();
    emit uploadFinished(m_lastShotId, result);
}
