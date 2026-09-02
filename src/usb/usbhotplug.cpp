#include "usbhotplug.h"

#include "usbscalemanager.h"
#include "usbmanager.h"
#include "../ble/scales/scalelogging.h"

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>
#include <QPointer>
#endif

#define HOTPLUG_INFO(msg) SCALE_LOG_STDERR_TAGGED("USB Scale", msg)
#define HOTPLUG_WARN(msg) SCALE_WARN_STDERR_TAGGED("USB Scale", msg)

#ifdef Q_OS_ANDROID

namespace {

constexpr const char* kReceiverClass =
    "io/github/kulitorum/decenza_de1/UsbHotplugReceiver";

// The managers the JNI callback routes to. QPointer so a manager destroyed
// before stop() runs leaves a null rather than a dangling pointer — the callback
// arrives from a system thread and cannot check lifetimes any other way.
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
        HOTPLUG_INFO(QStringLiteral("Hotplug: %1 %2 (vid=0x%3 pid=0x%4)")
                         .arg(isScale ? QStringLiteral("scale") : QStringLiteral("DE1"),
                              isAttach ? QStringLiteral("attached") : QStringLiteral("detached"),
                              QString::number(vid, 16), QString::number(pid, 16)));

        // Both paths run the manager's normal probe pass, which is driven by
        // whether the device is present rather than by what triggered it — so
        // attach and detach need no separate handling here.
        if (isScale) {
            if (s_scaleManager) s_scaleManager->onHotplugEvent();
        } else {
            if (s_de1Manager) s_de1Manager->onHotplugEvent();
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
    };
    if (!env.registerNativeMethods(kReceiverClass, methods, 1)) {
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
    QJniObject::callStaticMethod<void>(kReceiverClass, "register",
                                       "(Landroid/content/Context;)V", context.object());
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
