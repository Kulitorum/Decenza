#include "machinecontrolservicesource.h"

#include "ble/de1device.h"
#include "ble/blemanager.h"
#include "ble/scaledeviceproxy.h"
#include "controllers/maincontroller.h"
#include "controllers/profilemanager.h"
#include "core/settings.h"
#include "core/settings_app.h"
#include "core/settings_brew.h"
#include "core/settings_hardware.h"
#include "core/batterymanager.h"
#include "core/machinecontrolpresets.h"
#include "core/machinecontrolstartpolicy.h"
#include "machinecontrolespressopolicy.h"
#include "machine/machinestate.h"
#include "models/shotdatamodel.h"
#include "screensaver/screensavervideomanager.h"

#include <QMetaType>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <cmath>
#include <limits>

namespace {
constexpr qint64 MaxSafeEventSequence = 9007199254740991LL;

bool strictFiniteNumber(const QVariant& value, double* result)
{
    if (!result || value.metaType().id() == QMetaType::Bool
        || value.metaType().id() == QMetaType::QString)
        return false;
    bool ok = false;
    const double number = value.toDouble(&ok);
    if (!ok || !std::isfinite(number))
        return false;
    *result = number;
    return true;
}

QVariantMap brewDialogSnapshot(const MainController* controller)
{
    if (!controller)
        return {};

    const QVariantMap activeRecipe = controller->activeRecipe();
    return {
        {QStringLiteral("activeRecipe"), QVariantMap{
            {QStringLiteral("name"), activeRecipe.value(QStringLiteral("name"))},
            {QStringLiteral("yieldValue"), activeRecipe.value(QStringLiteral("yieldValue"))},
            {QStringLiteral("yieldMode"), activeRecipe.value(QStringLiteral("yieldMode"))}
        }},
        {QStringLiteral("activeBaselineTemperatureC"),
         controller->activeBaselineTemperatureC()},
        {QStringLiteral("activeBaselineYieldValue"), controller->activeBaselineYieldValue()},
        {QStringLiteral("activeBaselineYieldMode"), controller->activeBaselineYieldMode()},
        {QStringLiteral("activeBaselineYieldSource"), controller->activeBaselineYieldSource()},
        {QStringLiteral("yieldPersistTarget"), controller->yieldPersistTarget()}
    };
}

QVariantMap brewSettingsSnapshotMap(const BrewSettingsSnapshot& snapshot,
                                    const MainController* controller)
{
    const QVariantMap dialog = brewDialogSnapshot(controller);
    QJsonObject revisionView{
        {QStringLiteral("profileManagerRevision"), snapshot.revision},
        {QStringLiteral("brewDialog"), QJsonObject::fromVariantMap(dialog)}
    };
    const QString viewRevision = QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(revisionView).toJson(QJsonDocument::Compact),
        QCryptographicHash::Sha256).toHex());

    QVariantMap result{
        {QStringLiteral("revision"), viewRevision},
        {QStringLiteral("activeBagId"), snapshot.activeBagId},
        {QStringLiteral("activeRecipeId"), snapshot.activeRecipeId},
        {QStringLiteral("activeEquipmentId"), snapshot.activeEquipmentId},
        {QStringLiteral("activeBagYieldMode"), snapshot.activeBagYieldMode},
        {QStringLiteral("activeBagYieldValue"), snapshot.activeBagYieldValue},
        {QStringLiteral("activeRecipeDoseG"), snapshot.activeRecipeDoseG},
        {QStringLiteral("doseLadderResolved"), snapshot.doseLadderResolved},
        {QStringLiteral("profileTitle"), snapshot.profileTitle},
        {QStringLiteral("baseProfileName"), snapshot.baseProfileName},
        {QStringLiteral("profileLoadGeneration"), QString::number(snapshot.profileLoadGeneration)},
        {QStringLiteral("profileContentSha256"), snapshot.profileContentSha256},
        {QStringLiteral("doseG"), snapshot.doseG},
        {QStringLiteral("grindSetting"), snapshot.grindSetting},
        {QStringLiteral("grindRpm"), snapshot.grindRpm},
        {QStringLiteral("yieldMode"), snapshot.yieldMode},
        {QStringLiteral("yieldValue"), snapshot.yieldValue},
        {QStringLiteral("lastUsedRatio"), snapshot.lastUsedRatio},
        {QStringLiteral("hasTemperatureOverride"), snapshot.hasTemperatureOverride},
        {QStringLiteral("temperatureC"), snapshot.temperatureC},
        {QStringLiteral("profileTargetWeightG"), snapshot.profileTargetWeightG},
        {QStringLiteral("profileTemperatureC"), snapshot.profileTemperatureC}
    };
    result.insert(QStringLiteral("brewDialog"), dialog);
    return result;
}

bool validBrewRevision(const QVariant& value, QString* revision)
{
    if (!revision || value.metaType().id() != QMetaType::QString)
        return false;
    const QString text = value.toString();
    if (text.size() != 64)
        return false;
    for (const QChar ch : text) {
        const bool hex = (ch >= QLatin1Char('0') && ch <= QLatin1Char('9'))
            || (ch >= QLatin1Char('a') && ch <= QLatin1Char('f'));
        if (!hex)
            return false;
    }
    *revision = text;
    return true;
}
}

