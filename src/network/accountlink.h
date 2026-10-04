#pragma once

#include <QObject>
#include <QtQmlIntegration/qqmlintegration.h>

// The outcome of connecting an upload account (Visualizer, the Decent account),
// shared so both settings cards show the same messages.
namespace AccountLink {
Q_NAMESPACE
QML_ELEMENT

enum class Error {
    None,
    Rejected,     // the server refused the credentials
    Unreachable,  // no answer at all (offline, timeout, DNS)
    ServerError,  // an answer, but an error or not one the API gives
};
Q_ENUM_NS(Error)
}
