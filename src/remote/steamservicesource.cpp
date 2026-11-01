#include "steamservicesource.h"
#include "servicevalue.h"

#include "controllers/maincontroller.h"
#include "controllers/preparationcontroller.h"
#include "controllers/steampageeditor.h"
#include "ai/livesteamcoach.h"
#include "core/settings.h"
#include "core/settings_app.h"
#include "core/settings_brew.h"
#include "core/settings_graph.h"
#include "core/settings_hardware.h"
#include "core/steamcalculations.h"
#include "core/steampitcherselection.h"
#include "machine/machinestate.h"
#include "machine/steamhealthtracker.h"
#include "models/steamdatamodel.h"
#include "ble/de1device.h"
#include "ble/scaledeviceproxy.h"
#include "network/steameditrequest.h"
#include "network/steamhealthsnapshot.h"

#include <QJsonObject>
#include <QMetaType>
#include <QMetaObject>
#include <QUuid>

#include <cmath>
#include <limits>
#include <utility>

namespace {

bool readInteger(const QVariantMap& map, const QString& key, int* output)
{
    const QVariant value = map.value(key);
    const int type = value.metaType().id();
    if (type == QMetaType::Bool || type == QMetaType::QString)
        return false;
    bool ok = false;
    const double number = value.toDouble(&ok);
    if (!ok || !std::isfinite(number) || std::trunc(number) != number
        || number < std::numeric_limits<int>::min()
        || number > std::numeric_limits<int>::max())
        return false;
    *output = static_cast<int>(number);
    return true;
}

bool readBool(const QVariantMap& map, const QString& key, bool* output)
{
    const QVariant value = map.value(key);
    if (value.metaType().id() != QMetaType::Bool)
        return false;
    *output = value.toBool();
    return true;
}

} // namespace