NativeMachineControlServiceSource::NativeMachineControlServiceSource(
    MainController* controller, Settings* settings, MachineState* machineState,
    DE1Device* device, BLEManager* bleManager, BatteryManager* batteryManager,
    ScaleDeviceProxy* scaleDevice, QObject* parent)
    : MachineControlStateSimpleSource(parent)
    , m_controller(controller)
    , m_settings(settings)
    , m_machineState(machineState)
    , m_device(device)
    , m_bleManager(bleManager)
    , m_batteryManager(batteryManager)
    , m_scaleDevice(scaleDevice)
{
    setSelectedRecipeId(m_controller ? m_controller->selectedRecipeId() : -1);
    setEspressoOperationId(m_controller ? m_controller->espressoOperationId() : 0);
    setEventSourceId(QUuid::createUuid().toString(QUuid::WithoutBraces));
    setEventSequence(0);
    setPhase(m_machineState ? static_cast<int>(m_machineState->phase())
                            : static_cast<int>(MachineState::Phase::Disconnected));
    setCurrentFrameIndex(-1);
    setCurrentFrameName(m_controller ? m_controller->currentFrameName() : QString());
    if (m_controller && m_controller->shotDataModel()) {
        const auto& markers = m_controller->shotDataModel()->phaseMarkersList();
        if (!markers.isEmpty() && markers.constLast().frameNumber >= 0) {
            setCurrentFrameIndex(markers.constLast().frameNumber);
            setCurrentFrameName(markers.constLast().label);
        }
    }
    if (m_machineState) {
        connect(m_machineState, &MachineState::phaseChanged, this, [this] {
            if (!m_machineState)
                return;
            const int currentPhase = static_cast<int>(m_machineState->phase());
            setPhase(currentPhase);
            publishPresentationEvent(QStringLiteral("phaseChanged"),
                                     {{QStringLiteral("phase"), currentPhase}});
        });
        connect(m_machineState, &MachineState::targetWeightReached, this, [this] {
            if (m_controller)
                m_controller->reportShotStopReason(QStringLiteral("weight"));
            publishPresentationEvent(QStringLiteral("targetWeightReached"));
        });
    }
    if (m_controller) {
        connect(m_controller, &MainController::shotFlowStartedForOperation, this,
                [this](qint64 operationId, int phase) {
            publishPresentationEvent(QStringLiteral("shotStarted"),
                                     {{QStringLiteral("phase"), phase},
                                      {QStringLiteral("operationId"), operationId}});
        });
        connect(m_controller, &MainController::shotFlowEndedForOperation, this,
                [this](qint64 operationId, const QString& reason) {
            publishPresentationEvent(QStringLiteral("shotEnded"),
                                     {{QStringLiteral("operationId"), operationId},
                                      {QStringLiteral("reason"), reason}});
        });
        connect(m_controller, &MainController::selectedRecipeIdChanged, this, [this] {
            setSelectedRecipeId(m_controller ? m_controller->selectedRecipeId() : -1);
        });
        connect(m_controller, &MainController::espressoOperationIdChanged, this, [this] {
            setEspressoOperationId(m_controller ? m_controller->espressoOperationId() : 0);
        });
        connect(m_controller, &MainController::recipeShotStartRequestCompleted,
                this, &NativeMachineControlServiceSource::onRecipeStartCompleted);
        connect(m_controller, &MainController::simulatedShotRequestCompleted,
                this, &NativeMachineControlServiceSource::onSimulatedShotRequestCompleted);
        connect(m_controller, &MainController::frameChanged, this,
                [this](int frameIndex, const QString& frameName,
                       const QString& transitionReason) {
            setCurrentFrameIndex(frameIndex);
            setCurrentFrameName(frameName);
            publishPresentationEvent(QStringLiteral("frameChanged"),
                                     {{QStringLiteral("frameIndex"), frameIndex},
                                      {QStringLiteral("frameName"), frameName},
                                      {QStringLiteral("transitionReason"), transitionReason}});
        });
        connect(m_controller, &MainController::shotEndedShowMetadataForOperation, this,
                [this](qint64 shotId, qint64 operationId) {
            // Preserve zero as the explicit save-failure sentinel. The shared
            // completion coordinator must never fall back to a previous shot.
            publishPresentationEvent(QStringLiteral("shotReviewReady"),
                                     {{QStringLiteral("shotId"), shotId},
                                      {QStringLiteral("operationId"), operationId}});
        });
    }
}

void NativeMachineControlServiceSource::publishPresentationEvent(
    const QString& event, const QVariantMap& payload)
{
    if (eventSequence() >= MaxSafeEventSequence) {
        setEventSourceId(QUuid::createUuid().toString(QUuid::WithoutBraces));
        setEventSequence(0);
    }
    const qint64 nextSequence = eventSequence() + 1;
    setEventSequence(nextSequence);
    emit presentationEvent(eventSourceId(), nextSequence, event, payload);
}

void NativeMachineControlServiceSource::setScreensaverManager(ScreensaverVideoManager* manager)
{
    m_screensaverManager = manager;
}

bool NativeMachineControlServiceSource::onlyField(const QVariantMap& fields,
                                                   const QString& key)
{
    return fields.size() == 1 && fields.contains(key);
}

bool NativeMachineControlServiceSource::readIndex(const QVariantMap& fields,
                                                   const QString& key, int* value)
{
    if (!value || !onlyField(fields, key))
        return false;
    const QVariant input = fields.value(key);
    if (input.metaType().id() == QMetaType::Bool || input.metaType().id() == QMetaType::QString)
        return false;
    bool ok = false;
    const double number = input.toDouble(&ok);
    if (!ok || !std::isfinite(number) || std::trunc(number) != number
        || number < 0 || number > std::numeric_limits<int>::max())
        return false;
    *value = static_cast<int>(number);
    return true;
}

