#include "usbhotplug.h"

#include "usbscalemanager.h"
#include "usbmanager.h"
#include "../ble/scales/scalelogging.h"
#include "../ble/de1logging.h"

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>
#include <QPointer>
#endif

#define HOTPLUG_INFO(msg) SCALE_INFO_STDERR_TAGGED("USB Scale", msg)
#define HOTPLUG_WARN(msg) SCALE_WARN_STDERR_TAGGED("USB Scale", msg)

#ifdef Q_OS_ANDROID

namespace {

constexpr const char* kReceiverClass =
    "io/github/kulitorum/decenza_de1/UsbHotplugReceiver";

// QPointer so a manager destroyed before stop() leaves a null, not a dangle.
QPointer<UsbScaleManager> s_scaleManager;
QPointer<USBManager> s_de1Manager;

// Arrives on a binder thread. Hops to the Qt thread before touching either
// manager: both own QTimers and emit signals that drive QML.
void nativeOnUsbDeviceChanged(JNIEnv*, jclass, jint vendorId, jint productId, jboolean attached)
{
    const int pid = static_cast<int>(productId);
    const int vid = static_cast<int>(vendorId);
    const bool isAttach = (attached == JNI_TRUE);

    QMetaObject::invokeMethod(qApp, [vid, pid, isAttach]() {
        const bool isScale = (usbDeviceKindForPid(pid) == UsbDeviceKind::Scale);
        // Marker follows the DEVICE, not this file: one shared receiver does not
        // make the DE1's cable a scale event.
        const QString what = QStringLiteral("Hotplug: %1 (vid=0x%2 pid=0x%3)")
                                 .arg(isAttach ? QStringLiteral("attached")
                                               : QStringLiteral("detached"),
                                      QString::number(vid, 16), QString::number(pid, 16));
        if (isScale) HOTPLUG_INFO(what);
        else         DE1_INFO_TAGGED("USB", what);

        if (isScale) {
            if (s_scaleManager) s_scaleManager->onHotplugEvent();
        } else {
            if (s_de1Manager) s_de1Manager->onHotplugEvent();
        }
    }, Qt::QueuedConnection);
}

// A permission dialog closed. Carries no device and no verdict: the PendingIntent
// is FLAG_IMMUTABLE, so the Intent the system fills in at send() — the only carrier
// of EXTRA_DEVICE and EXTRA_PERMISSION_GRANTED — is discarded. The action says which
// manager, and that manager reads the real permission state itself.
void nativeOnUsbPermissionResult(JNIEnv*, jclass, jboolean isScale)
{
    const bool scale = (isScale == JNI_TRUE);
    QMetaObject::invokeMethod(qApp, [scale]() {
        // Logged apart from a cable attach: one physical plug-in otherwise produced
        // two identical "attached" lines, and a reader could not tell which was which.
        const QString what = QStringLiteral("USB permission dialog closed");
        if (scale) {
            HOTPLUG_INFO(what);
            if (s_scaleManager) s_scaleManager->onPermissionResult();
        } else {
            DE1_INFO_TAGGED("USB", what);
            if (s_de1Manager) s_de1Manager->onPermissionResult();
        }
    }, Qt::QueuedConnection);
}

bool registerNatives()
{
    static bool registered = false;
    if (registered) return true;
    QJniEnvironment env;
    const JNINativeMethod methods[] = {
        {"nativeOnUsbDeviceChanged", "(IIZ)V",
         reinterpret_cast<void*>(nativeOnUsbDeviceChanged)},
        {"nativeOnUsbPermissionResult", "(Z)V",
         reinterpret_cast<void*>(nativeOnUsbPermissionResult)},
    };
    if (!env.registerNativeMethods(kReceiverClass, methods, 2)) {
        HOTPLUG_WARN(QStringLiteral(
            "Could not register USB hotplug native methods — attach/detach will not "
            "be reported; the fallback scan is the only detection path"));
        return false;
    }
    registered = true;
    return true;
}

}  // namespace

void UsbHotplug::start(UsbScaleManager* scaleManager, USBManager* de1Manager)
{
    s_scaleManager = scaleManager;
    s_de1Manager = de1Manager;

    if (!registerNatives()) return;

    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        HOTPLUG_WARN(QStringLiteral("No Android context — USB hotplug not registered"));
        return;
    }
    const jint supported = QJniObject::callStaticMethod<jint>(
        kReceiverClass, "register", "(Landroid/content/Context;)I", context.object());
    if (supported <= 0) {
        // Java cannot report this: android.util.Log goes to logcat, Decenza's log
        // comes from a Qt message handler.
        HOTPLUG_WARN(QStringLiteral(
            "Hotplug armed but device_filter.xml yielded no ids — no attach or detach "
            "will be recognised (permission results still are); turn on Scan for USB "
            "devices to fall back to scanning"));
        return;
    }
    HOTPLUG_INFO(QStringLiteral("Hotplug armed for %1 supported device id(s)").arg(supported));
}

void UsbHotplug::stop()
{
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (context.isValid()) {
        QJniObject::callStaticMethod<void>(kReceiverClass, "unregister",
                                           "(Landroid/content/Context;)V", context.object());
    }
    s_scaleManager = nullptr;
    s_de1Manager = nullptr;
}

#else  // !Q_OS_ANDROID

void UsbHotplug::start(UsbScaleManager*, USBManager*) {}
void UsbHotplug::stop() {}

#endif
