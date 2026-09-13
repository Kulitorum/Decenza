#pragma once

#include "belkaportaldevice.h"

// Shared by the app and event-replay tests, which supply a discovery source
// without starting platform Bluetooth, WiFi or USB discovery.
template <typename Discovery>
void connectPortalDiscovery(Discovery* discovery, BelkaPortalDevice* portal)
{
    QObject::connect(discovery, &Discovery::portalDiscovered,
                     portal, &BelkaPortalDevice::observeDevice);
    // scanningChanged also reports WiFi/USB progress while BLE is still active.
    // Only a new BLE scan may discard the peripherals already discovered.
    QObject::connect(discovery, &Discovery::scanStarted,
                     portal, &BelkaPortalDevice::beginScan);
    QObject::connect(portal, &BelkaPortalDevice::scanRequested,
                     discovery, &Discovery::scanForDevices);
}
