#include "remotewebdebuglogger.h"

#include "core/logtags.h"

#include <QDebug>
#include <QJSEngine>
#include <QQmlEngine>

RemoteWebDebugLogger::RemoteWebDebugLogger(QObject* parent)
    : QObject(parent)
{
}

RemoteWebDebugLogger* RemoteWebDebugLogger::create(QQmlEngine* engine, QJSEngine*)
{
    auto* logger = new RemoteWebDebugLogger(engine);
    QJSEngine::setObjectOwnership(logger, QJSEngine::CppOwnership);
    return logger;
}

QString RemoteWebDebugLogger::formattedMessage(const QString& subsystem,
                                               const QString& emitter,
                                               const QString& message)
{
    QString safeEmitter = emitter;
    safeEmitter.replace(QLatin1Char(']'), QLatin1Char('_'));
    safeEmitter.replace(QLatin1Char('\n'), QLatin1Char(' '));
    safeEmitter.replace(QLatin1Char('\r'), QLatin1Char(' '));

    const QString owner = DecenzaLog::isRegistered(subsystem)
        ? subsystem : QString::fromLatin1(DECENZA_LOG_MARKER_RUNTIME);
    return DecenzaLog::prefix(owner, safeEmitter) + QLatin1Char(' ') + message;
}

void RemoteWebDebugLogger::debug(const QString& subsystem,
                                 const QString& emitter,
                                 const QString& message)
{
    qDebug().noquote() << formattedMessage(subsystem, emitter, message); // log-marker-exempt: shared formatter validates the owner
}

void RemoteWebDebugLogger::info(const QString& subsystem,
                                const QString& emitter,
                                const QString& message)
{
    qInfo().noquote() << formattedMessage(subsystem, emitter, message); // log-marker-exempt: shared formatter validates the owner
}

void RemoteWebDebugLogger::warn(const QString& subsystem,
                                const QString& emitter,
                                const QString& message)
{
    qWarning().noquote() << formattedMessage(subsystem, emitter, message); // log-marker-exempt: shared formatter validates the owner
}

void RemoteWebDebugLogger::error(const QString& subsystem,
                                 const QString& emitter,
                                 const QString& message)
{
    qCritical().noquote() << formattedMessage(subsystem, emitter, message); // log-marker-exempt: shared formatter validates the owner
}
