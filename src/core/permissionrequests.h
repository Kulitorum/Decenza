#pragma once

#include <QPermissions>

#include <functional>
#include <optional>

class QObject;
class QString;

// Every runtime permission request the app makes, issued one at a time.
//
// Android answers a request made while another one's dialog is pending with empty results
// at once (Activity.requestPermissions), and Qt reports an empty answer to a QPermission as
// Granted and caches it (qpermissions_android.cpp:137-146, :179-186). So two concurrent
// requests do not both ask: the second one silently "succeeds".
//
// Main thread only. The callback is skipped if `context` has been destroyed.
namespace PermissionRequests {

void request(const QPermission& permission, QObject* context,
             std::function<void(const QPermission&)> callback);

#ifdef Q_OS_ANDROID
// For a permission Qt has no QPermission type for. nullopt: the request was cancelled
// (interrupted) before the user answered.
void requestAndroid(const QString& permission, QObject* context,
                    std::function<void(std::optional<bool> granted)> callback);
#endif

} // namespace PermissionRequests
