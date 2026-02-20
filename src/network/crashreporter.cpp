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

    m_pendingIsAiReport = false;
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
    reply->setProperty("isAiReport", false);
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
        emit aiReportFailed(tr("A report is already being submitted. Please wait and try again."));
        return;
    }

    m_pendingIsAiReport = true;
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

    if (!userNotes.isEmpty()) {
        body["user_notes"] = userNotes;
    }

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
        if (m_pendingIsAiReport)
            emit aiReportFailed(error);
        else
            emit failed(error);
        return;
    }

    reply->deleteLater();
    setSubmitting(false);

    const bool wasAiReport = reply->property("isAiReport").toBool();

    if (reply->error() != QNetworkReply::NoError) {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString error;
        if (statusCode == 404) {
            error = tr("Reporting service unavailable. Please ensure the app is up to date.");
        } else if (statusCode == 429) {
            error = tr("Too many reports submitted. Please try again in an hour.");
        } else if (statusCode >= 500) {
            error = tr("Server error (HTTP %1). Please try again later.").arg(statusCode);
        } else {
            error = reply->errorString();
        }
        qWarning() << "CrashReporter: Failed to submit - HTTP" << statusCode << reply->errorString();
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
        // Prefer our own "error" field; fall back to API Gateway's "message" field;
        // last resort use the HTTP status code so the user sees something actionable.
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString error = obj["error"].toString();
        if (error.isEmpty())
            error = obj["message"].toString();
        if (error.isEmpty())
            error = statusCode > 0 ? tr("Server error (HTTP %1)").arg(statusCode) : tr("Unknown error");
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
