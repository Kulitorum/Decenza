#pragma once

#include <QLowEnergyController>
#include <QString>

// Shared naming + classification for QLowEnergyController errors and states.
//
// The DE1 transport (bletransport.cpp) and the scale transport
// (transport/qtscalebletransport.cpp) each carried their own copy of both
// switch statements, so the same radio failure could be described two different
// ways in one debug log. This is that mapping made shared, mirroring what
// bleserviceerror.h already does for QLowEnergyService::ServiceError.
//
// No `default:` label on purpose: a new value in Qt's enum trips -Wswitch rather
// than silently falling through to a bare number. That is an error on clang and
// gcc, which build with -Werror; the MSVC arm is /W4 without /WX, so on Windows
// it is a warning and the number-fallback after each switch is what a new value
// would print.

inline QString bleControllerErrorName(QLowEnergyController::Error err) {
    switch (err) {
        case QLowEnergyController::NoError:
            return QStringLiteral("NoError");
        case QLowEnergyController::UnknownError:
            return QStringLiteral("UnknownError");
        case QLowEnergyController::UnknownRemoteDeviceError:
            return QStringLiteral("UnknownRemoteDeviceError");
        case QLowEnergyController::NetworkError:
            return QStringLiteral("NetworkError");
        case QLowEnergyController::InvalidBluetoothAdapterError:
            return QStringLiteral("InvalidBluetoothAdapterError");
        case QLowEnergyController::ConnectionError:
            return QStringLiteral("ConnectionError");
        case QLowEnergyController::AdvertisingError:
            return QStringLiteral("AdvertisingError");
        case QLowEnergyController::RemoteHostClosedError:
            return QStringLiteral("RemoteHostClosedError");
        case QLowEnergyController::AuthorizationError:
            return QStringLiteral("AuthorizationError");
        case QLowEnergyController::MissingPermissionsError:
            return QStringLiteral("MissingPermissionsError");
        case QLowEnergyController::RssiReadError:
            return QStringLiteral("RssiReadError");
    }
    return QString::number(static_cast<int>(err));
}

inline QString bleControllerStateName(QLowEnergyController::ControllerState state) {
    switch (state) {
        case QLowEnergyController::UnconnectedState:
            return QStringLiteral("Unconnected");
        case QLowEnergyController::ConnectingState:
            return QStringLiteral("Connecting");
        case QLowEnergyController::ConnectedState:
            return QStringLiteral("Connected");
        case QLowEnergyController::DiscoveringState:
            return QStringLiteral("Discovering");
        case QLowEnergyController::DiscoveredState:
            return QStringLiteral("Discovered");
        case QLowEnergyController::ClosingState:
            return QStringLiteral("Closing");
        case QLowEnergyController::AdvertisingState:
            return QStringLiteral("Advertising");
    }
    return QString::number(static_cast<int>(state));
}

// True for the "the link went away" family: the DE1 stopped being reachable, or
// the OS tore the encrypted link down under BLE contention. This is the
// dual-HIGH contention signature the connection-priority coordinator and the
// BLE-stack-wedge detector key off, reported via de1LinkFault().
//
// There is deliberately no user-facing message for these, nor for any other
// controller error — see the comment in BleTransport::onControllerError for why
// that whole path is log-only (#1658).
inline bool bleControllerErrorIsLinkTeardown(QLowEnergyController::Error err) {
    return err == QLowEnergyController::ConnectionError
        || err == QLowEnergyController::RemoteHostClosedError
        || err == QLowEnergyController::AuthorizationError;
}
