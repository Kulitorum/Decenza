#pragma once
#include <QMutex>
#include <QStringList>
#include <QtLogging>

// Capture only the logging paths under test; unrelated warnings still reach Qt Test.
class DiagnosticCapture {
public:
    explicit DiagnosticCapture(QStringList capturedPrefixes = {"[AI]", "[BeanBase]"})
        : prefixes(std::move(capturedPrefixes)) { active = this; previous = qInstallMessageHandler(receive); }
    ~DiagnosticCapture() { qInstallMessageHandler(previous); active = nullptr; }
    QStringList lines() const { QMutexLocker lock(&mutex); return messages; }
    QList<QtMsgType> levels() const { QMutexLocker lock(&mutex); return messageLevels; }
    QStringList terminals() const {
        QStringList result;
        for (const auto& line : lines())
            if (line.contains(" terminal outcome=")) result << line;
        return result;
    }
private:
    static void receive(QtMsgType type, const QMessageLogContext& context, const QString& message) {
        bool matches = false;
        if (active)
            for (const auto& prefix : active->prefixes)
                matches |= message.startsWith(prefix);
        if (matches) {
            QMutexLocker lock(&active->mutex);
            active->messages << message;
            active->messageLevels << type;
        } else if (active && active->previous) {
            active->previous(type, context, message);
        }
    }
    inline static DiagnosticCapture* active = nullptr;
    QtMessageHandler previous = nullptr;
    mutable QMutex mutex;
    const QStringList prefixes;
    QStringList messages;
    QList<QtMsgType> messageLevels;
};
