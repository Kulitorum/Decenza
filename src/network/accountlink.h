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
    Unreachable,  // no answer, or not one that says whether they are valid
};
Q_ENUM_NS(Error)
}
