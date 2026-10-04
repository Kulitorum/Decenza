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
    if (httpStatus == 408 || httpStatus == 429) return ResponseClass::Transient;
    if (httpStatus >= 400 && httpStatus < 500) return ResponseClass::Permanent;
    return ResponseClass::Transient;
}

QString DecentShotUploader::shotViewUrl(const QString& serial, const QString& serverShotId) {
    if (serial.isEmpty() || serverShotId.isEmpty()) return QString();
    QUrl url(QString::fromLatin1(DecentAccount::kBaseUrl) + QStringLiteral("/support/espressomachine"));
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
        finish(Result::NotLinked);
        return;
    }

    const DecentMachineIdentity connected = m_machineIdentity ? m_machineIdentity() : DecentMachineIdentity{};
    const QString dbPath = m_storage->databasePath();
    auto destroyed = m_destroyed;
    m_storage->runAfterQueuedWrites([this, destroyed, dbPath, shotId, connected]() {
        Prepared p;
        p.shotId = shotId;
        p.error = Result::NotFound;
        withTempDb(dbPath, "decent_upload", [&](QSqlDatabase& db) {
            const ShotRecord record = ShotHistoryStorage::loadShotRecordStatic(db, shotId, nullptr, Q_FUNC_INFO);
            DecentUploadState state;
            if (record.summary.id <= 0 || !ShotHistoryStorage::loadDecentUploadStateStatic(db, shotId, &state))
                return;
            const ShotProjection shot = ShotHistoryStorage::convertShotRecord(record);

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
        if (p.error == Result::None) {
            QFile file(uploadDebugFilePath(QStringLiteral("last_decent_upload.json")));
            if (file.open(QIODevice::WriteOnly))
                file.write(QJsonDocument::fromJson(p.body).toJson(QJsonDocument::Indented));
        }
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
    if (!m_account->applyAuth(request)) {
        finish(Result::NotLinked);
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
    {
        QFile file(uploadDebugFilePath(QStringLiteral("last_decent_upload_response.txt")));
        if (file.open(QIODevice::WriteOnly))
            file.write("POST " + reply->url().toEncoded() + "\nHTTP " + QByteArray::number(status) + "\n\n" + body);
    }

    switch (classify(status, transportError)) {
    case ResponseClass::Success: {
        const QJsonObject json = QJsonDocument::fromJson(body).object();
        QString serverId = json.value(QStringLiteral("id")).toString();
        if (serverId.isEmpty()) serverId = m_current.uuid;
        const bool duplicate = json.value(QStringLiteral("duplicate")).toBool();
        m_storage->requestRecordDecentUpload(shotId, serverId, m_current.serial);
        if (m_current.replace && duplicate) {
            // Seen from decentespresso.com on 2026-10-04 with ?replace=1 sent exactly as
            // its API docs and Decaid send it: the edit is not stored.
            DIAG_WARN(DECENT, "DecentShotUploader") << QStringLiteral(
                "shot %1: Decent answered a replace with \"duplicate\" and kept its earlier copy (serial %2, server id %3)")
                .arg(QString::number(shotId), m_current.serial, serverId);
            finish(Result::NotReplaced, status);
            return;
        }
        const QString how = duplicate ? QStringLiteral(" (already on the server)")
                            : m_current.replace ? QStringLiteral(" (replace)") : QString();
        DIAG_INFO(DECENT, "DecentShotUploader") << QStringLiteral("shot %1 uploaded%2, serial %3, server id %4")
                                                       .arg(QString::number(shotId), how, m_current.serial, serverId);
        finish(Result::Uploaded, status);
        return;
    }
    case ResponseClass::Transient:
        if (m_attempt < kAttempts) {
            DIAG_DEBUG(DECENT, "DecentShotUploader") << "shot" << shotId << "attempt" << m_attempt << "failed (HTTP"
                                                     << status << reply->errorString() << ") - retrying";
            QTimer::singleShot(m_retryDelayMs * m_attempt, this, &DecentShotUploader::send);
            return;
        }
        DIAG_WARN(DECENT, "DecentShotUploader") << "shot" << shotId << "not uploaded after" << kAttempts
                                                << "attempts (HTTP" << status << reply->errorString() << ")";
        finish(Result::Failed, status);
        return;
    case ResponseClass::AuthFailed:
        m_account->reportAuthFailure();
        finish(Result::NeedsSignIn, status);
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

void DecentShotUploader::finish(Result result, int httpStatus) {
    if (result == Result::NoMachine)
        DIAG_INFO(DECENT, "DecentShotUploader") << "shot" << m_current.shotId << "not uploaded: no DE1 connected";
    m_lastShotId = m_current.shotId;
    m_lastResult = result;
    m_lastHttpStatus = httpStatus;
    m_lastSerial = m_current.serial;
    m_uploading = false;
    emit uploadingChanged();
    emit lastResultChanged();
    emit uploadFinished(m_lastShotId, result);
}
