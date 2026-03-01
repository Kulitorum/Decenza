#include "batterymanager.h"
#include "settings.h"
#include "../ble/de1device.h"
#include <QDebug>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QCoreApplication>
#include <QGuiApplication>
#endif

#ifdef Q_OS_IOS
#include <UIKit/UIKit.h>
#endif

BatteryManager::BatteryManager(QObject* parent)
    : QObject(parent)
{
    // Check battery every 60 seconds (like de1app)
    m_checkTimer.setInterval(60000);
    connect(&m_checkTimer, &QTimer::timeout, this, &BatteryManager::checkBattery);
    m_checkTimer.start();

    // Check if this is a Samsung tablet (must disable smart charging)
    checkSamsungTablet();

#ifdef Q_OS_ANDROID
    // Re-check battery optimization when app returns to foreground (event-based guard).
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive && m_batteryOptimizationPending) {
            m_batteryOptimizationPending = false;
            emit batteryOptimizationChanged();
        }
    });
#endif

    // Do an initial check
    checkBattery();
}

void BatteryManager::setDE1Device(DE1Device* device) {
    m_device = device;
}

void BatteryManager::setSettings(Settings* settings) {
    m_settings = settings;
    if (m_settings) {
        // Load charging mode from settings
        m_chargingMode = m_settings->value("smartBatteryCharging", On).toInt();

        emit chargingModeChanged();
    }
}

void BatteryManager::setChargingMode(int mode) {
    if (m_chargingMode == mode) {
        return;
    }
    m_chargingMode = mode;
    qDebug() << "BatteryManager: Charging mode set to" << mode;

    if (m_settings) {
        m_settings->setValue("smartBatteryCharging", mode);
    }

    // If turning off smart charging, ensure charger is ON
    if (mode == Off && m_device) {
        m_device->setUsbChargerOn(true);
    }

    emit chargingModeChanged();

    // Apply new mode immediately
    checkBattery();
}

void BatteryManager::checkBattery() {
    int newPercent = readPlatformBatteryPercent();

    if (newPercent != m_batteryPercent) {
        m_batteryPercent = newPercent;
        emit batteryPercentChanged();
    }

    applySmartCharging();
}

int BatteryManager::readPlatformBatteryPercent() {
#ifdef Q_OS_ANDROID
    // Android: Use Intent.ACTION_BATTERY_CHANGED via JNI
    QJniObject context = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "getContext",
        "()Landroid/content/Context;");

    if (!context.isValid()) {
        return 100;
    }

    // Get IntentFilter for ACTION_BATTERY_CHANGED
    QJniObject intentFilter("android/content/IntentFilter",
        "(Ljava/lang/String;)V",
        QJniObject::fromString("android.intent.action.BATTERY_CHANGED").object<jstring>());

    // Register receiver (null) to get sticky intent
    QJniObject batteryStatus = context.callObjectMethod(
        "registerReceiver",
        "(Landroid/content/BroadcastReceiver;Landroid/content/IntentFilter;)Landroid/content/Intent;",
        nullptr,
        intentFilter.object());

    if (!batteryStatus.isValid()) {
        return 100;
    }

    // Get level and scale
    jint level = batteryStatus.callMethod<jint>(
        "getIntExtra",
        "(Ljava/lang/String;I)I",
        QJniObject::fromString("level").object<jstring>(),
        -1);

    jint scale = batteryStatus.callMethod<jint>(
        "getIntExtra",
        "(Ljava/lang/String;I)I",
        QJniObject::fromString("scale").object<jstring>(),
        100);

    if (level < 0 || scale <= 0) {
        return 100;
    }

    return (level * 100) / scale;

#elif defined(Q_OS_IOS)
    // iOS: Use UIDevice battery monitoring
    [[UIDevice currentDevice] setBatteryMonitoringEnabled:YES];
    float level = [[UIDevice currentDevice] batteryLevel];

    if (level < 0) {
        // Battery level unknown
        return 100;
    }

    return static_cast<int>(level * 100);

#else
    // Desktop: No battery, return 100%
    return 100;
#endif
}

