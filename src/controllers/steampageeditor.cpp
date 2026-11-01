#include "steampageeditor.h"

#include "../core/settings.h"
#include "core/uiworkstats.h"
#include "../core/settings_brew.h"
#include "../machine/machinestate.h"
#include "../ble/scaledevice.h"
#include "../network/machineuisnapshot.h"
#include "../network/steampitchersnapshot.h"

#include <QJsonObject>
#include <QRegularExpression>
#include <QUuid>

#include <cmath>

namespace {
bool validRequestId(const QString& requestId)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"));
    return pattern.match(requestId).hasMatch();
}

bool applied(SettingsBrew::PresetMutationResult result)
{
    return result == SettingsBrew::PresetMutationResult::Applied
        || result == SettingsBrew::PresetMutationResult::NoChange;
}

QString mutationError(SettingsBrew::PresetMutationResult result)
{
    if (result == SettingsBrew::PresetMutationResult::Unreadable)
        return QStringLiteral("The saved steam pitcher data could not be read.");
    if (result == SettingsBrew::PresetMutationResult::Invalid)
        return QStringLiteral("The steam pitcher changed or the requested value is invalid.");
    return {};
}
} // namespace

SteamPageEditor::SteamPageEditor(SettingsBrew* brew, MachineState* machineState,
                                 QObject* parent)
    : QObject(parent)
    , m_brew(brew)
    , m_machineState(machineState)
{
    if (!m_brew)
        return;

    for (auto signal : {&SettingsBrew::steamPitcherPresetsChanged,
                        &SettingsBrew::selectedSteamPitcherChanged,
                        &SettingsBrew::steamTemperatureChanged,
                        &SettingsBrew::steamTimeoutChanged,
                        &SettingsBrew::steamFlowChanged,
                        &SettingsBrew::milkAutoCaptureEnabledChanged,
                        &SettingsBrew::steamSecondsPerGramChanged}) {
        connect(m_brew, signal, this, [this] {
            m_revisionValid = false;
            emit editorRevisionChanged();
        });
    }
}

QString SteamPageEditor::editorRevision() const
{
    if (!m_brew)
        return {};

    if (m_revisionValid)
        return m_cachedRevision;
    UiWorkStats::Span work(UiWorkStats::Kind::SteamEditorRevision);
    MachineUiSnapshot machineUi;
    machineUi.brewSteamTemperature = m_brew->steamTemperature();
    machineUi.brewSteamTimeout = m_brew->steamTimeout();
    machineUi.brewSteamFlow = m_brew->steamFlow();
    machineUi.brewMilkAutoCaptureEnabled = m_brew->milkAutoCaptureEnabled();
    machineUi.brewSteamSecondsPerGram = m_brew->steamSecondsPerGram();
    const SteamPitcherSnapshot pitchers = SteamPitcherSnapshot::project(
        m_brew->steamPitcherPresets(), m_brew->selectedSteamPitcher());
    work.items = pitchers.presets.size();
    m_cachedRevision = SteamEditRequest::editorRevision(pitchers, machineUi);
    m_revisionValid = true;
    return m_cachedRevision;
}

QString SteamPageEditor::newRequestId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool SteamPageEditor::submit(const QString& operation, const QJsonObject& fields,
                             const QString& requestId, const QString& expectedRevision)
{
    const SteamEditRequest::Result validated = SteamEditRequest::validateFields(operation, fields);
    if (!validated.valid) {
        finish(requestId, operation, false, validated.error);
        return false;
    }
    return dispatch(validated.command, expectedRevision, requestId);
}

bool SteamPageEditor::setPresetDuration(const QString& requestId, const QString& revision,
                                        int index, const QString& expectedName, int durationSec)
{
    return submit(QStringLiteral("set-preset-duration"),
                  {{QStringLiteral("index"), index},
                   {QStringLiteral("expectedName"), expectedName},
                   {QStringLiteral("durationSec"), durationSec}}, requestId, revision);
}

bool SteamPageEditor::setPresetFlow(const QString& requestId, const QString& revision,
                                    int index, const QString& expectedName, int flow)
{
    return submit(QStringLiteral("set-preset-flow"),
                  {{QStringLiteral("index"), index},
                   {QStringLiteral("expectedName"), expectedName},
                   {QStringLiteral("flow"), flow}}, requestId, revision);
}

bool SteamPageEditor::setPresetTemperatureC(const QString& requestId, const QString& revision,
                                            int index, const QString& expectedName,
                                            double temperatureC)
{
    return submit(QStringLiteral("set-preset-temperature"),
                  {{QStringLiteral("index"), index},
                   {QStringLiteral("expectedName"), expectedName},
                   {QStringLiteral("temperatureC"), temperatureC}}, requestId, revision);
}

