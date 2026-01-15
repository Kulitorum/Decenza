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

    // Clear the buffer (and optionally the file)
    void clear(bool clearFile = false);

    // Get current line count (for polling comparison)
    int lineCount() const;

    // Get log file path
    QString logFilePath() const;

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
    int m_maxLines = 1000;  // Ring buffer size
    QElapsedTimer m_timer;
    QDateTime m_startTime;

    // File persistence
    QString m_logFilePath;
    static constexpr qint64 MAX_LOG_FILE_SIZE = 500 * 1024;  // 500KB - enough for ~5-10 min with BLE noise
};
