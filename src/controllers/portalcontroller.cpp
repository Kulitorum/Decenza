#include "portalcontroller.h"
#include "ble/belkaportaldevice.h"
#include "core/settings_hardware.h"
#include "machine/machinestate.h"
#include "controllers/shottimingcontroller.h"
#include "models/shotdatamodel.h"
#include <QDateTime>

PortalController::PortalController(BelkaPortalDevice* device, SettingsHardware* settings,
                                   MachineState* machine, ShotTimingController* timing,
                                   ShotDataModel* model, QObject* parent)
    : QObject(parent)
{
    const auto restore = [device, settings] {
        device->restoreSavedDevice(settings->portalAddress(), settings->portalName());
    };
    const auto syncDisplay = [device, settings] { device->setSyncDisplay(settings->portalSyncDisplay()); };
    // Restore before wiring the reverse direction; both setters are idempotent.
    syncDisplay();
    restore();
    connect(settings, &SettingsHardware::portalDeviceChanged, this, restore);
    connect(settings, &SettingsHardware::portalSyncDisplayChanged, this, syncDisplay);
    connect(device, &BelkaPortalDevice::savedDeviceChanged, this, [device, settings] {
        settings->setPortalDevice(device->savedAddress(), device->savedName());
    });
    connect(device, &BelkaPortalDevice::syncDisplayChanged, this, [device, settings] {
        settings->setPortalSyncDisplay(device->syncDisplay());
    });
    connect(timing, &ShotTimingController::extractionClockStarted, this, [this, device, model] {
        m_captureOpen = true;
        model->markPortalGap();
        device->setExtractionActive(true);
    });
    connect(timing, &ShotTimingController::shotProcessingReady, this, [this, device] {
        m_captureOpen = false;
        device->setExtractionActive(false);
    });
    connect(device, &BelkaPortalDevice::readingInterrupted, model, &ShotDataModel::markPortalGap);
    connect(device, &BelkaPortalDevice::measurementReceived, this,
            [this, timing, model](double ecRaw, double temperatureC) {
        if (!m_captureOpen) return;
        const auto time = timing->sensorTime(QDateTime::currentMSecsSinceEpoch());
        if (time) model->addPortalSample(*time, ecRaw, temperatureC);
    });
    const auto updateMachine = [this, machine, device, model] {
        const auto phase = machine->phase();
        using Phase = MachineState::Phase;
        if (phase == Phase::Disconnected) {
            m_captureOpen = false;
            device->setExtractionActive(false);
            model->markPortalGap();
        }
        device->setMachineBusy(phase != Phase::Disconnected && phase != Phase::Sleep
            && phase != Phase::Idle && phase != Phase::Heating && phase != Phase::Ready);
    };
    connect(machine, &MachineState::phaseChanged, this, updateMachine);
    updateMachine();
    // Saved devices reconnect when BLE discovery observes them. With USB-only
    // startup, the user's shared scan supplies that observation.
}
