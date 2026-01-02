#pragma once

#include <QObject>
#include <QStringList>
#include <QMutex>
#include <QElapsedTimer>

class ShotDebugLogger : public QObject {
    Q_OBJECT

public:
    explicit ShotDebugLogger(QObject* parent = nullptr);
    ~ShotDebugLogger();

    // Start/stop capture - installs/removes Qt message handler
    void startCapture();
    void stopCapture();
    bool isCapturing() const { return m_capturing; }

    // Get captured log (call after stopCapture)
    QString getCapturedLog() const;
    void clear();

    // Manual log entry (thread-safe)
    void logInfo(const QString& message);

    // Called by Qt message handler
    void handleMessage(QtMsgType type, const QString& message);

    // Singleton access for message handler
    static ShotDebugLogger* instance() { return s_instance; }
    static QtMessageHandler previousHandler() { return s_previousHandler; }

private:
    void appendLog(const QString& category, const QString& message);
    QString formatTime() const;

    mutable QMutex m_mutex;
    QStringList m_logLines;
    QElapsedTimer m_timer;
    bool m_capturing = false;

    static ShotDebugLogger* s_instance;
    static QtMessageHandler s_previousHandler;
};
