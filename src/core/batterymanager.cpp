#include "batterymanager.h"
#include "settings.h"
#include "../ble/de1device.h"
#include <QDebug>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QCoreApplication>
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