NativeSteamServiceSource::NativeSteamServiceSource(MainController* controller,
                                                   Settings* settings,
                                                   MachineState* machineState,
                                                   DE1Device* device,
                                                   ScaleDeviceProxy* scale,
                                                   QObject* parent)
    : SteamStateSimpleSource(parent)
    , m_controller(controller)
    , m_settings(settings)
    , m_machineState(machineState)
    , m_device(device)
    , m_scale(scale)
    , m_graph(controller ? controller->steamDataModel() : nullptr)
    , m_health(controller ? controller->steamHealthTracker() : nullptr)
    , m_editor(controller ? controller->steamPageEditor() : nullptr)
    , m_preparation(controller ? controller->preparation() : nullptr)
{
    if (m_settings) {
        auto* brew = m_settings->brew();
        for (auto signal : {&SettingsBrew::steamPitcherPresetsChanged,
                            &SettingsBrew::selectedSteamPitcherChanged,
                            &SettingsBrew::selectedSteamPitcherDisplayIndexChanged,
                            &SettingsBrew::steamTemperatureChanged,
                            &SettingsBrew::steamTimeoutChanged,
                            &SettingsBrew::steamFlowChanged,
                            &SettingsBrew::steamAutoFlushSecondsChanged,
                            &SettingsBrew::steamSecondsPerGramChanged,
                            &SettingsBrew::milkAutoCaptureEnabledChanged,
                            &SettingsBrew::lastSteamMilkGChanged,
                            &SettingsBrew::lastSteamTimeSChanged})
            connect(brew, signal, this, &NativeSteamServiceSource::scheduleSettingsRefresh);

        auto* app = m_settings->app();
        for (auto signal : {&SettingsApp::steamViewModeChanged,
                            &SettingsApp::steamCoachVisualEnabledChanged,
                            &SettingsApp::steamCoachAudioEnabledChanged})
            connect(app, signal, this, &NativeSteamServiceSource::scheduleSettingsRefresh);

        auto* graph = m_settings->graph();
        for (auto signal : {&SettingsGraph::showSteamPressureChanged,
                            &SettingsGraph::showSteamFlowChanged,
                            &SettingsGraph::showSteamTemperatureChanged})
            connect(graph, signal, this, &NativeSteamServiceSource::scheduleSettingsRefresh);
        connect(m_settings->hardware(), &SettingsHardware::steamTwoTapStopChanged,
                this, &NativeSteamServiceSource::scheduleSettingsRefresh);
    }
    if (m_machineState) {
        connect(m_machineState, &MachineState::phaseChanged,
                this, &NativeSteamServiceSource::scheduleRefresh);
        connect(m_machineState, &MachineState::controlContextIdChanged,
                this, &NativeSteamServiceSource::scheduleRefresh);
        connect(m_machineState, &MachineState::shotTimeChanged,
                this, &NativeSteamServiceSource::scheduleRefresh);
        connect(m_machineState, &MachineState::scaleWeightChanged,
                this, &NativeSteamServiceSource::scheduleRefresh);
    }
    if (m_device) {
        connect(m_device, &DE1Device::connectedChanged,
                this, &NativeSteamServiceSource::scheduleRefresh);
        connect(m_device, &DE1Device::stateChanged,
                this, &NativeSteamServiceSource::scheduleRefresh);
        connect(m_device, &DE1Device::subStateChanged,
                this, &NativeSteamServiceSource::scheduleRefresh);
        connect(m_device, &DE1Device::isHeadlessChanged,
                this, &NativeSteamServiceSource::scheduleRefresh);
        connect(m_device, &DE1Device::shotSampleReceived,
                this, &NativeSteamServiceSource::scheduleRefresh);
    }
    if (m_scale) {
        connect(m_scale, &ScaleDeviceProxy::connectedChanged,
                this, &NativeSteamServiceSource::scheduleRefresh);
        connect(m_scale, &ScaleDeviceProxy::weightChanged,
                this, &NativeSteamServiceSource::scheduleRefresh);
        connect(m_scale, &ScaleDeviceProxy::targetChanged,
                this, &NativeSteamServiceSource::scheduleRefresh);
    }
    if (m_preparation) {
        connect(m_preparation, &PreparationState::changed,
                this, &NativeSteamServiceSource::scheduleRefresh);
    }
    if (m_controller) {
        connect(m_controller, &MainController::steamSoftStoppedChanged,
                this, &NativeSteamServiceSource::scheduleRefresh);
        connect(m_controller, &MainController::steamAutoFlushCountdownChanged,
                this, &NativeSteamServiceSource::scheduleRefresh);
        connect(m_controller, &MainController::steamHeaterStateChanged,
                this, &NativeSteamServiceSource::scheduleRefresh);
        if (auto* coach = m_controller->liveSteamCoach()) {
            connect(coach, &LiveSteamCoach::cueChanged,
                    this, &NativeSteamServiceSource::scheduleRefresh);
            connect(coach, &LiveSteamCoach::durationMilkDerivedChanged,
                    this, &NativeSteamServiceSource::scheduleRefresh);
        }
    }
    if (m_editor)
        connect(m_editor, &SteamPageEditor::editorRevisionChanged,
                this, &NativeSteamServiceSource::scheduleSettingsRefresh);
    if (m_health) {
        connect(m_health, &SteamHealthTracker::warningSnapshotChanged,
                this, &NativeSteamServiceSource::scheduleWarningRefresh);
    }
    if (m_graph) {
        connect(m_graph, &SteamDataModel::flushed,
                this, &NativeSteamServiceSource::scheduleGraphRefresh);
        connect(m_graph, &SteamDataModel::cleared, this, [this] {
            m_graphPublisher.reset();
            scheduleGraphRefresh();
        });
    }
    refreshSnapshot();
}

void NativeSteamServiceSource::setIdentity(const QString& packageId, const QString& hostId)
{
    setPackageId(packageId);
    setHostId(hostId);
}

