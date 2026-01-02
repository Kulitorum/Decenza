#pragma once

#include <QObject>
#include <QMutex>
#include <QStringList>
#include <QElapsedTimer>
#include <QDateTime>

/**
 * Captures Qt debug output for streaming to web interface.
 * Maintains a ring buffer of recent log messages.
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

    // Clear the buffer
    void clear();

    // Get current line count (for polling comparison)
    int lineCount() const;

private:
    explicit WebDebugLogger(QObject* parent = nullptr);

    void handleMessage(QtMsgType type, const QString& message);

    static void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg);
    static QtMessageHandler s_previousHandler;
    static WebDebugLogger* s_instance;

    mutable QMutex m_mutex;
    QStringList m_lines;
    int m_maxLines = 1000;  // Ring buffer size
    QElapsedTimer m_timer;
    QDateTime m_startTime;
};
