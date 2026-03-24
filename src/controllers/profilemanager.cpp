#include "profilemanager.h"
#include "../core/settings.h"
#include "../ble/de1device.h"
#include "../machine/machinestate.h"
#include "../core/profilestorage.h"

// TODO: Phase 1 stub — implementations will be moved from maincontroller.cpp
// in subsequent phases. Each method currently returns a default/no-op.

ProfileManager::ProfileManager(Settings* settings, DE1Device* device,
                               MachineState* machineState,
                               ProfileStorage* profileStorage,
                               QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_device(device)
    , m_machineState(machineState)
    , m_profileStorage(profileStorage)
{
}