void NativeSteamServiceSource::scheduleRefresh()
{
    if (m_refreshQueued)
        return;
    m_refreshQueued = true;
    QMetaObject::invokeMethod(this, [this] {
        m_refreshQueued = false;
        refreshSnapshot();
    }, Qt::QueuedConnection);
}

void NativeSteamServiceSource::scheduleSettingsRefresh()
{
    m_settingsDirty = true;
    scheduleRefresh();
}

void NativeSteamServiceSource::scheduleWarningRefresh()
{
    m_warningsDirty = true;
    scheduleRefresh();
}

void NativeSteamServiceSource::scheduleGraphRefresh()
{
    m_graphSnapshotDirty = true;
    scheduleRefresh();
}

QVariantMap NativeSteamServiceSource::currentGraphSnapshot()
{
    return m_graph ? SteamGraphUpdate::capture(*m_graph) : QVariantMap{};
}

void NativeSteamServiceSource::refreshSnapshot()
{
    if (!m_settings || !m_machineState || !m_device || !m_scale || !m_controller
        || !m_preparation || !m_health || !m_editor)
        return;

    if (std::exchange(m_settingsDirty, false)) {
        const auto* brew = m_settings->brew();
        const auto* app = m_settings->app();
        const auto* graph = m_settings->graph();
        const QVariantList presets = brew->steamPitcherPresets();
        const int selected = brew->selectedSteamPitcher();
        setSteamPitcherPresets(presets);
        setSelectedSteamPitcher(selected);
        setSelectedSteamPitcherDisplayIndex(SteamPitcherSelection::displayIndex(presets, selected));
        setSteamTemperature(brew->steamTemperature());
        setSteamTimeout(brew->steamTimeout());
        setSteamFlow(brew->steamFlow());
        setSteamAutoFlushSeconds(brew->steamAutoFlushSeconds());
        setSteamSecondsPerGram(brew->steamSecondsPerGram());
        setMilkAutoCaptureEnabled(brew->milkAutoCaptureEnabled());
        setLastSteamMilkG(brew->lastSteamMilkG());
        setLastSteamTimeS(brew->lastSteamTimeS());
        setLastSteamEffectiveDurationSec(brew->effectiveSteamDurationSec(selected, lastSteamMilkG()));
        setSteamTwoTapStop(m_settings->hardware()->steamTwoTapStop());
        setSteamViewMode(app->steamViewMode());
        setSteamCoachVisualEnabled(app->steamCoachVisualEnabled());
        setSteamCoachAudioEnabled(app->steamCoachAudioEnabled());
        setShowSteamPressure(graph->showSteamPressure());
        setShowSteamFlow(graph->showSteamFlow());
        setShowSteamTemperature(graph->showSteamTemperature());
        setEditorRevision(m_editor->editorRevision());
    }
    if (std::exchange(m_warningsDirty, false)) {
        setActiveWarningKinds(m_health->activeWarningKinds());
        setActiveWarningSessionId(m_health->activeWarningSessionId());
        setActiveSteamSession(m_health->activeSession());
        setWarningSnapshot(m_health->warningSnapshot().toJson().toVariantMap());
    }
    const auto preset = SteamPitcherSelection::presetAt(steamPitcherPresets(), selectedSteamPitcher());
    const double measuredMilk = m_scale->connected() && !m_scale->isFlowScale()
        ? SteamCalculations::netMilkForPitcher(preset,
            m_preparation->loadOnScaleG(m_machineState->scaleWeight())) : 0.0;
    setDeviceConnected(m_device->isConnected());
    setDeviceState(m_device->stateInt());
    setDeviceSubState(m_device->subStateInt());
    setIsHeadless(m_device->isHeadless());
    setPhase(static_cast<int>(m_machineState->phase()));
    setIsFlowing(m_machineState->isFlowing());
    setIsHeating(m_machineState->isHeating());
    setIsReady(m_machineState->isReady());
    setIsSteaming(m_machineState->phase() == MachineState::Phase::Steaming);
    setControlContextId(m_machineState->controlContextId());
    setShotTime(m_machineState->shotTime());
    setScaleConnected(m_scale->connected());
    setScaleIsFlowScale(m_scale->isFlowScale());
    setScaleWeight(m_machineState->scaleWeight());
    setSteamTemperatureActual(m_device->steamTemperature());
    setPlannedSteamDurationSec(m_preparation->plannedSteamDurationSec(
        preset, milkAutoCaptureEnabled(), steamSecondsPerGram(), steamTimeout()));
    setSteamHeaterOn(m_controller->steamHeaterOn());
    setSteamSoftStopped(m_controller->steamSoftStopped());
    setSteamAutoFlushCountdown(m_controller->steamAutoFlushCountdown());
    setSteamTimeoutScaled(m_preparation->steamTimeoutScaled());
    setMilkScaleLoadG(m_preparation->milkScaleLoadG());
    setMeasuredMilkG(measuredMilk);
    setScaledSteamTimeout(SteamCalculations::scaledSteamTime(
        preset, milkAutoCaptureEnabled(), steamSecondsPerGram(), measuredMilk));
    const auto* coach = m_controller->liveSteamCoach();
    setCueText(coach ? coach->cueText() : QString());
    setCueSeverity(coach ? coach->cueSeverity() : QString());
    setCueActive(coach && coach->cueActive());
    setDurationMilkDerived(coach && coach->durationMilkDerived());
    const bool publishGraph = std::exchange(m_graphSnapshotDirty, false);
    if (publishGraph && m_graph) {
        const auto update = m_graphPublisher.next(*m_graph);
        if (!update.isEmpty()) setGraphUpdate(update);
    }
}

