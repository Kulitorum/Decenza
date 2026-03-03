#include "crashreporter.h"
#include "version.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>
#include <QDebug>

static const char* API_URL = "https://api.decenza.coffee/v1/crash-report";

CrashReporter::CrashReporter(QObject* parent)
    : QObject(parent)
{
}

QString CrashReporter::platform() const
{
#if defined(Q_OS_ANDROID)
    return "android";
#elif defined(Q_OS_IOS)
    return "ios";
#elif defined(Q_OS_WIN)
    return "windows";
#elif defined(Q_OS_MACOS)
    return "macos";
#elif defined(Q_OS_LINUX)
    return "linux";
#else
    return "unknown";
#endif
}

QString CrashReporter::deviceInfo() const
{
    QString info = QSysInfo::prettyProductName();

#ifdef Q_OS_ANDROID
    // Try to get more specific Android device info
    QString manufacturer = QSysInfo::productType();
    QString model = QSysInfo::machineHostName();
    if (!manufacturer.isEmpty() || !model.isEmpty()) {
        info = manufacturer + " " + model;
    }
#endif

    return info.simplified();
}

void CrashReporter::submitReport(const QString& crashLog,
                                  const QString& userNotes,
                                  const QString& debugLogTail)
{
    if (m_submitting) {
        qWarning() << "CrashReporter: Already submitting a report";
        return;
    }

    setSubmitting(true);
    setLastError(QString());

    // Build request body
    QJsonObject body;
    body["version"] = QString(VERSION_STRING);
    body["platform"] = platform();
    body["device"] = deviceInfo();
    body["crash_log"] = crashLog;

    if (!userNotes.isEmpty()) {
        body["user_notes"] = userNotes;
    }

    if (!debugLogTail.isEmpty()) {
        body["debug_log_tail"] = debugLogTail;
    }

    QJsonDocument doc(body);
    QByteArray data = doc.toJson(QJsonDocument::Compact);

    qDebug() << "CrashReporter: Submitting crash report to" << API_URL;

    // Create request
    QNetworkRequest request{QUrl(API_URL)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("User-Agent", QString("Decenza/%1").arg(VERSION_STRING).toUtf8());

    // Send POST request
    QNetworkReply* reply = m_networkManager.post(request, data);
    connect(reply, &QNetworkReply::finished, this, &CrashReporter::onReplyFinished);
}

void CrashReporter::onReplyFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    reply->deleteLater();
    setSubmitting(false);

    if (reply->error() != QNetworkReply::NoError) {
        QString error = reply->errorString();
        qWarning() << "CrashReporter: Failed to submit -" << error;
        setLastError(error);
        emit failed(error);
        return;
    }

    // Parse response
    QByteArray responseData = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    QJsonObject obj = doc.object();

    if (obj["success"].toBool()) {
        QString issueUrl = obj["issue_url"].toString();
        qDebug() << "CrashReporter: Report submitted successfully -" << issueUrl;
        emit submitted(issueUrl);
    } else {
        QString error = obj["error"].toString("Unknown error");
        qWarning() << "CrashReporter: Server error -" << error;
        setLastError(error);
        emit failed(error);
    }
}

void CrashReporter::setSubmitting(bool submitting)
{
    if (m_submitting != submitting) {
        m_submitting = submitting;
        emit submittingChanged();
    }
}

void CrashReporter::setLastError(const QString& error)
{
    if (m_lastError != error) {
        m_lastError = error;
        emit lastErrorChanged();
    }
}
