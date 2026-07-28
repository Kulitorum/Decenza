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

    // What the PREVIOUS run left behind, read once from disk by CrashHandler during startup and
    // handed here by main(). They live on this class because this is where QML already goes for
    // crash reporting — CrashReportDialog reads them and passes them straight back to
    // submitReport() below, so the data and the thing that sends it are one object.
    //
    // NOTE the asymmetry, because it is the reason these are CONSTANT rather than notifying: this
    // class does not own the log's lifecycle. Clearing it is MainController::clearCrashLog(),
    // which deletes the file CrashHandler wrote; nothing writes back here, and nothing needs to.
    // The values describe a run that has already ended, so they cannot change while this process
    // lives, and dismissing the banner is UI state rather than a change to what happened.
    Q_PROPERTY(QString previousCrashLog READ previousCrashLog CONSTANT)
    Q_PROPERTY(QString previousDebugLogTail READ previousDebugLogTail CONSTANT)

public:
    explicit CrashReporter(QObject* parent = nullptr);

    bool isSubmitting() const { return m_submitting; }
    QString lastError() const { return m_lastError; }

    QString previousCrashLog() const { return m_previousCrashLog; }
    QString previousDebugLogTail() const { return m_previousDebugLogTail; }

    /// Called once by main() before the QML engine loads. Not a QML setter: the properties are
    /// CONSTANT, so a later change would not notify anything that already read them.
    void setPreviousRun(const QString& crashLog, const QString& debugLogTail)
    {
        m_previousCrashLog = crashLog;
        m_previousDebugLogTail = debugLogTail;
    }

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
    QString m_previousCrashLog;
    QString m_previousDebugLogTail;

    void setSubmitting(bool submitting);
    void setLastError(const QString& error);
};

#endif // CRASHREPORTER_H