QVariantMap NativeSteamServiceSource::failure(const QString& message) const
{
    return {{QStringLiteral("success"), false},
            {QStringLiteral("message"), message},
            {QStringLiteral("result"), QVariantMap{}},
            {QStringLiteral("newRevision"), m_editor ? m_editor->editorRevision() : QString()}};
}

bool NativeSteamServiceSource::contextMatches(const QString& expectedRevision) const
{
    return m_machineState && !expectedRevision.isEmpty()
        && expectedRevision == m_machineState->controlContextId();
}

static bool pitcherContextMatches(const QString& expectedRevision,
                                  const MachineState* machineState,
                                  const SteamPageEditor* editor)
{
    return machineState && editor && !expectedRevision.isEmpty()
        && expectedRevision == machineState->controlContextId() + QLatin1Char(':')
            + editor->editorRevision();
}

QVariantMap NativeSteamServiceSource::editorOperation(const QVariantMap& fields,
                                                      const QString& expectedRevision)
{
    if (!m_editor || !ServiceValue::hasExactKeys(fields, {QStringLiteral("editorOperation"), QStringLiteral("fields")})
        || fields.value(QStringLiteral("editorOperation")).metaType().id() != QMetaType::QString
        || fields.value(QStringLiteral("fields")).metaType().id() != QMetaType::QVariantMap)
        return failure(QStringLiteral("Steam editor request has invalid fields."));

    const QString contextPrefix = m_machineState->controlContextId() + QLatin1Char(':');
    if (!expectedRevision.startsWith(contextPrefix))
        return failure(QStringLiteral("The machine state changed. Review it before editing steam."));
    const QString editorRevision = expectedRevision.mid(contextPrefix.size());
    const QString operation = fields.value(QStringLiteral("editorOperation")).toString();
    const QJsonObject jsonFields = QJsonObject::fromVariantMap(
        fields.value(QStringLiteral("fields")).toMap());
    const SteamEditRequest::Result validated = SteamEditRequest::validateFields(operation, jsonFields);
    if (!validated.valid)
        return failure(validated.error);

    const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QVariantMap completion;
    bool received = false;
    const auto connection = connect(m_editor, &SteamPageEditor::operationCompleted, this,
        [&completion, &received, &requestId](const QString& id, const QString& name,
                                             bool success, const QString& message,
                                             const QVariantMap& result, const QString& revision) {
            if (id != requestId)
                return;
            completion = {{QStringLiteral("success"), success},
                          {QStringLiteral("message"), message},
                          {QStringLiteral("result"), result},
                          {QStringLiteral("newRevision"), revision},
                          {QStringLiteral("operation"), name}};
            received = true;
        });
    m_editor->dispatch(validated.command, editorRevision, requestId);
    disconnect(connection);
    if (!received)
        return failure(QStringLiteral("Steam editor did not complete the operation."));
    return completion;
}

