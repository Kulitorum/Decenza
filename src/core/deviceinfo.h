#pragma once

#include <QString>
#include <QSysInfo>

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#endif

// What device this is, read once. Header-only for the reason logpaths.h gives:
// crashhandler.cpp and mcpremoteaccess.cpp are compiled into different test
// targets, and inline functions cost neither of them a link dependency.
//
// On Android QSysInfo cannot answer this: productType() is "android" and
// machineHostName() is "localhost", which is what every Android crash report
// said about its device until this existed.
namespace DeviceInfo {

struct AndroidBuild {
    QString manufacturer;
    QString model;
    QString release;      // Build.VERSION.RELEASE, e.g. "14"
    int sdkInt = -1;      // -1 when unreadable, and on every other platform
    // Why sdkInt is -1, for BLEManager's one-time warning.
    bool sdkIntThrew = false;
    int sdkIntRaw = 0;
};

// Build.* are permanent characteristics of the OS image, so this is cached.
inline const AndroidBuild& androidBuild()
{
    static const AndroidBuild cached = []() {
        AndroidBuild b;
#ifdef Q_OS_ANDROID
        QJniEnvironment env;
        const auto field = [&env](const char* cls, const char* name) {
            const QJniObject v = QJniObject::getStaticObjectField<jstring>(cls, name);
            env.checkAndClearExceptions();
            return v.isValid() ? v.toString().trimmed() : QString();
        };
        b.manufacturer = field("android/os/Build", "MANUFACTURER");
        b.model = field("android/os/Build", "MODEL");
        b.release = field("android/os/Build$VERSION", "RELEASE");
        b.sdkIntRaw = QJniObject::getStaticField<jint>("android/os/Build$VERSION", "SDK_INT");
        b.sdkIntThrew = env.checkAndClearExceptions();
        b.sdkInt = (b.sdkIntThrew || b.sdkIntRaw <= 0) ? -1 : b.sdkIntRaw;
#endif
        return b;
    }();
    return cached;
}

// One line naming the device, for crash reports: "samsung SM-X210, Android 14
// (API 34)" on Android, QSysInfo's product name elsewhere.
inline QString description()
{
#ifdef Q_OS_ANDROID
    const AndroidBuild& b = androidBuild();
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