bool SteamPageEditor::renamePreset(const QString& requestId, const QString& revision,
                                   int index, const QString& expectedName, const QString& newName)
{
    return submit(QStringLiteral("rename-preset"),
                  {{QStringLiteral("index"), index},
                   {QStringLiteral("expectedName"), expectedName},
                   {QStringLiteral("newName"), newName}}, requestId, revision);
}

bool SteamPageEditor::createPreset(const QString& requestId, const QString& revision,
                                   const QString& name)
{
    return submit(QStringLiteral("create-preset"),
                  {{QStringLiteral("name"), name}}, requestId, revision);
}

bool SteamPageEditor::deletePreset(const QString& requestId, const QString& revision,
                                   int index, const QString& expectedName)
{
    return submit(QStringLiteral("delete-preset"),
                  {{QStringLiteral("index"), index},
                   {QStringLiteral("expectedName"), expectedName}}, requestId, revision);
}

bool SteamPageEditor::movePreset(const QString& requestId, const QString& revision,
                                 int fromIndex, const QString& expectedFromName,
                                 int toIndex, const QString& expectedToName)
{
    return submit(QStringLiteral("move-preset"),
                  {{QStringLiteral("fromIndex"), fromIndex},
                   {QStringLiteral("expectedFromName"), expectedFromName},
                   {QStringLiteral("toIndex"), toIndex},
                   {QStringLiteral("expectedToName"), expectedToName}}, requestId, revision);
}

bool SteamPageEditor::setPitcherWeight(const QString& requestId, const QString& revision,
                                       int index, const QString& expectedName, double weightG)
{
    return submit(QStringLiteral("set-pitcher-weight"),
                  {{QStringLiteral("index"), index},
                   {QStringLiteral("expectedName"), expectedName},
                   {QStringLiteral("weightG"), weightG}}, requestId, revision);
}

bool SteamPageEditor::storeCurrentPitcherWeight(const QString& requestId,
                                                const QString& revision, int index,
                                                const QString& expectedName)
{
    return submit(QStringLiteral("store-current-pitcher-weight"),
                  {{QStringLiteral("index"), index},
                   {QStringLiteral("expectedName"), expectedName}}, requestId, revision);
}

bool SteamPageEditor::clearPitcherWeight(const QString& requestId, const QString& revision,
                                         int index, const QString& expectedName)
{
    return submit(QStringLiteral("clear-pitcher-weight"),
                  {{QStringLiteral("index"), index},
                   {QStringLiteral("expectedName"), expectedName}}, requestId, revision);
}

bool SteamPageEditor::setMilkAutoCaptureEnabled(const QString& requestId,
                                                const QString& revision, bool enabled)
{
    return submit(QStringLiteral("set-milk-auto-capture"),
                  {{QStringLiteral("enabled"), enabled}}, requestId, revision);
}

bool SteamPageEditor::setSteamSecondsPerGram(const QString& requestId,
                                             const QString& revision, double value)
{
    return submit(QStringLiteral("set-steam-seconds-per-gram"),
                  {{QStringLiteral("value"), value}}, requestId, revision);
}

bool SteamPageEditor::calibrateFromLastSteam(const QString& requestId,
                                             const QString& revision)
{
    return submit(QStringLiteral("calibrate-from-last-steam"), {}, requestId, revision);
}