QVariantMap NativeSteamServiceSource::execute(QString operation,
                                             QVariantMap fields,
                                             QString expectedRevision)
{
    if (!m_settings || !m_controller || !m_machineState || !m_preparation)
        return failure(QStringLiteral("Steam controls are unavailable."));

    QVariantMap result;
    bool success = false;
    QString message;
    QString pageFailure;
    const auto finish = [this, &operation](bool ok, const QString& error = {},
                                           const QVariantMap& details = QVariantMap{}) {
        return QVariantMap{{QStringLiteral("success"), ok},
                           {QStringLiteral("message"), error},
                           {QStringLiteral("result"), details},
                           {QStringLiteral("newRevision"), m_editor ? m_editor->editorRevision() : QString()},
                           {QStringLiteral("operation"), operation}};
    };

    if (operation == QStringLiteral("steam-editor"))
        return editorOperation(fields, expectedRevision);

    const bool machineAction = operation == QStringLiteral("steam-prepare")
        || operation == QStringLiteral("steam-select-pitcher")
        || operation == QStringLiteral("steam-adjust-time")
        || operation == QStringLiteral("steam-stop")
        || operation == QStringLiteral("steam-back")
        || operation == QStringLiteral("tare");
    const bool pitcherAction = operation == QStringLiteral("steam-prepare")
        || operation == QStringLiteral("steam-select-pitcher");
    const bool contextValid = pitcherAction
        ? pitcherContextMatches(expectedRevision, m_machineState, m_editor)
        : contextMatches(expectedRevision);
    if (machineAction && !contextValid)
        return failure(QStringLiteral("The machine state changed. Review it before trying again."));

    QMetaObject::Connection pageFailureConnection;
    QMetaObject::Connection pageSuccessConnection;
    if (machineAction) {
        pageSuccessConnection = connect(m_controller, &MainController::steamPageActionSucceeded,
            this, [&result, &operation](const QString& action, bool dismissPage) {
                if (action == operation)
                    result.insert(QStringLiteral("dismissPage"), dismissPage);
            });
        pageFailureConnection = connect(m_controller, &MainController::steamPageActionFailed,
            this, [&pageFailure, &operation](const QString& action, const QString& reason) {
                if (action == operation)
                    pageFailure = reason;
            });
    }

    if (operation == QStringLiteral("steam-prepare") && fields.isEmpty()) {
        success = m_controller->prepareSteamPage();
    } else if (operation == QStringLiteral("steam-select-pitcher")
               && ServiceValue::hasExactKeys(fields, {QStringLiteral("index")})) {
        int index = -1;
        success = readInteger(fields, QStringLiteral("index"), &index)
            && m_controller->selectSteamPagePitcher(index);
    } else if (operation == QStringLiteral("steam-adjust-time")
               && ServiceValue::hasExactKeys(fields, {QStringLiteral("delta")})) {
        int delta = 0;
        success = readInteger(fields, QStringLiteral("delta"), &delta)
            && m_controller->adjustSteamPageTimeout(delta);
    } else if (operation == QStringLiteral("steam-stop")
               && ServiceValue::hasExactKeys(fields, {QStringLiteral("expectedSoftStopped")})) {
        bool expectedSoftStopped = false;
        if (readBool(fields, QStringLiteral("expectedSoftStopped"), &expectedSoftStopped)) {
            success = m_controller->stopSteamPage(expectedSoftStopped);
        }
    } else if (operation == QStringLiteral("steam-back") && fields.isEmpty()) {
        success = m_controller->leaveSteamPage();
    } else if (operation == QStringLiteral("tare") && fields.isEmpty()) {
        if (m_scale && m_scale->connected()) {
            m_machineState->tareScale();
            success = true;
        } else {
            message = QStringLiteral("A connected scale is required to tare.");
        }
    } else if (operation == QStringLiteral("set-steam-view-mode")
               && ServiceValue::hasExactKeys(fields, {QStringLiteral("value"), QStringLiteral("expectedValue")})) {
        const QVariant value = fields.value(QStringLiteral("value"));
        const QVariant expected = fields.value(QStringLiteral("expectedValue"));
        auto* app = m_settings->app();
        if (value.metaType().id() == QMetaType::QString
            && expected.metaType().id() == QMetaType::QString
            && expected.toString() == app->steamViewMode()
            && (value.toString() == QStringLiteral("timer")
                || value.toString() == QStringLiteral("chart"))) {
            app->setSteamViewMode(value.toString());
            success = true;
        } else {
            message = QStringLiteral("Steam view settings changed or are invalid.");
        }
    } else if ((operation == QStringLiteral("set-steam-coach-visual")
                || operation == QStringLiteral("set-steam-coach-audio"))
               && ServiceValue::hasExactKeys(fields, {QStringLiteral("value"), QStringLiteral("expectedValue")})) {
        bool value = false;
        bool expected = false;
        auto* app = m_settings->app();
        const bool visual = operation == QStringLiteral("set-steam-coach-visual");
        const bool current = visual ? app->steamCoachVisualEnabled() : app->steamCoachAudioEnabled();
        if (readBool(fields, QStringLiteral("value"), &value)
            && readBool(fields, QStringLiteral("expectedValue"), &expected)
            && current == expected) {
            if (visual)
                app->setSteamCoachVisualEnabled(value);
            else
                app->setSteamCoachAudioEnabled(value);
            success = true;
        } else {
            message = QStringLiteral("Steam coaching settings changed or are invalid.");
        }
    } else if (operation == QStringLiteral("set-steam-graph-trace")
               && ServiceValue::hasExactKeys(fields, {QStringLiteral("trace"), QStringLiteral("enabled"),
                                    QStringLiteral("expectedEnabled")})) {
        const QVariant traceValue = fields.value(QStringLiteral("trace"));
        bool enabled = false;
        bool expected = false;
        auto* graph = m_settings->graph();
        if (traceValue.metaType().id() == QMetaType::QString
            && readBool(fields, QStringLiteral("enabled"), &enabled)
            && readBool(fields, QStringLiteral("expectedEnabled"), &expected)) {
            const QString trace = traceValue.toString();
            if (trace == QStringLiteral("pressure") && graph->showSteamPressure() == expected) {
                graph->setShowSteamPressure(enabled);
                success = true;
            } else if (trace == QStringLiteral("flow") && graph->showSteamFlow() == expected) {
                graph->setShowSteamFlow(enabled);
                success = true;
            } else if (trace == QStringLiteral("temperature")
                       && graph->showSteamTemperature() == expected) {
                graph->setShowSteamTemperature(enabled);
                success = true;
            } else {
                message = QStringLiteral("Steam graph settings changed or are invalid.");
            }
        }
    }

    if (pageFailureConnection)
        disconnect(pageFailureConnection);
    if (pageSuccessConnection)
        disconnect(pageSuccessConnection);
    if (!success && !pageFailure.isEmpty())
        message = pageFailure;

    if (!success && message.isEmpty())
        message = QStringLiteral("Unknown Steam operation or invalid fields.");
    if (success)
        scheduleRefresh();
    return finish(success, message, result);
}
