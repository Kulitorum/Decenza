#include "hoststatusservicesource.h"
#include "weather/weathermanager.h"
#include "core/batterymanager.h"

NativeHostStatusServiceSource::NativeHostStatusServiceSource(
    WeatherManager* weather, BatteryManager* battery, QObject* parent)
    : HostStatusStateSimpleSource(parent)
{
    const auto refreshWeather = [this, weather] {
        setWeather({{QStringLiteral("valid"), weather->valid()},
                    {QStringLiteral("loading"), weather->loading()},
                    {QStringLiteral("provider"), weather->providerName()},
                    {QStringLiteral("locationName"), weather->locationName()},
                    {QStringLiteral("hourlyForecast"), weather->hourlyForecast()},
                    {QStringLiteral("useImperialUnits"), weather->useImperialUnits()}});
    };
    connect(weather, &WeatherManager::weatherChanged, this, refreshWeather);
    connect(weather, &WeatherManager::loadingChanged, this, refreshWeather);
    refreshWeather();

    const auto refreshBattery = [this, battery] {
        setBatteryPercent(battery->batteryPercent());
        setIsCharging(battery->isCharging());
        setChargingMode(battery->chargingMode());
        setUsbChargerEnabled(battery->usbChargerEnabled());
    };
    connect(battery, &BatteryManager::batteryPercentChanged, this, refreshBattery);
    connect(battery, &BatteryManager::isChargingChanged, this, refreshBattery);
    connect(battery, &BatteryManager::chargingModeChanged, this, refreshBattery);
    connect(battery, &BatteryManager::usbChargerEnabledChanged, this, refreshBattery);
    refreshBattery();
}
