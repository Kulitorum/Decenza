#pragma once
#include <QObject>

class BelkaPortalDevice;
class SettingsHardware;
class MachineState;
class ShotTimingController;
class ShotDataModel;

// Bridges shot capture, settings and machine phases to the PORTAL driver.
class PortalController : public QObject {
    Q_OBJECT
public:
    PortalController(BelkaPortalDevice* device, SettingsHardware* settings,
                     MachineState* machine, ShotTimingController* timing,
                     ShotDataModel* model, QObject* parent = nullptr);
private:
    bool m_captureOpen = false;
};
