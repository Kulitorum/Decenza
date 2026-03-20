#include "asynclogger.h"

#ifdef Q_OS_ANDROID
#include <android/log.h>
#endif

AsyncLogger* AsyncLogger::s_instance = nullptr;
QtMessageHandler AsyncLogger::s_previousHandler = nullptr;

void AsyncLogger::install()
{
    if (s_instance) return;
    s_instance = new AsyncLogger();
    s_instance->m_running = true;
    s_instance->m_writerThread.start();

    // Install as message handler — saves whoever was before us (Qt default on first call).
    // Handlers installed AFTER us (CrashHandler, WebDebugLogger, ShotDebugLogger) will
    // call through to us, and we enqueue for background I/O instead of blocking.
    s_previousHandler = qInstallMessageHandler(messageHandler);
}

void AsyncLogger::uninstall()
{
    if (!s_instance) return;

    // Restore previous handler so new qDebug() calls bypass us
    qInstallMessageHandler(s_previousHandler);
    s_previousHandler = nullptr;

    // Signal writer thread to drain remaining messages and exit
    s_instance->m_running = false;
    s_instance->m_condition.wakeOne();
    s_instance->m_writerThread.wait(2000);  // Allow up to 2s for drain

    delete s_instance;
    s_instance = nullptr;
}

void AsyncLogger::messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    Q_UNUSED(context);

    // Suppress noisy Qt/iOS AVAudioSession warning that fires on every video transition
    // in the screensaver. Harmless: audio session deactivation without prior activation.
    if (type == QtWarningMsg && msg.contains(u"Unbalanced audio session deactivation"))
        return;

    if (!s_instance) {
        // Fallback: write directly if instance was destroyed
        if (s_previousHandler) s_previousHandler(type, context, msg);
        return;
    }

    QMutexLocker lock(&s_instance->m_mutex);
    if (s_instance->m_count < BUFFER_SIZE) {
        s_instance->m_buffer[s_instance->m_writePos] = {type, msg};
        s_instance->m_writePos = (s_instance->m_writePos + 1) % BUFFER_SIZE;
        s_instance->m_count++;
        s_instance->m_condition.wakeOne();
    }
    // If buffer full, drop message (prefer responsiveness over completeness).
    // At 5 Hz extraction with verbose logging, 4096 entries ≈ 800 seconds of buffer.
}

void AsyncLogger::WriterThread::run()
{
    // Drain loop: runs until m_running is false AND buffer is empty
    while (m_logger->m_running || m_logger->m_count > 0) {
        LogEntry entry;
        {
            QMutexLocker lock(&m_logger->m_mutex);
            while (m_logger->m_count == 0) {
                if (!m_logger->m_running) return;  // Shutdown and buffer empty
                m_logger->m_condition.wait(&m_logger->m_mutex, 100);
            }

            // Move entry out of buffer (frees the QString memory in the slot)
            entry = std::move(m_logger->m_buffer[m_logger->m_readPos]);
            m_logger->m_readPos = (m_logger->m_readPos + 1) % BUFFER_SIZE;
            m_logger->m_count--;
        }

        // Platform output — this is the slow blocking I/O we moved off the main thread
#ifdef Q_OS_ANDROID
        int prio;
        switch (entry.type) {
        case QtDebugMsg:    prio = ANDROID_LOG_DEBUG; break;
        case QtInfoMsg:     prio = ANDROID_LOG_INFO;  break;
        case QtWarningMsg:  prio = ANDROID_LOG_WARN;  break;
        case QtCriticalMsg: prio = ANDROID_LOG_ERROR;  break;
        case QtFatalMsg:    prio = ANDROID_LOG_FATAL;  break;
        default:            prio = ANDROID_LOG_DEBUG;  break;
        }
        __android_log_print(prio, "Decenza", "%s", qPrintable(entry.message));
#else
        fprintf(stderr, "%s\n", qPrintable(entry.message));
#endif
    }
}
