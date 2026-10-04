#pragma once

#include <QByteArray>
#include <QString>

class ShotProjection;

// The machine a shot is filed under in the Decent account.
struct DecentMachineIdentity {
    QString serialNumber;
    QString firmwareVersion;  // "" when unknown
    QString model;            // "" when unknown
};

// Serializes a saved shot as a Decaid ShotRecord document for
// POST /support/api/shot_upload. The field mapping follows de1app's
// plugins/shot_upload/converter.tcl, whose documents the server accepts.
// Pure and thread-safe: called on a worker thread.
namespace DecentShotRecord {

QByteArray build(const ShotProjection& shot, const DecentMachineIdentity& machine);

// DE1Device::machineModel() → the model string Decaid sends ("" for unknown).
QString modelName(int machineModel);

}