bool NativeMachineControlServiceSource::readPresetIndex(const QVariantMap& fields,
                                                         const QString& key, int* value,
                                                         QVariantMap* expectedPreset)
{
    if (!value || !expectedPreset || fields.size() != 2 || !fields.contains(key)
        || !fields.contains(QStringLiteral("expectedPreset")))
        return false;
    const QVariant snapshot = fields.value(QStringLiteral("expectedPreset"));
    if (snapshot.metaType().id() != QMetaType::QVariantMap)
        return false;
    *expectedPreset = snapshot.toMap();
    if (expectedPreset->isEmpty())
        return false;

    const QVariant input = fields.value(key);
    if (input.metaType().id() == QMetaType::Bool || input.metaType().id() == QMetaType::QString)
        return false;
    bool ok = false;
    const double number = input.toDouble(&ok);
    if (!ok || !std::isfinite(number) || std::trunc(number) != number
        || number < 0 || number > std::numeric_limits<int>::max())
        return false;
    *value = static_cast<int>(number);
    return true;
}

bool NativeMachineControlServiceSource::readPositiveId(const QVariantMap& fields,
                                                       const QString& key, qint64* value)
{
    return value && onlyField(fields, key)
        && MachineControlPresets::readPositiveId(fields.value(key), value);
}

