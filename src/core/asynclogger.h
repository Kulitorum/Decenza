#pragma once

#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <array>
#include <atomic>
#include <QString>

// Async logging: replaces Qt's default message handler with a non-blocking
// ring buffer. A background thread drains the buffer to platform output
// (logcat on Android, stderr elsewhere). Eliminates synchronous I/O from
// the main thread during extraction.
//
// Installation order matters — AsyncLogger must be installed FIRST so it
// sits at the bottom of the handler chain:
//   qDebug() → ShotDebugLogger → WebDebugLogger → CrashHandler → AsyncLogger → (bg thread)
//
// Uninstallation is reverse: CrashHandler first, then AsyncLogger.

class AsyncLogger {
public:
    static void install();
    static void uninstall();

private:
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    static constexpr int BUFFER_SIZE = 512;
#else
    static constexpr int BUFFER_SIZE = 4096;
#endif

    struct LogEntry {
        QtMsgType type;
        QString message;
    };

    // Writer thread drains ring buffer to platform output (logcat/stderr).
    // Subclasses QThread with run() override instead of using moveToThread,
    // because the writer loop blocks and doesn't need an event loop.
    class WriterThread : public QThread {
    public:
        explicit WriterThread(AsyncLogger* logger) : m_logger(logger) {
            setObjectName(QStringLiteral("AsyncLogger"));
        }
    protected:
        void run() override;
    private:
        AsyncLogger* m_logger;
    };

    static void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg);

    static AsyncLogger* s_instance;
    static QtMessageHandler s_previousHandler;

    // Ring buffer (mutex-protected, not lock-free — writer side is fast enough)
    QMutex m_mutex;
    QWaitCondition m_condition;
    std::array<LogEntry, BUFFER_SIZE> m_buffer;
    int m_writePos = 0;
    int m_readPos = 0;
    int m_count = 0;
    std::atomic<bool> m_running{false};

    WriterThread m_writerThread{this};
};
