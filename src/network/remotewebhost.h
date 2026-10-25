#pragma once

#include <QString>

class QTcpSocket;

// What RemoteAccess needs from the server that serves the web interface
// (ShotServer). An interface so the coordinator is testable without linking
// ShotServer, which no test target compiles.
class RemoteWebHost {
public:
    virtual ~RemoteWebHost() = default;
    // Whether the web interface is being served at all (Enable Network is on).
    virtual bool remoteWebServing() const = 0;
    // A new admission for a browser that opened the safe URL; returns the cookie
    // value, or empty when none could be created.
    virtual QString createRemoteWebAdmission(const QString& userAgent) = 0;
    virtual bool remoteWebAdmissionValid(const QString& admissionId) const = 0;
    // Takes ownership of an admitted connection whose next request is still
    // unread in the socket (the WebSocket upgrade depends on that).
    virtual void adoptRemoteWebConnection(QTcpSocket* socket) = 0;
    // Forget every admission and close every adopted connection.
    virtual void endRemoteWebSessions() = 0;
};