void BatteryManager::applySmartCharging() {
    if (!m_device || !m_device->isConnected()) {
        return;
    }

    bool shouldChargerBeOn = true;

    switch (m_chargingMode) {
    case Off:
        // Charger always on
        shouldChargerBeOn = true;
        break;

    case On:
        // Smart charging: 55-65%
        // Turn charger ON when battery <= 55%
        // Turn charger OFF when battery >= 65%
        if (m_discharging) {
            // Currently discharging, wait until 55%
            if (m_batteryPercent <= 55) {
                shouldChargerBeOn = true;
                m_discharging = false;
                qDebug() << "BatteryManager: Battery at" << m_batteryPercent << "%, starting charge";
            } else {
                shouldChargerBeOn = false;
            }
        } else {
            // Currently charging, wait until 65%
            if (m_batteryPercent >= 65) {
                shouldChargerBeOn = false;
                m_discharging = true;
                qDebug() << "BatteryManager: Battery at" << m_batteryPercent << "%, stopping charge";
            } else {
                shouldChargerBeOn = true;
            }
        }
        break;

    case Night:
        // Night mode: 90-95% when active, 15-95% when sleeping
        // For now, use 90-95% (we'd need machine state to check sleep)
        if (m_discharging) {
            if (m_batteryPercent <= 90) {
                shouldChargerBeOn = true;
                m_discharging = false;
                qDebug() << "BatteryManager: Night mode - battery at" << m_batteryPercent << "%, starting charge";
            } else {
                shouldChargerBeOn = false;
            }
        } else {
            if (m_batteryPercent >= 95) {
                shouldChargerBeOn = false;
                m_discharging = true;
                qDebug() << "BatteryManager: Night mode - battery at" << m_batteryPercent << "%, stopping charge";
            } else {
                shouldChargerBeOn = true;
            }
        }
        break;
    }

    // IMPORTANT: Always send the charger command with force=true.
    // The DE1 has a 10-minute timeout that automatically turns the charger back ON.
    // We must resend the command every 60 seconds to keep it off (if that's what we want).
    // This matches de1app behavior which always calls set_usb_charger_on every check.
    m_device->setUsbChargerOn(shouldChargerBeOn, true);

    // Update isCharging state
    if (m_isCharging != shouldChargerBeOn) {
        m_isCharging = shouldChargerBeOn;
        emit isChargingChanged();
    }
}

bool BatteryManager::isBatteryOptimizationIgnored() const {
#ifdef Q_OS_ANDROID
    QJniObject context = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "getContext",
        "()Landroid/content/Context;");

    if (!context.isValid()) {
        return true;  // Assume OK if we can't check
    }

    // Get PowerManager
    QJniObject powerServiceName = QJniObject::getStaticObjectField(
        "android/content/Context",
        "POWER_SERVICE",
        "Ljava/lang/String;");

    QJniObject powerManager = context.callObjectMethod(
        "getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;",
        powerServiceName.object<jstring>());

    if (!powerManager.isValid()) {
        return true;
    }

    // Get package name
    QJniObject packageName = context.callObjectMethod(
        "getPackageName",
        "()Ljava/lang/String;");

    // Check if we're ignoring battery optimizations
    jboolean isIgnoring = powerManager.callMethod<jboolean>(
        "isIgnoringBatteryOptimizations",
        "(Ljava/lang/String;)Z",
        packageName.object<jstring>());

    return isIgnoring;
#else
    return true;  // Non-Android platforms don't have this restriction
#endif
}

void BatteryManager::requestIgnoreBatteryOptimization() {
#ifdef Q_OS_ANDROID
    if (isBatteryOptimizationIgnored()) {
        return;  // Already whitelisted
    }

    QJniObject context = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "getContext",
        "()Landroid/content/Context;");

    if (!context.isValid()) {
        return;
    }

    // Get package name
    QJniObject packageName = context.callObjectMethod(
        "getPackageName",
        "()Ljava/lang/String;");

    // Create intent to request ignoring battery optimizations
    QJniObject actionString = QJniObject::getStaticObjectField(
        "android/provider/Settings",
        "ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS",
        "Ljava/lang/String;");

    // Build URI: package:com.example.app
    QString uriString = QString("package:%1").arg(packageName.toString());
    QJniObject uri = QJniObject::callStaticObjectMethod(
        "android/net/Uri",
        "parse",
        "(Ljava/lang/String;)Landroid/net/Uri;",
        QJniObject::fromString(uriString).object<jstring>());

    // Create intent
    QJniObject intent("android/content/Intent",
        "(Ljava/lang/String;Landroid/net/Uri;)V",
        actionString.object<jstring>(),
        uri.object());

    // Add FLAG_ACTIVITY_NEW_TASK flag
    jint flagNewTask = QJniObject::getStaticField<jint>(
        "android/content/Intent",
        "FLAG_ACTIVITY_NEW_TASK");
    intent.callObjectMethod(
        "addFlags",
        "(I)Landroid/content/Intent;",
        flagNewTask);

    // Start activity
    context.callMethod<void>(
        "startActivity",
        "(Landroid/content/Intent;)V",
        intent.object());

    // Flag checked when app resumes from Android Settings (applicationStateChanged)
    m_batteryOptimizationPending = true;
