#include "webdebuglogger.h"

#include <QDebug>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>

namespace {
const QString kSessionMarker = QStringLiteral("========== SESSION START:");
}

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

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

    // Set up log file path — use external storage on Android so logs survive APK updates.
    // Falls back to internal app data if external storage is unavailable.
    QString dataDir;
#ifdef Q_OS_ANDROID
    QJniObject javaPath = QJniObject::callStaticObjectMethod(
        "io/github/kulitorum/decenza_de1/StorageHelper",
        "getLogsPath",
        "()Ljava/lang/String;");
    if (javaPath.isValid()) {
        dataDir = javaPath.toString();
    }
#endif
    if (dataDir.isEmpty()) {
        dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    QDir().mkpath(dataDir);
    m_logFilePath = dataDir + "/debug.log";

    // Write session start marker
    QFile file(m_logFilePath);
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << "\n========== SESSION START: " << m_startTime.toString(Qt::ISODate) << " ==========\n";
    }
}

#ifdef DECENZA_TESTING
WebDebugLogger::WebDebugLogger(const QString& testLogFilePath, QObject* parent)
    : QObject(parent)
    , m_startTime(QDateTime::currentDateTime())
{
    m_timer.start();
    m_logFilePath = testLogFilePath;
}

void WebDebugLogger::installForTesting(WebDebugLogger* instance)
{
    s_instance = instance;
}
#endif

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

    // Write trimmed content; re-emit the session marker so it survives the trim.
    if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        file.write("... [log trimmed] ...\n");
        file.write(("========== SESSION START: " + m_startTime.toString(Qt::ISODate) + " ==========\n").toUtf8());
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

QList<WebDebugLogger::SessionBoundary> WebDebugLogger::sessionIndex(qsizetype* totalLines) const
{
    QMutexLocker locker(&m_sessionIndexMutex);

    const QFileInfo info(m_logFilePath);
    const qint64 size = info.size();
    const QDateTime mtime = info.lastModified();

    if (m_cachedFileSize != size || m_cachedFileMTime != mtime) {
#ifdef DECENZA_TESTING
        ++s_testSessionIndexRebuildCount;
#endif
        qsizetype scannedTotal = 0;
        // Same 100,000-line cap the tool's filtered/tailed reads use — a
        // generous superset of what MAX_LOG_FILE_SIZE (2MB) can ever hold.
        const QStringList allLines = getPersistedLogChunk(0, 100000, &scannedTotal);

        QList<SessionBoundary> sessions;
        for (qsizetype i = 0; i < allLines.size(); ++i) {
            if (allLines[i].contains(kSessionMarker)) {
                QString ts;
                const qsizetype tsStart = allLines[i].indexOf(kSessionMarker) + kSessionMarker.size();
                const qsizetype tsEnd = allLines[i].indexOf(QStringLiteral("=========="), tsStart);
                if (tsEnd > tsStart)
                    ts = allLines[i].mid(tsStart, tsEnd - tsStart).trimmed();
                sessions.append({i, ts, 0});
            }
        }
        for (qsizetype i = 0; i < sessions.size(); ++i) {
            const qsizetype nextStart = (i + 1 < sessions.size()) ? sessions[i + 1].startLine : scannedTotal;
            sessions[i].lineCount = nextStart - sessions[i].startLine;
        }

        m_cachedSessionIndex = sessions;
        m_cachedTotalLines = scannedTotal;
        m_cachedFileSize = size;
        m_cachedFileMTime = mtime;
    }

    if (totalLines) *totalLines = m_cachedTotalLines;
    return m_cachedSessionIndex;
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
