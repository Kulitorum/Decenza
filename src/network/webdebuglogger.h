#pragma once

#include <QObject>
#include <QMutex>
#include <QStringList>
#include <QElapsedTimer>
#include <QDateTime>
#include <QFile>

/**
 * Captures Qt debug output for streaming to web interface.
 * Maintains a ring buffer of recent log messages in memory,
 * and persists to a file for crash recovery.
 */
class WebDebugLogger : public QObject {
    Q_OBJECT

public:
    static WebDebugLogger* instance();
    static void install();

    // Get recent log lines (for polling)
    QStringList getLines(int afterIndex, int* lastIndex = nullptr) const;

    // Get all lines in buffer
    QStringList getAllLines() const;

    // Get persisted log from file (survives crashes)
    QString getPersistedLog() const;

    // Get a chunk of the persisted log by line offset/limit.
    // Returns the lines and sets totalLines to the total number of lines in the file.
    QStringList getPersistedLogChunk(qsizetype offset, qsizetype limit, qsizetype* totalLines = nullptr) const;

    // One "========== SESSION START: ... ==========" marker in the persisted log.
    struct SessionBoundary {
        qsizetype startLine;
        QString timestamp;
        qsizetype lineCount;
    };

    // Session-boundary index for the persisted log, keyed on the log file's
    // size + mtime: rebuilt only when either changed since the last call,
    // instead of rescanning the whole file on every call (used by the
    // debug_get_log MCP tool's sessions/session modes). Sets totalLines to
    // the file's total line count when non-null.
    QList<SessionBoundary> sessionIndex(qsizetype* totalLines = nullptr) const;

    // Clear the buffer (and optionally the file)
    void clear(bool clearFile = false);

    // Get current line count (for polling comparison)
    int lineCount() const;

    // Get log file path
    QString logFilePath() const;

#ifdef DECENZA_TESTING
    // Test-only: construct pointed at an explicit file instead of the real
    // singleton's AppDataLocation path, so a test can seed exact file
    // content/size/mtime without touching s_instance or the developer's
    // real debug.log. Writes no session-start marker — the test controls
    // the file's content directly.
    explicit WebDebugLogger(const QString& testLogFilePath, QObject* parent = nullptr);

    // Test-only: point the singleton instance() resolves at a test-owned
    // object (e.g. one built with the constructor above), so a real
    // registerDebugTools() handler under test resolves WebDebugLogger::instance()
    // to test-controlled content. Does not take ownership; pass nullptr to
    // restore no singleton.
    static void installForTesting(WebDebugLogger* instance);

    // Test-only: counts sessionIndex() cache *misses* (rebuilds), so a test
    // can assert a repeated call with no file change reused the cache
    // instead of rescanning.
    static int testSessionIndexRebuildCount() { return s_testSessionIndexRebuildCount; }
    static void resetTestSessionIndexRebuildCount() { s_testSessionIndexRebuildCount = 0; }
#endif

private:
    explicit WebDebugLogger(QObject* parent = nullptr);

    void handleMessage(QtMsgType type, const QString& message);
    void writeToFile(const QString& line);
    void trimLogFile();

    static void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg);
    static QtMessageHandler s_previousHandler;
    static WebDebugLogger* s_instance;

    mutable QMutex m_mutex;
    QStringList m_lines;
    int m_maxLines = 500;   // Ring buffer size
    QElapsedTimer m_timer;
    QDateTime m_startTime;

    // File persistence
    QString m_logFilePath;
    static constexpr qint64 MAX_LOG_FILE_SIZE = 2 * 1024 * 1024;  // 2MB - several days of sessions

    // Session-index cache (see sessionIndex()). Separate mutex from m_mutex,
    // which guards the in-memory ring buffer, not the persisted file.
    mutable QMutex m_sessionIndexMutex;
    mutable QList<SessionBoundary> m_cachedSessionIndex;
    mutable qsizetype m_cachedTotalLines = 0;
    mutable qint64 m_cachedFileSize = -1;
    mutable QDateTime m_cachedFileMTime;

#ifdef DECENZA_TESTING
    static inline int s_testSessionIndexRebuildCount = 0;
#endif
};
