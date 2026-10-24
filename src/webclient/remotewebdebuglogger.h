#pragma once

#include <QObject>
#include <QString>
#include <type_traits>
#include <QtQmlIntegration/qqmlintegration.h>

class QQmlEngine;
class QJSEngine;

// Browser-engine adapter for the WebDebugLogger calls made by shared/native
// QML. Records go through Qt's Wasm message handler to the browser console;
// host-side persisted logs remain owned by the native app.
class RemoteWebDebugLogger final : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(WebDebugLogger)
    QML_SINGLETON

public:
    static RemoteWebDebugLogger* create(QQmlEngine* engine, QJSEngine* jsEngine);

    Q_INVOKABLE void debug(const QString& subsystem, const QString& emitter, const QString& message);
    Q_INVOKABLE void info(const QString& subsystem, const QString& emitter, const QString& message);
    Q_INVOKABLE void warn(const QString& subsystem, const QString& emitter, const QString& message);
    Q_INVOKABLE void error(const QString& subsystem, const QString& emitter, const QString& message);

private:
    explicit RemoteWebDebugLogger(QObject* parent = nullptr);
    static QString formattedMessage(const QString& subsystem,
                                   const QString& emitter,
                                   const QString& message);
};

static_assert(!std::is_default_constructible_v<RemoteWebDebugLogger>,
              "RemoteWebDebugLogger is a QML_SINGLETON with a create() factory: Qt must not "
              "default-construct a separate logger instance.");
