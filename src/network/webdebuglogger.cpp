#include "webdebuglogger.h"

#include <QDebug>
#include <QStandardPaths>
#include <QDir>
#include <QTextStream>

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

    // Set up log file path
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    m_logFilePath = dataDir + "/debug.log";

    // Write session start marker
    QFile file(m_logFilePath);
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << "\n========== SESSION START: " << m_startTime.toString(Qt::ISODate) << " ==========\n";
    }
}

void WebDebugLogger::messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    // Forward to previous handler (console output) - wall clock comes from qSetMessagePattern
    if (s_previousHandler) {
        s_previousHandler(type, context, msg);
    }

    // Capture to our buffer (without prefix - internal use)
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

    // Also write to file (outside mutex to avoid blocking)
    locker.unlock();
    writeToFile(line);
}

void WebDebugLogger::writeToFile(const QString& line)
{
    QFile file(m_logFilePath);
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << line << "\n";
        file.close();

        // Check if we need to trim
        if (file.size() > MAX_LOG_FILE_SIZE) {
            trimLogFile();
        }
    }
}

void WebDebugLogger::trimLogFile()
{
    QFile file(m_logFilePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    // Read all content
    QByteArray content = file.readAll();
    file.close();

    // Keep last ~80% of max size to avoid frequent trimming
    qint64 keepSize = MAX_LOG_FILE_SIZE * 80 / 100;
    if (content.size() <= keepSize) {
        return;
    }

    // Find a newline near the trim point to keep lines intact
    qint64 trimPoint = content.size() - keepSize;
    qint64 newlinePos = content.indexOf('\n', trimPoint);
    if (newlinePos == -1) {
        newlinePos = trimPoint;
    }

    // Write trimmed content
    if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        file.write("... [log trimmed] ...\n");
        file.write(content.mid(newlinePos + 1));
    }
}

QString WebDebugLogger::getPersistedLog() const
{
    QFile file(m_logFilePath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString::fromUtf8(file.readAll());
    }
    return QString();
}

QStringList WebDebugLogger::getPersistedLogChunk(qsizetype offset, qsizetype limit, qsizetype* totalLines) const
{
    QFile file(m_logFilePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (totalLines) *totalLines = 0;
        return {};
    }

    QTextStream stream(&file);
    QStringList result;
    qsizetype lineNum = 0;

    while (!stream.atEnd()) {
        QString line = stream.readLine();
        if (lineNum >= offset && result.size() < limit) {
            result.append(line);
        }
        lineNum++;
    }

    if (totalLines) *totalLines = lineNum;
    return result;
}

QString WebDebugLogger::logFilePath() const
{
    return m_logFilePath;
}

QStringList WebDebugLogger::getLines(int afterIndex, int* lastIndex) const
{
    QMutexLocker locker(&m_mutex);

    if (lastIndex) {
        *lastIndex = static_cast<int>(m_lines.size());
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

void WebDebugLogger::clear(bool clearFile)
{
    QMutexLocker locker(&m_mutex);
    m_lines.clear();

    if (clearFile && !m_logFilePath.isEmpty()) {
        locker.unlock();
        QFile file(m_logFilePath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            QTextStream stream(&file);
            stream << "========== LOG CLEARED: " << QDateTime::currentDateTime().toString(Qt::ISODate) << " ==========\n";
        }
    }
}

int WebDebugLogger::lineCount() const
{
    QMutexLocker locker(&m_mutex);
    return static_cast<int>(m_lines.size());
}
