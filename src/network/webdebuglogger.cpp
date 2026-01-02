#include "webdebuglogger.h"

#include <QDebug>

WebDebugLogger* WebDebugLogger::s_instance = nullptr;
QtMessageHandler WebDebugLogger::s_previousHandler = nullptr;

WebDebugLogger* WebDebugLogger::instance()
{
    return s_instance;
}

void WebDebugLogger::install()
{
    if (!s_instance) {
        s_instance = new WebDebugLogger();
        s_previousHandler = qInstallMessageHandler(messageHandler);
    }
}

WebDebugLogger::WebDebugLogger(QObject* parent)
    : QObject(parent)
    , m_startTime(QDateTime::currentDateTime())
{
    m_timer.start();
}

void WebDebugLogger::messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    // Forward to previous handler (console output)
    if (s_previousHandler) {
        s_previousHandler(type, context, msg);
    }

    // Capture to our buffer
    if (s_instance) {
        s_instance->handleMessage(type, msg);
    }
}

void WebDebugLogger::handleMessage(QtMsgType type, const QString& message)
{
    QString category;
    switch (type) {
    case QtDebugMsg:    category = "DEBUG"; break;
    case QtInfoMsg:     category = "INFO"; break;
    case QtWarningMsg:  category = "WARN"; break;
    case QtCriticalMsg: category = "ERROR"; break;
    case QtFatalMsg:    category = "FATAL"; break;
    }

    double seconds = m_timer.elapsed() / 1000.0;
    QString line = QString("[%1] %2 %3")
        .arg(seconds, 8, 'f', 3)
        .arg(category, -5)
        .arg(message);

    QMutexLocker locker(&m_mutex);
    m_lines.append(line);

    // Trim to max size (ring buffer)
    while (m_lines.size() > m_maxLines) {
        m_lines.removeFirst();
    }
}

QStringList WebDebugLogger::getLines(int afterIndex, int* lastIndex) const
{
    QMutexLocker locker(&m_mutex);

    if (lastIndex) {
        *lastIndex = m_lines.size();
    }

    if (afterIndex >= m_lines.size()) {
        return QStringList();
    }

    if (afterIndex <= 0) {
        return m_lines;
    }

    return m_lines.mid(afterIndex);
}

QStringList WebDebugLogger::getAllLines() const
{
    QMutexLocker locker(&m_mutex);
    return m_lines;
}

void WebDebugLogger::clear()
{
    QMutexLocker locker(&m_mutex);
    m_lines.clear();
}

int WebDebugLogger::lineCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_lines.size();
}