bool SteamPageEditor::dispatch(const SteamEditRequest::Command& command,
                               const QString& expectedRevision, const QString& requestId)
{
    const QString operation = SteamEditRequest::operationName(command.operation);
    if (m_busy) {
        finish(requestId, operation, false, QStringLiteral("Another steam editor change is still being applied."));
        return false;
    }
    if (!validRequestId(requestId)) {
        finish(requestId, operation, false, QStringLiteral("The steam editor request is invalid."));
        return false;
    }
    if (!m_brew) {
        finish(requestId, operation, false, QStringLiteral("Steam settings are unavailable."));
        return false;
    }
    if (!SteamEditRequest::isRevision(expectedRevision) || expectedRevision != editorRevision()) {
        finish(requestId, operation, false,
               QStringLiteral("Steam settings changed. Review the latest values and try again."));
        return false;
    }

    setBusy(true);
    setErrorMessage({});
    bool success = false;
    QString message;
    QVariantMap result;
    const auto failMutation = [&message](SettingsBrew::PresetMutationResult status) {
        message = mutationError(status);
        return applied(status);
    };
    const auto currentPresetMatches = [this](int index, const QString& expectedName) {
        const QVariantMap preset = m_brew->getSteamPitcherPreset(index);
        return !preset.isEmpty() && !preset.value(QStringLiteral("disabled")).toBool()
            && preset.value(QStringLiteral("name")).toString() == expectedName;
    };

    switch (command.operation) {
    case SteamEditRequest::Operation::SetPresetDuration:
        if (command.index != m_brew->selectedSteamPitcher()
            || !currentPresetMatches(command.index, command.expectedName)) {
            message = QStringLiteral("The selected steam pitcher changed. Review it and try again.");
            break;
        }
        success = failMutation(m_brew->trySetSteamPitcherDuration(
            command.index, command.expectedName, command.durationSec));
        if (success)
            m_brew->setSteamTimeout(command.durationSec);
        break;
    case SteamEditRequest::Operation::SetPresetFlow:
        if (command.index != m_brew->selectedSteamPitcher()
            || !currentPresetMatches(command.index, command.expectedName)) {
            message = QStringLiteral("The selected steam pitcher changed. Review it and try again.");
            break;
        }
        success = failMutation(m_brew->trySetSteamPitcherFlow(
            command.index, command.expectedName, command.flow));
        if (success) {
            m_brew->setSteamFlow(command.flow);
            emit steamFlowCommitRequested(command.flow);
        }
        break;
    case SteamEditRequest::Operation::SetPresetTemperature:
        if (command.index != m_brew->selectedSteamPitcher()
            || !currentPresetMatches(command.index, command.expectedName)) {
            message = QStringLiteral("The selected steam pitcher changed. Review it and try again.");
            break;
        }
        success = failMutation(m_brew->trySetSteamPitcherTemperature(
            command.index, command.expectedName, command.temperatureC));
        if (success)
            emit steamTemperatureCommitRequested(command.temperatureC);
        break;
    case SteamEditRequest::Operation::RenamePreset:
        success = failMutation(m_brew->tryRenameSteamPitcherPreset(
            command.index, command.expectedName, command.newName));
        break;
    case SteamEditRequest::Operation::CreatePreset: {
        const int index = m_brew->steamPitcherCount();
        success = failMutation(m_brew->tryAddSteamPitcherPreset(
            command.newName, 30, 150, m_brew->steamTemperature()));
        if (success) {
            m_brew->setSelectedSteamCup(index);
            result.insert(QStringLiteral("index"), index);
        }
        break;
    }
    case SteamEditRequest::Operation::DeletePreset:
        success = failMutation(m_brew->tryRemoveSteamPitcherPreset(
            command.index, command.expectedName));
        break;
    case SteamEditRequest::Operation::MovePreset:
        success = failMutation(m_brew->tryMoveSteamPitcherPreset(
            command.fromIndex, command.expectedFromName,
            command.toIndex, command.expectedToName));
        break;
    case SteamEditRequest::Operation::SetPitcherWeight:
        success = failMutation(m_brew->trySetSteamPitcherWeight(
            command.index, command.expectedName, command.weightG));
        break;
    case SteamEditRequest::Operation::StoreCurrentPitcherWeight: {
        const auto* scale = m_machineState ? m_machineState->scale() : nullptr;
        if (!scale || !scale->isConnected() || scale->isFlowScale()) {
            message = QStringLiteral("A connected weight scale is required to weigh the pitcher.");
            break;
        }
        const double weightG = m_machineState->scaleWeight();
        if (!std::isfinite(weightG) || weightG < 0.0 || weightG > 1000.0) {
            message = QStringLiteral("The scale reading is outside the valid pitcher-weight range.");
            break;
        }
        success = failMutation(m_brew->trySetSteamPitcherWeight(
            command.index, command.expectedName, weightG));
        if (success)
            result.insert(QStringLiteral("weightG"), weightG);
        break;
    }
    case SteamEditRequest::Operation::ClearPitcherWeight:
        success = failMutation(m_brew->trySetSteamPitcherWeight(
            command.index, command.expectedName, 0.0));
        if (success)
            result.insert(QStringLiteral("weightG"), 0.0);
        break;
    case SteamEditRequest::Operation::SetMilkAutoCapture:
        m_brew->setMilkAutoCaptureEnabled(command.enabled);
        success = true;
        break;
    case SteamEditRequest::Operation::SetSteamSecondsPerGram:
        m_brew->setSteamSecondsPerGram(command.value);
        success = true;
        break;
    case SteamEditRequest::Operation::CalibrateFromLastSteam: {
        const double milkG = m_brew->lastSteamMilkG();
        const double timeS = m_brew->lastSteamTimeS();
        if (!std::isfinite(milkG) || !std::isfinite(timeS) || milkG <= 0.0 || timeS <= 0.0) {
            message = QStringLiteral("A completed weighed steam session is required for calibration.");
            break;
        }
        m_brew->calibrateSteamFromReference(milkG, timeS);
        success = true;
        break;
    }
    }

    if (!success && message.isEmpty())
        message = QStringLiteral("The steam setting could not be changed.");
    finish(requestId, operation, success, message, result);
    setBusy(false);
    return success;
}

void SteamPageEditor::finish(const QString& requestId, const QString& operation,
                             bool success, const QString& message, const QVariantMap& result)
{
    setErrorMessage(success ? QString() : message);
    emit operationCompleted(requestId, operation, success, message, result, editorRevision());
}

void SteamPageEditor::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void SteamPageEditor::setErrorMessage(const QString& message)
{
    if (m_errorMessage == message)
        return;
    m_errorMessage = message;
    emit errorMessageChanged();
}
