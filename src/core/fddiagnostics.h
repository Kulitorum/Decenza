#ifndef FDDIAGNOSTICS_H
#define FDDIAGNOSTICS_H

#include <QJsonObject>

// Read-only, in-process descriptor census. Android deliberately prevents ADB
// from inspecting another app's /proc/<pid>/fd, while /proc/self remains
// available to the app itself. Keep this independent from CrashHandler: a
// diagnostic request must not emit hundreds of lines into the persisted log.
namespace FdDiagnostics {

// Returns a JSON-friendly snapshot of every currently open descriptor. Socket
// descriptors include their inode and, when procfs exposes it, protocol, state
// and endpoints. `supported` is false on platforms without the Linux procfs
// layout; that is an explicit unsupported result, never an empty census.
QJsonObject snapshot();

} // namespace FdDiagnostics

#endif // FDDIAGNOSTICS_H
