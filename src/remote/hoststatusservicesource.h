#pragma once

#include "rep_hoststatusservice_source.h"

class WeatherManager;
class BatteryManager;

// Low-frequency host status. Browsers observe the host forecast and charger;
// they never query their own location, weather provider or battery hardware.
class NativeHostStatusServiceSource final : public HostStatusStateSimpleSource
{
    Q_OBJECT
public:
    NativeHostStatusServiceSource(WeatherManager* weather, BatteryManager* battery,
                                  QObject* parent = nullptr);
};
