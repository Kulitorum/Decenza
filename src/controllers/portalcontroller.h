#pragma once
#include <QObject>

class BelkaPortalDevice;
class SettingsHardware;
class MachineState;
class ShotTimingController;
class ShotDataModel;

// Owns capture and connection policy; the driver only owns the peripheral.
class PortalController : public QObject {
    Q_OBJECT
public:
    PortalController(BelkaPortalDevice* device, SettingsHardware* settings,
                     MachineState* machine, ShotTimingController* timing,
                     ShotDataModel* model, QObject* parent = nullptr);
private:
    bool m_captureOpen = false;
};
