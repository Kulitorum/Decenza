#pragma once

#include "machineuisnapshot.h"
#include "steampitchersnapshot.h"

#include <QJsonObject>
#include <QString>

namespace SteamEditRequest {

enum class Operation {
    SetPresetDuration,
    SetPresetFlow,
    SetPresetTemperature,
    RenamePreset,
    CreatePreset,
    DeletePreset,
    MovePreset,
    SetPitcherWeight,
    StoreCurrentPitcherWeight,
    ClearPitcherWeight,
    SetMilkAutoCapture,
    SetSteamSecondsPerGram,
    CalibrateFromLastSteam
};

struct Command {
    Operation operation = Operation::SetPresetDuration;
    int index = -1;
    int fromIndex = -1;
    int toIndex = -1;
    int durationSec = 0;
    int flow = 0;
    double temperatureC = 0.0;
    double weightG = 0.0;
    double value = 0.0;
    bool enabled = false;
    QString expectedName;
    QString newName;
    QString expectedFromName;
    QString expectedToName;
};

struct Result {
    bool valid = false;
    Operation operation = Operation::SetPresetDuration;
    QString operationName;
    QJsonObject fields;
    Command command;
    int httpStatus = 400;
    QString error;
};

// The editor token covers every persisted value the shared Steam editor can
// mutate, including the selected pitcher and global steam tuning settings.
QString editorRevision(const SteamPitcherSnapshot& pitchers,
                       const MachineUiSnapshot& machineUi);
bool isRevision(const QString& revision);
QString operationName(Operation operation);
bool operationForName(const QString& name, Operation* operation);
Result validateFields(const QString& operation, const QJsonObject& fields);

} // namespace SteamEditRequest
