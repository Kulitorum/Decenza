#pragma once

#include <QString>
#include <QSysInfo>

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#include <QMutex>
#include <QMutexLocker>
#endif

// What device this is. Header-only for the reason logpaths.h gives:
// crashhandler.cpp and mcpremoteaccess.cpp are compiled into different test
// targets, and inline functions cost neither of them a link dependency.
//
// On Android QSysInfo cannot answer this: productType() is "android" and
// machineHostName() is "localhost".
namespace DeviceInfo {

struct AndroidBuild {
    QString manufacturer;
    QString model;
    QString release;      // Build.VERSION.RELEASE, e.g. "14"
    int sdkInt = -1;      // -1 when unreadable, and on every other platform
    int sdkIntRaw = 0;    // what the read returned, for BLEManager's warning
};

// Build.* are permanent characteristics of the OS image, so a successful read
// is cached. A failed one is not: the first caller is CrashHandler::install(),
// before the app object exists, and CrashHandler::refreshDeviceLine() reads
// again once it does. A failure shows only as an empty value or -1.
inline AndroidBuild androidBuild()
{
#ifdef Q_OS_ANDROID
    static QMutex mutex;
    static AndroidBuild cached;
    QMutexLocker lock(&mutex);
    if (cached.sdkInt < 0 || cached.model.isEmpty()) {
        // This overload leaves an exception pending (qjniobject.cpp:1318-1333),
        // which would poison the next JNI call; getStaticField<jint> clears its own.
        const auto field = [](const char* cls, const char* name) {
            const QJniObject v = QJniObject::getStaticObjectField<jstring>(cls, name);
            QJniEnvironment().checkAndClearExceptions();
            return v.isValid() ? v.toString().trimmed() : QString();
        };
        cached.manufacturer = field("android/os/Build", "MANUFACTURER");
        cached.model = field("android/os/Build", "MODEL");
        cached.release = field("android/os/Build$VERSION", "RELEASE");
        cached.sdkIntRaw = QJniObject::getStaticField<jint>("android/os/Build$VERSION", "SDK_INT");
        cached.sdkInt = cached.sdkIntRaw > 0 ? cached.sdkIntRaw : -1;
    }
    return cached;
#else
    return {};
#endif
}

// One line naming the device, for crash reports: "samsung SM-X210, Android 14
// (API 34)" on Android, QSysInfo's product name elsewhere.
inline QString description()
{
#ifdef Q_OS_ANDROID
    const AndroidBuild b = androidBuild();
    QString device = b.model;
    // Some MODEL strings already start with the manufacturer.
    if (!b.manufacturer.isEmpty() && !device.startsWith(b.manufacturer, Qt::CaseInsensitive))
        device = b.manufacturer + QLatin1Char(' ') + device;
    if (device.trimmed().isEmpty())
        device = QStringLiteral("unknown device");
    return QStringLiteral("%1, Android %2 (API %3)")
        .arg(device.simplified(),
             b.release.isEmpty() ? QStringLiteral("?") : b.release,
             b.sdkInt > 0 ? QString::number(b.sdkInt) : QStringLiteral("?"));
#else
    return QSysInfo::prettyProductName().simplified();
#endif
}

} // namespace DeviceInfo