void NativeMachineControlServiceSource::execute(QString requestId, QString operation,
                                                QVariantMap fields,
                                                QString expectedContextId)
{
    const auto start = m_requestIds.begin(requestId);
    if (start == ServiceRequestIds::Start::DuplicateId)
        return;
    if (start != ServiceRequestIds::Start::Accepted) {
        emit operationCompleted(requestId, operation, false,
            start == ServiceRequestIds::Start::LimitReached
                ? QStringLiteral("Too many machine control requests are pending.")
                : QStringLiteral("Invalid machine control request ID."), {});
        return;
    }
    if (!m_simulatedShotRequests.isEmpty()
        && operation != QLatin1String("generate-fake-shot")) {
        finish(requestId, operation, false,
               QStringLiteral("Wait for the simulated shot save to finish before using machine controls."));
        return;
    }
    if (!m_controller || !m_settings || !m_settings->brew()) {
        finish(requestId, operation, false, QStringLiteral("Machine controls are unavailable."));
        return;
    }
    const auto currentContext = [this, &expectedContextId] {
        return m_machineState && MachineControlStartPolicy::contextMatches(
            expectedContextId, m_machineState->controlContextId());
    };

    if (operation == QLatin1String("stop-espresso")) {
        const bool allowed = m_machineState && m_device
            && MachineControlEspressoPolicy::stopAllowed(
                expectedContextId, m_machineState->controlContextId(),
                static_cast<MachinePhase>(m_machineState->phase()), m_device->isConnected());
        if (!fields.isEmpty() || !allowed) {
            finish(requestId, operation, false,
                   QStringLiteral("The espresso operation changed before it could be stopped."));
            return;
        }
        m_controller->reportShotStopReason(QStringLiteral("manual"));
        m_device->stopOperation();
        finish(requestId, operation, true, {}, {{QStringLiteral("stopped"), true}});
        return;
    }

    if (operation == QLatin1String("bump-target-weight")) {
        double deltaG = 0.0;
        const bool validPayload = fields.size() == 1
            && fields.contains(QStringLiteral("deltaG"))
            && strictFiniteNumber(fields.value(QStringLiteral("deltaG")), &deltaG);
        const bool allowed = validPayload && m_machineState && m_device
            && MachineControlEspressoPolicy::targetAdjustmentAllowed(
                expectedContextId, m_machineState->controlContextId(),
                static_cast<MachinePhase>(m_machineState->phase()), m_device->isConnected(),
                m_machineState->targetWeight(), m_machineState->canDecreaseTargetWeight(),
                deltaG);
        if (!allowed) {
            finish(requestId, operation, false,
                   QStringLiteral("The espresso target can no longer be adjusted."));
            return;
        }
        m_controller->bumpTargetWeight(deltaG);
        finish(requestId, operation, true, {},
               {{QStringLiteral("targetWeightG"), m_machineState->targetWeight()}});
        return;
    }

    if (operation == QLatin1String("skip-frame")) {
        const bool allowed = m_machineState && m_device
            && MachineControlEspressoPolicy::skipAllowed(
                expectedContextId, m_machineState->controlContextId(),
                static_cast<MachinePhase>(m_machineState->phase()), m_device->isConnected());
        if (!fields.isEmpty() || !allowed) {
            finish(requestId, operation, false,
                   QStringLiteral("The espresso frame can no longer be skipped."));
            return;
        }
        m_device->skipToNextFrame();
        finish(requestId, operation, true, {}, {{QStringLiteral("dispatched"), true}});
        return;
    }

    if (operation == QLatin1String("generate-fake-shot")) {
        if (!fields.isEmpty() || !currentContext() || !m_device
            || !m_device->simulationMode() || !m_machineState
            || m_machineState->isOperating() || m_machineState->isFlowing()
            || m_requestIds.pendingCount() != 1 || !m_simulatedShotRequests.isEmpty()) {
            finish(requestId, operation, false,
                   QStringLiteral("A simulated shot is available only while the simulator is idle."));
            return;
        }
        m_simulatedShotRequests.insert(requestId);
        if (!m_controller->requestSimulatedShotData(requestId)) {
            m_simulatedShotRequests.remove(requestId);
            finish(requestId, operation, false,
                   QStringLiteral("The simulator could not save a test shot."));
        }
        return;
    }

    if (operation == QLatin1String("sleep-machine")) {
        if (!fields.isEmpty() || !currentContext() || !m_device) {
            finish(requestId, operation, false, QStringLiteral("Sleep is unavailable right now."));
            return;
        }
        if (m_scaleDevice && m_scaleDevice->connected())
            m_scaleDevice->disableLcd();
        const bool sleepSent = m_device->goToSleep();
        if (m_screensaverManager) m_screensaverManager->setScreensaverActive(true);
        finish(requestId, operation, true, {}, {{QStringLiteral("sleepSent"), sleepSent}});
        return;
    }

    if (operation == QLatin1String("wake-machine")) {
        if (!fields.isEmpty() || !currentContext() || !m_device) {
            finish(requestId, operation, false, QStringLiteral("Wake is unavailable right now."));
            return;
        }

        // Keep the screensaver wake path on the host: DE1Device owns deferred
        // wake semantics for disconnected/connecting devices, while BLEManager
        // suppresses physical reconnect attempts for simulated devices. A
        // headless DE1 is still a real machine and must receive the same wake.
        const bool simulated = m_device->simulationMode();
        const bool headless = m_device->isHeadless();
        if (m_screensaverManager) m_screensaverManager->setScreensaverActive(false);
        m_device->wakeUp();
        if (!simulated && !m_device->isConnected() && !m_device->isConnecting()
            && m_bleManager) {
            m_bleManager->tryDirectConnectToDE1();
        }

        if (m_scaleDevice && m_scaleDevice->connected()) {
            m_scaleDevice->wake();
        } else if (m_bleManager) {
            // BLEManager also guards simulated scales and saved simulator IDs.
            m_bleManager->tryDirectConnectToScale();
        }

        finish(requestId, operation, true, {},
               {{QStringLiteral("wakeRequested"), true},
                {QStringLiteral("simulated"), simulated},
                {QStringLiteral("headless"), headless}});
        return;
    }

    if (operation == QLatin1String("request-idle")) {
        if (!fields.isEmpty() || !currentContext() || !m_device) {
            finish(requestId, operation, false, QStringLiteral("Idle is unavailable right now."));
            return;
        }
        m_device->requestIdle();
        finish(requestId, operation, true);
        return;
    }

    if (operation == QLatin1String("disconnect-machine")) {
        if (!fields.isEmpty() || !currentContext() || !m_device) {
            finish(requestId, operation, false, QStringLiteral("Disconnect is unavailable right now."));
            return;
        }
        m_device->disconnect();
        finish(requestId, operation, true);
        return;
    }

    if (operation == QLatin1String("scan-for-devices")) {
        if (!fields.isEmpty() || !expectedContextId.isEmpty() || !m_bleManager) {
            finish(requestId, operation, false, QStringLiteral("Device scanning is unavailable right now."));
            return;
        }
        m_bleManager->scanForDevices();
        finish(requestId, operation, true);
        return;
    }

    if (operation == QLatin1String("toggle-steam-heater")) {
        if (!fields.isEmpty() || !currentContext()) {
            finish(requestId, operation, false, QStringLiteral("Steam heater control is unavailable."));
            return;
        }
        m_controller->toggleSteamHeater(QStringLiteral("custom-widget-toggle"));
        finish(requestId, operation, true);
        return;
    }

    if (operation == QLatin1String("toggle-charging")) {
        if (!fields.isEmpty() || !currentContext() || !m_batteryManager) {
            finish(requestId, operation, false, QStringLiteral("Charging control is unavailable right now."));
            return;
        }
        if (!m_batteryManager->usbChargerEnabled())
            m_batteryManager->setUsbChargerEnabled(true);
        else
            m_batteryManager->setChargingMode((m_batteryManager->chargingMode() + 1) % 3);
        finish(requestId, operation, true);
        return;
    }

    if (operation == QLatin1String("load-previous-profile")) {
        if (!fields.isEmpty() || !currentContext() || !m_controller->profileManager()) {
            finish(requestId, operation, false, QStringLiteral("Profile navigation is unavailable right now."));
            return;
        }
        const QString previous = m_controller->profileManager()->previousProfileName();
        if (previous.isEmpty()
            || !m_controller->profileManager()->loadProfile(previous)) {
            finish(requestId, operation, false, QStringLiteral("There is no previous profile to load."));
            return;
        }
        finish(requestId, operation, true, {}, {{QStringLiteral("profileFilename"), previous}});
        return;
    }

    if (operation == QLatin1String("apply-temperature-override")) {
        const QVariant expectedOverrideValue = fields.value(QStringLiteral("expectedOverride"));
        const QVariantMap expectedOverride = expectedOverrideValue.toMap();
        const QVariant expectedHasOverride = expectedOverride.value(QStringLiteral("hasOverride"));
        const QVariant expectedValue = expectedOverride.value(QStringLiteral("valueC"));
        const QVariant expectedProfileValue = fields.value(QStringLiteral("expectedProfile"));
        const QVariantMap expectedProfile = expectedProfileValue.toMap();
        const QVariant expectedFilename = expectedProfile.value(QStringLiteral("filename"));
        const QVariant expectedTitle = expectedProfile.value(QStringLiteral("title"));
        const QVariant temperatureValue = fields.value(QStringLiteral("valueC"));
        bool expectedValueOk = false;
        bool temperatureOk = false;
        const double expectedValueC = expectedValue.toDouble(&expectedValueOk);
        const double valueC = temperatureValue.toDouble(&temperatureOk);
        const bool expectedMapValid = expectedOverride.size() == 2
            && expectedOverride.contains(QStringLiteral("hasOverride"))
            && expectedOverride.contains(QStringLiteral("valueC"))
            && expectedHasOverride.metaType().id() == QMetaType::Bool
            && expectedValue.metaType().id() != QMetaType::Bool
            && expectedValue.metaType().id() != QMetaType::QString
            && expectedValueOk && std::isfinite(expectedValueC)
            && (expectedHasOverride.toBool()
                    ? expectedValueC >= 70.0 && expectedValueC <= 100.0
                    : qAbs(expectedValueC) <= 1e-6);
        const bool expectedProfileValid = expectedProfile.size() == 2
            && expectedProfile.contains(QStringLiteral("filename"))
            && expectedProfile.contains(QStringLiteral("title"))
            && expectedFilename.metaType().id() == QMetaType::QString
            && expectedTitle.metaType().id() == QMetaType::QString
            && !expectedTitle.toString().isEmpty();
        const bool payloadValid = fields.size() == 3
            && fields.contains(QStringLiteral("expectedProfile"))
            && expectedProfileValue.metaType().id() == QMetaType::QVariantMap
            && expectedProfileValid
            && expectedOverrideValue.metaType().id() == QMetaType::QVariantMap
            && expectedMapValid
            && fields.contains(QStringLiteral("valueC"))
            && temperatureValue.metaType().id() != QMetaType::Bool
            && temperatureValue.metaType().id() != QMetaType::QString
            && temperatureOk && std::isfinite(valueC)
            && valueC >= 70.0 && valueC <= 100.0;
        if (!payloadValid || !currentContext()
            || !m_settings->brew() || !m_controller->profileManager()) {
            finish(requestId, operation, false,
                   QStringLiteral("Temperature override is unavailable or the machine state changed."));
            return;
        }

        SettingsBrew* brew = m_settings->brew();
        ProfileManager* profiles = m_controller->profileManager();
        const bool actualHasOverride = brew->hasTemperatureOverride();
        const double actualValueC = brew->temperatureOverride();
        if (profiles->baseProfileName() != expectedFilename.toString()
            || profiles->currentProfileTitle() != expectedTitle.toString()
            || actualHasOverride != expectedHasOverride.toBool()
            || (actualHasOverride && qAbs(actualValueC - expectedValueC) > 1e-6)) {
            finish(requestId, operation, false,
                   QStringLiteral("The profile or temperature override changed. Review it and try again."));
            return;
        }

        profiles->applyTemperatureOverride(valueC);
        finish(requestId, operation, true, {},
               {{QStringLiteral("profile"), expectedProfile},
                {QStringLiteral("valueC"), valueC}});
        return;
    }

    if (operation == QLatin1String("brew-settings-snapshot")) {
        if (!fields.isEmpty() || !expectedContextId.isEmpty()
            || !m_controller->profileManager()) {
            finish(requestId, operation, false,
                   QStringLiteral("Brew settings are unavailable right now."));
            return;
        }
        const QVariantMap snapshot = brewSettingsSnapshotMap(
            m_controller->profileManager()->brewSettingsSnapshot(), m_controller);
        finish(requestId, operation, true, {},
               {{QStringLiteral("snapshot"), snapshot},
                {QStringLiteral("revision"), snapshot.value(QStringLiteral("revision"))}});
        return;
    }

    if (operation == QLatin1String("brew-settings-apply")) {
        ProfileManager* profiles = m_controller->profileManager();
        QString expectedRevision;
        if (!profiles || !currentContext()
            || fields.size() != 7
            || !fields.contains(QStringLiteral("expectedRevision"))
            || !validBrewRevision(fields.value(QStringLiteral("expectedRevision")),
                                  &expectedRevision)
            || fields.value(QStringLiteral("grindSetting")).metaType().id() != QMetaType::QString
            || fields.value(QStringLiteral("yieldMode")).metaType().id() != QMetaType::QString
            || !fields.contains(QStringLiteral("doseG"))
            || !fields.contains(QStringLiteral("grindRpm"))
            || !fields.contains(QStringLiteral("yieldMode"))
            || !fields.contains(QStringLiteral("yieldValue"))
            || !fields.contains(QStringLiteral("temperatureC"))) {
            finish(requestId, operation, false,
                   QStringLiteral("Brew settings are invalid or the machine state changed."));
            return;
        }

        BrewSettingsValues values;
        double dose = 0.0;
        double yield = 0.0;
        double temperature = 0.0;
        double rpmNumber = 0.0;
        if (!strictFiniteNumber(fields.value(QStringLiteral("doseG")), &dose)
            || !strictFiniteNumber(fields.value(QStringLiteral("yieldValue")), &yield)
            || !strictFiniteNumber(fields.value(QStringLiteral("temperatureC")), &temperature)
            || !strictFiniteNumber(fields.value(QStringLiteral("grindRpm")), &rpmNumber)
            || std::trunc(rpmNumber) != rpmNumber
            || rpmNumber < -1.0 || rpmNumber > std::numeric_limits<int>::max()) {
            finish(requestId, operation, false,
                   QStringLiteral("Brew settings contain invalid numeric values."));
            return;
        }
        values.doseG = dose;
        values.grindSetting = fields.value(QStringLiteral("grindSetting")).toString();
        values.grindRpm = static_cast<int>(rpmNumber);
        values.yieldMode = fields.value(QStringLiteral("yieldMode")).toString();
        values.yieldValue = yield;
        values.temperatureC = temperature;

        const BrewSettingsSnapshot current = profiles->brewSettingsSnapshot();
        const QVariantMap currentView = brewSettingsSnapshotMap(current, m_controller);
        if (expectedRevision != currentView.value(QStringLiteral("revision")).toString()) {
            finish(requestId, operation, false,
                   QStringLiteral("Brew settings changed; review the current values before saving."),
                   {{QStringLiteral("status"), QStringLiteral("conflict")},
                    {QStringLiteral("snapshot"), currentView},
                    {QStringLiteral("revision"), currentView.value(QStringLiteral("revision"))}});
            return;
        }

        // The dialog view revision also covers MainController-owned recipe
        // baselines. ProfileManager keeps its own canonical CAS revision for
        // the values it owns; both checks happen synchronously on this thread.
        const BrewSettingsApplyResult applied = profiles->applyBrewSettings(current.revision,
                                                                              values);
        const QVariantMap snapshot = brewSettingsSnapshotMap(profiles->brewSettingsSnapshot(),
                                                              m_controller);
        const bool success = applied.status == BrewSettingsApplyStatus::Applied;
        finish(requestId, operation, success, success ? QString() : applied.error,
               {{QStringLiteral("status"), success ? QStringLiteral("applied")
                                                   : applied.status == BrewSettingsApplyStatus::Conflict
                                                       ? QStringLiteral("conflict")
                                                       : QStringLiteral("invalid")},
                {QStringLiteral("snapshot"), snapshot},
                {QStringLiteral("revision"), snapshot.value(QStringLiteral("revision"))}});
        return;
    }

    if (operation == QLatin1String("brew-settings-update-profile-temperature")) {
        ProfileManager* profiles = m_controller->profileManager();
        QString expectedRevision;
        double valueC = 0.0;
        if (!profiles || !currentContext() || fields.size() != 2
            || !validBrewRevision(fields.value(QStringLiteral("expectedRevision")),
                                  &expectedRevision)
            || !strictFiniteNumber(fields.value(QStringLiteral("valueC")), &valueC)
            || !ProfileManager::isBrewTemperatureInRange(valueC)) {
            finish(requestId, operation, false,
                   QStringLiteral("Profile temperature is invalid or the machine state changed."));
            return;
        }
        const BrewSettingsSnapshot before = profiles->brewSettingsSnapshot();
        const QVariantMap beforeView = brewSettingsSnapshotMap(before, m_controller);
        if (expectedRevision != beforeView.value(QStringLiteral("revision")).toString()) {
            finish(requestId, operation, false,
                   QStringLiteral("Brew settings changed; review the current values before saving."),
                   {{QStringLiteral("status"), QStringLiteral("conflict")},
                    {QStringLiteral("snapshot"), beforeView},
                    {QStringLiteral("revision"), beforeView.value(QStringLiteral("revision"))}});
            return;
        }
        if (before.activeRecipeId >= 0) {
            finish(requestId, operation, false,
                   QStringLiteral("Update the active recipe's temperature instead of the shared profile."));
            return;
        }
        const bool saved = profiles->applyTemperatureToProfile(valueC);
        const QVariantMap snapshot = brewSettingsSnapshotMap(profiles->brewSettingsSnapshot(),
                                                              m_controller);
        finish(requestId, operation, true, {},
               {{QStringLiteral("saved"), saved},
                {QStringLiteral("snapshot"), snapshot},
                {QStringLiteral("revision"), snapshot.value(QStringLiteral("revision"))}});
        return;
    }

    if (operation == QLatin1String("apply-hot-water-settings")
        || operation == QLatin1String("apply-flush-settings")) {
        if (!fields.isEmpty() || !currentContext()) {
            finish(requestId, operation, false, QStringLiteral("Preset settings could not be applied."));
            return;
        }
        if (operation == QLatin1String("apply-hot-water-settings"))
            m_controller->applyHotWaterSettings();
        else
            m_controller->applyFlushSettings();
        finish(requestId, operation, true);
        return;
    }

    if (operation == QLatin1String("set-hot-water-flow")) {
        int flow = 0;
        const bool activeHotWater = m_machineState
            && m_machineState->phase() == MachineState::Phase::HotWater;
        if (!readIndex(fields, QStringLiteral("flow"), &flow)
            || flow < 5 || flow > 100 || flow % 5 != 0
            || !m_machineState || !MachineControlStartPolicy::contextMatches(
                expectedContextId, m_machineState->controlContextId())
            || !activeHotWater || !m_device || !m_device->isConnected()) {
            finish(requestId, operation, false,
                   QStringLiteral("The hot-water flow control is no longer active."));
            return;
        }
        m_controller->setHotWaterFlowRateImmediate(flow);
        finish(requestId, operation, true, {}, {{QStringLiteral("flow"), flow}});
        return;
    }

    if (operation == QLatin1String("bump-flush-seconds")) {
        double deltaSec = 0.0;
        if (fields.size() != 1 || !fields.contains(QStringLiteral("deltaSec"))
            || !strictFiniteNumber(fields.value(QStringLiteral("deltaSec")), &deltaSec)
            || (deltaSec != -5.0 && deltaSec != 5.0)
            || !m_machineState || !MachineControlStartPolicy::contextMatches(
                expectedContextId, m_machineState->controlContextId())
            || m_machineState->phase() != MachineState::Phase::Flushing
            || !m_device || !m_device->isConnected()) {
            finish(requestId, operation, false,
                   QStringLiteral("The flush time can no longer be adjusted."));
            return;
        }
        m_controller->bumpFlushSeconds(deltaSec);
        finish(requestId, operation, true, {},
               {{QStringLiteral("flushSeconds"), m_settings->brew()->flushSeconds()}});
        return;
    }

    if (operation == QLatin1String("stop-hot-water")
        || operation == QLatin1String("stop-flush")) {
        const MachineState::Phase expectedPhase = operation == QLatin1String("stop-hot-water")
            ? MachineState::Phase::HotWater : MachineState::Phase::Flushing;
        if (!fields.isEmpty() || !m_machineState || !MachineControlStartPolicy::contextMatches(
                expectedContextId, m_machineState->controlContextId())
            || m_machineState->phase() != expectedPhase
            || !m_device || !m_device->isConnected()) {
            finish(requestId, operation, false,
                   QStringLiteral("The operation changed before it could be stopped."));
            return;
        }
        if (operation == QLatin1String("stop-flush"))
            emit m_controller->homeFlushStopRequested();
        m_device->stopOperation();
        finish(requestId, operation, true, {}, {{QStringLiteral("stopped"), true}});
        return;
    }

    if (operation == QLatin1String("select-steam-pitcher")) {
        int index = -1;
        QVariantMap expectedPreset;
        if (!readPresetIndex(fields, QStringLiteral("index"), &index, &expectedPreset)) {
            finish(requestId, operation, false,
                   QStringLiteral("Invalid steam-pitcher selection."));
            return;
        }
        const QVariantMap currentPreset = m_settings->brew()->getSteamPitcherPreset(index);
        if (!MachineControlPresets::matchesSnapshot(expectedPreset, currentPreset)) {
            finish(requestId, operation, false,
                   QStringLiteral("The selected steam pitcher changed. Review it and try again."));
            return;
        }
        m_controller->selectSteamPitcher(index,
            m_controller->preparation()
                ? m_controller->preparation()->sessionMeasuredMilkG() : 0.0);
        finish(requestId, operation, true, {}, {{QStringLiteral("index"), index}});
        return;
    }

    if (operation == QLatin1String("select-water-vessel")) {
        int index = -1;
        QVariantMap expectedPreset;
        if (!readPresetIndex(fields, QStringLiteral("index"), &index, &expectedPreset)) {
            finish(requestId, operation, false, QStringLiteral("Invalid water-vessel selection."));
            return;
        }
        if (!MachineControlPresets::matchesSnapshot(
                expectedPreset, m_settings->brew()->getWaterVesselPreset(index))) {
            finish(requestId, operation, false,
                   QStringLiteral("The selected water-vessel preset changed. Review it and try again."));
            return;
        }
        if (!m_controller->selectWaterVessel(index)) {
            finish(requestId, operation, false,
                   QStringLiteral("The selected water-vessel preset is no longer available."));
            return;
        }
        finish(requestId, operation, true, {}, {{QStringLiteral("index"), index}});
        return;
    }

    if (operation == QLatin1String("select-flush-preset")) {
        int index = -1;
        QVariantMap expectedPreset;
        if (!readPresetIndex(fields, QStringLiteral("index"), &index, &expectedPreset)) {
            finish(requestId, operation, false, QStringLiteral("Invalid flush-preset selection."));
            return;
        }
        if (!MachineControlPresets::matchesSnapshot(
                expectedPreset, m_settings->brew()->getFlushPreset(index))) {
            finish(requestId, operation, false,
                   QStringLiteral("The selected flush preset changed. Review it and try again."));
            return;
        }
        if (!m_controller->selectFlushPreset(index)) {
            finish(requestId, operation, false,
                   QStringLiteral("The selected flush preset is no longer available."));
            return;
        }
        finish(requestId, operation, true, {}, {{QStringLiteral("index"), index}});
        return;
    }

    QString gateReason;
    if (!validateStart(expectedContextId, &gateReason)) {
        finish(requestId, operation, false, gateReason);
        return;
    }

    if (operation == QLatin1String("start-recipe")) {
        qint64 recipeId = -1;
        if (!readPositiveId(fields, QStringLiteral("recipeId"), &recipeId)) {
            finish(requestId, operation, false, QStringLiteral("Invalid recipe-start request."));
            return;
        }
        if (m_controller->selectedRecipeId() != recipeId) {
            finish(requestId, operation, false,
                   QStringLiteral("The selected recipe changed. Select it again before starting."));
            return;
        }
        m_recipeStartRequests.insert(requestId);
        m_controller->requestStartRecipeShotWhenApplied(requestId, recipeId, expectedContextId);
        return;
    }

    if (operation == QLatin1String("start-espresso")) {
        if (!onlyField(fields, QStringLiteral("profileFilename"))
            || fields.value(QStringLiteral("profileFilename")).metaType().id() != QMetaType::QString) {
            finish(requestId, operation, false, QStringLiteral("Invalid espresso-start request."));
            return;
        }
        const QString expectedFilename = fields.value(QStringLiteral("profileFilename")).toString();
        if (expectedFilename.isEmpty()
            || m_settings->app()->currentProfile() != expectedFilename
            || m_controller->selectedRecipeId() > 0) {
            finish(requestId, operation, false,
                   QStringLiteral("The selected profile changed. Select it again before starting."));
            return;
        }
        m_device->startEspresso();
        finish(requestId, operation, true, {}, {{QStringLiteral("dispatched"), true}});
        return;
    }

    if (operation == QLatin1String("start-steam")) {
        int index = -1;
        QVariantMap expectedPreset;
        if (!readPresetIndex(fields, QStringLiteral("pitcherIndex"), &index, &expectedPreset)) {
            finish(requestId, operation, false, QStringLiteral("Invalid steam-pitcher selection."));
            return;
        }
        const QVariantMap preset = m_settings->brew()->getSteamPitcherPreset(index);
        if (!MachineControlPresets::matchesSelection(
                index, m_settings->brew()->selectedSteamPitcher(), expectedPreset, preset)
            || preset.value(QStringLiteral("disabled")).toBool()) {
            finish(requestId, operation, false,
                   QStringLiteral("The selected steam pitcher changed. Select it again before starting."));
            return;
        }
        // Native Home's second tap reapplies the selected pitcher so milk is
        // recalculated from the current scale before the machine starts.
        const double measuredMilkG = m_controller->preparation()
            ? m_controller->preparation()->sessionMeasuredMilkG() : 0.0;
        m_controller->selectSteamPitcher(index, measuredMilkG);
        QString applyReason;
        if (!validateStart(expectedContextId, &applyReason)
            || !MachineControlPresets::matchesSelection(
                index, m_settings->brew()->selectedSteamPitcher(), expectedPreset,
                m_settings->brew()->getSteamPitcherPreset(index))) {
            finish(requestId, operation, false,
                   applyReason.isEmpty()
                       ? QStringLiteral("The steam pitcher changed before the machine could start.")
                       : applyReason);
            return;
        }
        m_device->startSteam();
        finish(requestId, operation, true, {}, {{QStringLiteral("dispatched"), true}});
        return;
    }

    if (operation == QLatin1String("start-hot-water")) {
        int index = -1;
        QVariantMap expectedPreset;
        if (!readPresetIndex(fields, QStringLiteral("waterVesselIndex"), &index, &expectedPreset)
            || !MachineControlPresets::matchesSelection(
                index, m_settings->brew()->selectedWaterVessel(), expectedPreset,
                m_settings->brew()->getWaterVesselPreset(index))) {
            finish(requestId, operation, false,
                   QStringLiteral("The selected water-vessel preset changed. Select it again before starting."));
            return;
        }
        if (!m_controller->selectWaterVessel(index)) {
            finish(requestId, operation, false,
                   QStringLiteral("The selected water-vessel preset is no longer available."));
            return;
        }
        QString applyReason;
        if (!validateStart(expectedContextId, &applyReason)
            || !MachineControlPresets::matchesSelection(
                index, m_settings->brew()->selectedWaterVessel(), expectedPreset,
                m_settings->brew()->getWaterVesselPreset(index))) {
            finish(requestId, operation, false,
                   applyReason.isEmpty()
                       ? QStringLiteral("The water-vessel preset changed before the machine could start.")
                       : applyReason);
            return;
        }
        m_device->startHotWater();
        finish(requestId, operation, true, {}, {{QStringLiteral("dispatched"), true}});
        return;
    }

    if (operation == QLatin1String("start-flush")) {
        int index = -1;
        QVariantMap expectedPreset;
        if (!readPresetIndex(fields, QStringLiteral("flushPresetIndex"), &index, &expectedPreset)
            || !MachineControlPresets::matchesSelection(
                index, m_settings->brew()->selectedFlushPreset(), expectedPreset,
                m_settings->brew()->getFlushPreset(index))) {
            finish(requestId, operation, false,
                   QStringLiteral("The selected flush preset changed. Select it again before starting."));
            return;
        }
        if (!m_controller->selectFlushPreset(index)) {
            finish(requestId, operation, false,
                   QStringLiteral("The selected flush preset is no longer available."));
            return;
        }
        QString applyReason;
        if (!validateStart(expectedContextId, &applyReason)
            || !MachineControlPresets::matchesSelection(
                index, m_settings->brew()->selectedFlushPreset(), expectedPreset,
                m_settings->brew()->getFlushPreset(index))) {
            finish(requestId, operation, false,
                   applyReason.isEmpty()
                       ? QStringLiteral("The flush preset changed before the machine could start.")
                       : applyReason);
            return;
        }
        m_device->startFlush();
        finish(requestId, operation, true, {}, {{QStringLiteral("dispatched"), true}});
        return;
    }

    finish(requestId, operation, false, QStringLiteral("Unknown machine control operation."));
}

