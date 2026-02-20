#include "crashreporter.h"
#include "version.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>
#include <QDebug>

static const char* API_URL = "https://api.decenza.coffee/v1/crash-report";
static const char* AI_REPORT_URL = "https://api.decenza.coffee/v1/ai-report";

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
    return "linux";  // Server enum validation rejects "unknown"; "linux" is the safest fallback
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
    request.setRawHeader("User-Agent", QString("Decenza-DE1/%1").arg(VERSION_STRING).toUtf8());
    request.setTransferTimeout(30000);  // 30s timeout for flaky mobile networks

    // Send POST request
    QNetworkReply* reply = m_networkManager.post(request, data);
    connect(reply, &QNetworkReply::finished, this, &CrashReporter::onReplyFinished);
}

void CrashReporter::submitAiReport(const QString& providerName,
                                    const QString& modelName,
                                    const QString& contextLabel,
                                    const QString& systemPrompt,
                                    const QString& conversationTranscript,
                                    const QString& shotDebugLog,
                                    const QString& userNotes)
{
    if (m_submitting) {
        qWarning() << "CrashReporter: Already submitting a report";
        return;
    }

    setSubmitting(true);
    setLastError(QString());

    // Build request body with structured fields
    QJsonObject body;
    body["version"] = QString(VERSION_STRING);
    body["platform"] = platform();
    body["device"] = deviceInfo();
    body["provider_name"] = providerName;
    body["model_name"] = modelName;
    body["system_prompt"] = systemPrompt;
    body["conversation_transcript"] = conversationTranscript;
    body["user_notes"] = userNotes;

    if (!contextLabel.isEmpty()) {
        body["context_label"] = contextLabel;
    }

    if (!shotDebugLog.isEmpty()) {
        body["shot_debug_log"] = shotDebugLog;
    }

    QJsonDocument doc(body);
    QByteArray data = doc.toJson(QJsonDocument::Compact);

    qDebug() << "CrashReporter: Submitting AI report to" << AI_REPORT_URL;

    // Create request
    QNetworkRequest request{QUrl(AI_REPORT_URL)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("User-Agent", QString("Decenza-DE1/%1").arg(VERSION_STRING).toUtf8());
    request.setTransferTimeout(30000);  // 30s timeout for flaky mobile networks

    // Send POST request
    QNetworkReply* reply = m_networkManager.post(request, data);
    reply->setProperty("isAiReport", true);
    connect(reply, &QNetworkReply::finished, this, &CrashReporter::onReplyFinished);
}

void CrashReporter::onReplyFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        qWarning() << "CrashReporter: onReplyFinished called with null reply";
        setSubmitting(false);
        QString error = "Internal error: no reply object";
        setLastError(error);
        emit failed(error);
        return;
    }

    reply->deleteLater();
    setSubmitting(false);

    const bool wasAiReport = reply->property("isAiReport").toBool();

    if (reply->error() != QNetworkReply::NoError) {
        QString error = reply->errorString();
        qWarning() << "CrashReporter: Failed to submit -" << error;
        setLastError(error);
        if (wasAiReport)
            emit aiReportFailed(error);
        else
            emit failed(error);
        return;
    }

    // Parse response
    QByteArray responseData = reply->readAll();
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString error = QString("Invalid server response (HTTP %1): %2")
                            .arg(statusCode).arg(parseError.errorString());
        qWarning() << "CrashReporter:" << error;
        setLastError(error);
        if (wasAiReport)
            emit aiReportFailed(error);
        else
            emit failed(error);
        return;
    }

    QJsonObject obj = doc.object();

    if (obj["success"].toBool()) {
        QString issueUrl = obj["issue_url"].toString();
        qDebug() << "CrashReporter: Report submitted successfully -" << issueUrl;
        if (wasAiReport)
            emit aiReportSubmitted(issueUrl);
        else
            emit submitted(issueUrl);
    } else {
        QString error = obj["error"].toString("Unknown error");
        qWarning() << "CrashReporter: Server error -" << error;
        setLastError(error);
        if (wasAiReport)
            emit aiReportFailed(error);
        else
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
