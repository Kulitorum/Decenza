#pragma once

#include <QDebug>
#include <QString>

// Shared logging macros for scale implementations.
// Must only be used inside methods of ScaleDevice subclasses (requires
// `emit logMessage(...)` to be in scope, i.e. a QObject with that signal).
// Each scale file defines its own short aliases, e.g.:
//   #define ACAIA_LOG(msg)  SCALE_LOG("AcaiaScale", msg)
//   #define ACAIA_WARN(msg) SCALE_WARN("AcaiaScale", msg)

#define SCALE_LOG(prefix, msg) do { \
    QString _msg = QString("[BLE " prefix "] ") + msg; \
    qDebug().noquote() << _msg; \
    emit logMessage(_msg); \
} while(0)

#define SCALE_WARN(prefix, msg) do { \
    QString _msg = QString("[BLE " prefix "] ") + msg; \
    qWarning().noquote() << _msg; \
    emit logMessage(_msg); \
} while(0)