void NativeMachineControlServiceSource::onSimulatedShotRequestCompleted(
    const QString& requestId, bool success, qint64 shotId)
{
    if (!m_simulatedShotRequests.remove(requestId))
        return;
    finish(requestId, QStringLiteral("generate-fake-shot"), success,
           success ? QString() : QStringLiteral("The simulated shot could not be saved."),
           success ? QVariantMap{{QStringLiteral("shotId"), shotId}} : QVariantMap{});
}

bool NativeMachineControlServiceSource::validateStart(const QString& expectedContextId,
                                                      QString* reason) const
{
    if (!m_machineState || !m_device || !m_settings || !m_settings->app()) {
        if (reason) *reason = QStringLiteral("Machine controls are unavailable.");
        return false;
    }
    if (!MachineControlStartPolicy::machineCanStart(
            expectedContextId, m_machineState->controlContextId(),
            m_machineState->isReady(), m_device->isHeadless(), m_device->simulationMode())) {
        if (expectedContextId.isEmpty()
            || expectedContextId != m_machineState->controlContextId()) {
            if (reason) *reason = QStringLiteral("The machine state changed. Review it before starting.");
        } else if (!m_machineState->isReady()) {
            if (reason) *reason = QStringLiteral("The machine is not ready to start an operation.");
        } else if (reason) {
            *reason = QStringLiteral("Starting from this app is disabled while the group head control is active.");
        }
        return false;
    }
    return true;
}

void NativeMachineControlServiceSource::finish(const QString& requestId,
                                               const QString& operation,
                                               bool success, const QString& message,
                                               const QVariantMap& result)
{
    if (!m_requestIds.finish(requestId))
        return;
    emit operationCompleted(requestId, operation, success, message, result);
}

void NativeMachineControlServiceSource::onRecipeStartCompleted(const QString& requestId,
                                                                qint64 recipeId,
                                                                bool success,
                                                                const QString& reason)
{
    if (!m_recipeStartRequests.remove(requestId))
        return;
    finish(requestId, QStringLiteral("start-recipe"), success, reason,
           success ? QVariantMap{{QStringLiteral("recipeId"), recipeId},
                                 {QStringLiteral("dispatched"), true}} : QVariantMap{});
}