#endif
}

void BatteryManager::checkSamsungTablet() {
#ifdef Q_OS_ANDROID
    // Check if manufacturer contains "samsung" (case insensitive)
    // This matches de1app's tablet_is_samsung_brand check
    QJniObject manufacturer = QJniObject::getStaticObjectField(
        "android/os/Build",
        "MANUFACTURER",
        "Ljava/lang/String;");

    if (manufacturer.isValid()) {
        QString mfr = manufacturer.toString().toLower();
        m_isSamsungTablet = mfr.contains("samsung");
        if (m_isSamsungTablet) {
            qDebug() << "BatteryManager: Samsung device detected (manufacturer:" << manufacturer.toString() << ")";
        }
    }
#endif
    m_samsungCheckDone = true;
}

bool BatteryManager::isSamsungTablet() const {
    return m_isSamsungTablet;
}

bool BatteryManager::showSamsungWarning() const {
    if (!m_isSamsungTablet || !m_settings) {
        return false;
    }
    return !m_settings->value("samsungFastChargeWarningShown", false).toBool();
}

void BatteryManager::dismissSamsungWarning() {
    if (m_settings) {
        m_settings->setValue("samsungFastChargeWarningShown", true);
    }
}

void BatteryManager::openSamsungBatterySettings() {
#ifdef Q_OS_ANDROID
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (!activity.isValid()) return;

    // Try Samsung Device Care activities in order of specificity.
    // Qt's callMethod clears JNI exceptions internally, so we use PackageManager
    // to verify each component exists before attempting to start it.
    struct { const char* pkg; const char* cls; } targets[] = {
        // Charging settings page (has Fast Charging toggle directly)
        {"com.samsung.android.lool", "com.samsung.android.sm.battery.ui.BatteryAdvancedMenuActivity"},
        // Main battery page (has "Charging settings" sub-item)
        {"com.samsung.android.lool", "com.samsung.android.sm.battery.ui.BatteryActivity"},
    };

    QJniObject pm = activity.callObjectMethod("getPackageManager",
        "()Landroid/content/pm/PackageManager;");

    for (const auto& t : targets) {
        QJniObject intent("android/content/Intent");
        QJniObject component("android/content/ComponentName",
            "(Ljava/lang/String;Ljava/lang/String;)V",
            QJniObject::fromString(t.pkg).object<jstring>(),
            QJniObject::fromString(t.cls).object<jstring>());
        intent.callObjectMethod("setComponent",
            "(Landroid/content/ComponentName;)Landroid/content/Intent;",
            component.object());
        intent.callMethod<QJniObject>("addFlags", "(I)Landroid/content/Intent;", 0x10000000);

        // queryIntentActivities returns a non-empty list if the activity actually exists
        QJniObject matches = pm.callObjectMethod("queryIntentActivities",
            "(Landroid/content/Intent;I)Ljava/util/List;",
            intent.object(), 0);
        if (matches.isValid() && matches.callMethod<jint>("size") > 0) {
            qDebug() << "BatteryManager: Opening" << t.cls;
            activity.callMethod<void>("startActivity", "(Landroid/content/Intent;)V", intent.object());
            return;
        }
    }

    // Fallback: standard Android battery settings
    qDebug() << "BatteryManager: Samsung activities not found, opening standard battery settings";
    QJniObject action = QJniObject::fromString("android.intent.action.POWER_USAGE_SUMMARY");
    QJniObject fallback("android/content/Intent", "(Ljava/lang/String;)V", action.object<jstring>());
    fallback.callMethod<QJniObject>("addFlags", "(I)Landroid/content/Intent;", 0x10000000);
    if (fallback.isValid()) {
        activity.callMethod<void>("startActivity", "(Landroid/content/Intent;)V", fallback.object());
    }
#endif
}

void BatteryManager::ensureChargerOn() {
    // Safety function: Always turn charger ON when app exits or goes to background
    // This prevents the tablet from dying if left unattended with smart charging enabled
    // Matches de1app's app_exit behavior
    if (m_device && m_device->isConnected()) {
        qDebug() << "BatteryManager: Ensuring charger is ON (app exit/suspend safety)";
        m_device->setUsbChargerOn(true, true);  // force=true to ensure it's sent
    }
}
