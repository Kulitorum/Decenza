#include "shotdebuglogger.h"

#include <QDebug>
#include <QDateTime>

// Static members
ShotDebugLogger* ShotDebugLogger::s_instance = nullptr;
QtMessageHandler ShotDebugLogger::s_previousHandler = nullptr;

// Custom message handler that captures to the logger
static void shotDebugMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    // Always forward to previous handler (so console still works)
    if (ShotDebugLogger::previousHandler()) {
        ShotDebugLogger::previousHandler()(type, context, msg);
    }

    // Capture if logger is active
    if (ShotDebugLogger::instance() && ShotDebugLogger::instance()->isCapturing()) {
        ShotDebugLogger::instance()->handleMessage(type, msg);
    }
}

ShotDebugLogger::ShotDebugLogger(QObject* parent)
    : QObject(parent)
{
    s_instance = this;
}

ShotDebugLogger::~ShotDebugLogger()
{
    // Restore previous handler if we're still capturing
    if (m_capturing && s_previousHandler) {
        qInstallMessageHandler(s_previousHandler);
    }
    s_instance = nullptr;
}

void ShotDebugLogger::startCapture()
{
    QMutexLocker locker(&m_mutex);

    // If already capturing, reset for new shot (don't install handler again)
    if (m_capturing) {
        m_logLines.clear();
        m_timer.start();
        m_logLines << QString("[%1] START Shot capture restarted - %2")
                          .arg(formatTime())
                          .arg(QDateTime::currentDateTime().toString(Qt::ISODate));
        return;
    }

    m_logLines.clear();
    m_timer.start();
    m_capturing = true;

    // Install our message handler
    s_previousHandler = qInstallMessageHandler(shotDebugMessageHandler);

    m_logLines << QString("[%1] START Shot capture started - %2")
                      .arg(formatTime())
                      .arg(QDateTime::currentDateTime().toString(Qt::ISODate));
}

void ShotDebugLogger::stopCapture()
{
    QMutexLocker locker(&m_mutex);

    if (m_capturing) {
        m_logLines << QString("[%1] STOP Shot capture stopped")
                          .arg(formatTime());
        m_capturing = false;

        // Restore previous message handler
        if (s_previousHandler) {
            qInstallMessageHandler(s_previousHandler);
            s_previousHandler = nullptr;
        }
    }
}

QString ShotDebugLogger::getCapturedLog() const
{
    QMutexLocker locker(&m_mutex);
    return m_logLines.join("\n");
}

void ShotDebugLogger::clear()
{
    QMutexLocker locker(&m_mutex);
    m_logLines.clear();
}

void ShotDebugLogger::handleMessage(QtMsgType type, const QString& message)
{
    QString category;
    switch (type) {
    case QtDebugMsg:
        category = "DEBUG";
        break;
    case QtInfoMsg:
        category = "INFO";
        break;
    case QtWarningMsg:
        category = "WARNING";
        break;
    case QtCriticalMsg:
        category = "CRITICAL";
        break;
    case QtFatalMsg:
        category = "FATAL";
        break;
    }

    appendLog(category, message);
}

void ShotDebugLogger::logInfo(const QString& message)
{
    appendLog("INFO", message);
}

void ShotDebugLogger::appendLog(const QString& category, const QString& message)
{
    QMutexLocker locker(&m_mutex);
    if (!m_capturing) return;
    if (m_logLines.size() >= 2000) return;

    m_logLines << QString("[%1] %2 %3").arg(formatTime(), category, message);
}

QString ShotDebugLogger::formatTime() const
{
    return QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
}
