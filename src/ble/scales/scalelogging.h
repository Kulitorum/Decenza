#pragma once

#include <QDebug>
#include <QString>

// Shared logging macros for scale implementations.
// Must only be used inside methods of ScaleDevice subclasses (requires
// `emit logMessage(...)` to be in scope, i.e. a QObject with that signal).
// Each scale file defines its own short aliases, e.g.:
//   #define ACAIA_LOG(msg)  SCALE_LOG("AcaiaScale", msg)
//   #define ACAIA_WARN(msg) SCALE_WARN("AcaiaScale", msg)

// Every line in the scale subsystem starts with the stable [Scale] marker and
// then names its own source:
//   [Scale][BLE AcaiaScale] tare sent
//   [Scale][USB Scale] Probing cu.usbmodem1234
// So one `grep '\[Scale\]'` returns the WHOLE scale narrative from a
// user-submitted log — BLEManager's own lines (appendScaleLog prefixes those),
// the drivers, the transports, the refractometers and USB.
//
// This used to hold by accident: driver lines reached stderr twice, once bare
// and once through appendScaleLog's [Scale] mirror. Removing that duplicate
// (#1700) also removed driver lines from the [Scale] grep — the marker is now
// applied at the source instead. Reading this subsystem after the fact from a
// log the user sent in is how it gets diagnosed, so a reader must not have to
// know four prefixes and notice which one they forgot.
//
// Put logging behind one of these (or a small member helper that wraps them) —
// never hand-roll a prefix at the call site. usbscalemanager.cpp did that at 73
// sites and drifted into qDebug saying one thing while logMessage said another.
//
// SCALE_LOG_TAGGED is the base; `tag` is a string LITERAL naming the source.
// SCALE_LOG/SCALE_WARN are the BLE-driver shorthand and keep the tag "BLE <x>".
// All of them require `emit logMessage(QString)` to be in scope, i.e. use them
// inside a QObject carrying that signal.
#define SCALE_LOG_TAGGED(tag, msg) do { \
    QString _msg = QString("[Scale][" tag "] ") + msg; \
    qDebug().noquote() << _msg; \
    emit logMessage(_msg); \
} while(0)

#define SCALE_WARN_TAGGED(tag, msg) do { \
    QString _msg = QString("[Scale][" tag "] ") + msg; \
    qWarning().noquote() << _msg; \
    emit logMessage(_msg); \
} while(0)

// Stderr-only variants for scale code that has no logMessage signal to emit —
// free functions, static helpers, JNI shims. Same [Scale] marker, so the line
// still turns up in the one grep; it just doesn't reach the in-app scale log.
// Both spellings exist so nobody hand-rolls the missing half.
#define SCALE_LOG_STDERR_TAGGED(tag, msg) \
    qDebug().noquote() << (QString("[Scale][" tag "] ") + (msg))

#define SCALE_WARN_STDERR_TAGGED(tag, msg) \
    qWarning().noquote() << (QString("[Scale][" tag "] ") + (msg))

#define SCALE_LOG(prefix, msg)  SCALE_LOG_TAGGED("BLE " prefix, msg)
#define SCALE_WARN(prefix, msg) SCALE_WARN_TAGGED("BLE " prefix, msg)
