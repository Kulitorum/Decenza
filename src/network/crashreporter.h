#ifndef CRASHREPORTER_H
#define CRASHREPORTER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QString>

/**
 * @brief Sends crash reports to api.decenza.coffee which creates GitHub issues.
 *
 * Usage from QML:
 *   CrashReporter.submitReport(crashLog, userNotes, debugLogTail)
 */
class CrashReporter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool submitting READ isSubmitting NOTIFY submittingChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit CrashReporter(QObject* parent = nullptr);

    bool isSubmitting() const { return m_submitting; }
    QString lastError() const { return m_lastError; }

    /// Submit a crash report. Emits submitted() or failed() when done.
    Q_INVOKABLE void submitReport(const QString& crashLog,
                                   const QString& userNotes = QString(),
                                   const QString& debugLogTail = QString());

    /// Get platform string (android, ios, windows, macos, linux)
    Q_INVOKABLE QString platform() const;

    /// Get device info string
    Q_INVOKABLE QString deviceInfo() const;

    /// Drop the keepalive sockets in this class's private QNetworkAccessManager.
    /// Called from main.cpp before an Android APK install dispatches so no
    /// QSocketNotifier survives into the install handover (#865).
    void clearConnectionCache() { m_networkManager.clearConnectionCache(); }

signals:
    void submittingChanged();
    void lastErrorChanged();
    void submitted(const QString& issueUrl);
    void failed(const QString& error);

private slots:
    void onReplyFinished();

private:
    QNetworkAccessManager m_networkManager;
    bool m_submitting = false;
    QString m_lastError;

    void setSubmitting(bool submitting);
    void setLastError(const QString& error);
};

#endif // CRASHREPORTER_H
