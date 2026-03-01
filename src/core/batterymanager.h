#pragma once

#include <QObject>
#include <QTimer>

class DE1Device;
class Settings;

class BatteryManager : public QObject {
    Q_OBJECT

    Q_PROPERTY(int batteryPercent READ batteryPercent NOTIFY batteryPercentChanged)
    Q_PROPERTY(bool isCharging READ isCharging NOTIFY isChargingChanged)
    Q_PROPERTY(int chargingMode READ chargingMode WRITE setChargingMode NOTIFY chargingModeChanged)
    Q_PROPERTY(bool batteryOptimizationIgnored READ isBatteryOptimizationIgnored NOTIFY batteryOptimizationChanged)
    Q_PROPERTY(bool isSamsungTablet READ isSamsungTablet CONSTANT)
    Q_PROPERTY(bool showSamsungWarning READ showSamsungWarning CONSTANT)

public:
    // Charging modes (matching de1app)
    enum ChargingMode {
        Off = 0,    // Charger always ON (no smart control)
        On = 1,     // Smart charging 55-65%
        Night = 2   // Smart charging 90-95% active, 15-95% sleep
    };
    Q_ENUM(ChargingMode)

    explicit BatteryManager(QObject* parent = nullptr);

    void setDE1Device(DE1Device* device);
    void setSettings(Settings* settings);

    int batteryPercent() const { return m_batteryPercent; }
    bool isCharging() const { return m_isCharging; }
    int chargingMode() const { return m_chargingMode; }
    bool isBatteryOptimizationIgnored() const;
    bool isSamsungTablet() const;
    bool showSamsungWarning() const;

    // Ensure charger is ON - call on app exit/suspend for safety
    void ensureChargerOn();

public slots:
    void setChargingMode(int mode);
    void checkBattery();
    Q_INVOKABLE void requestIgnoreBatteryOptimization();
    Q_INVOKABLE void openSamsungBatterySettings();
    Q_INVOKABLE void dismissSamsungWarning();

signals:
    void batteryPercentChanged();
    void isChargingChanged();
    void chargingModeChanged();
    void batteryOptimizationChanged();

private:
    int readPlatformBatteryPercent();
    void applySmartCharging();
    void checkSamsungTablet();

    DE1Device* m_device = nullptr;
    Settings* m_settings = nullptr;
    QTimer m_checkTimer;

    int m_batteryPercent = 100;
    bool m_isCharging = true;
    int m_chargingMode = On;  // Default: smart charging (55-65%)
    bool m_discharging = false;  // Track charge/discharge cycle
    bool m_isSamsungTablet = false;
    bool m_samsungCheckDone = false;
    bool m_batteryOptimizationPending = false;
};
