#include "hostsessionservicesource.h"
#include "core/packageidentity.h"
#include "servicepayload.h"
#include "network/sessionappearance.h"
#include "ble/de1device.h"
#include "ble/scaledeviceproxy.h"
#include "ble/blemanager.h"
#include "ble/belkaportaldevice.h"
#ifndef Q_OS_IOS
#include "usb/usbscalemanager.h"
#endif
#include "controllers/maincontroller.h"
#include "machine/machinestate.h"
#include "core/settings.h"
#include "core/accessibilitymanager.h"

#include <QUuid>

NativeHostSessionServiceSource::NativeHostSessionServiceSource(Settings* settings,
    DE1Device* device, MachineState* machineState, MainController* controller,
    ScaleDeviceProxy* scale, BLEManager* ble, UsbScaleManager* usbScale,
    BelkaPortalDevice* portal, AccessibilityManager* accessibility, QObject* parent)
    : HostSessionStateSimpleSource(parent), m_settings(settings), m_device(device),
      m_machineState(machineState), m_mainController(controller), m_scaleDeviceProxy(scale),
      m_bleManager(ble), m_usbScaleManager(usbScale), m_belkaPortal(portal),
      m_accessibility(accessibility)
{
    m_packageId = QString::fromLatin1(decenzaPackageId());
    m_hostId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_publishTimer.setInterval(250);
    connect(&m_publishTimer, &QTimer::timeout, this, &NativeHostSessionServiceSource::publish);
    m_publishTimer.start();
}

void NativeHostSessionServiceSource::setIdentity(const QString& packageId, const QString& hostId)
{
    if (m_packageId == packageId && m_hostId == hostId)
        return;
    setReady(false);
    m_packageId = packageId;
    m_hostId = hostId;
    m_sequence = 0;
    setSnapshot({});
    publish();
}

void NativeHostSessionServiceSource::setAppearance(const QVariantMap& appearance)
{
    QVariantMap validated;
    if (!SessionAppearance::validate(appearance, &validated))
        validated.clear();
    if (m_appearance == validated)
        return;
    m_appearance = validated;
    publish();
}

void NativeHostSessionServiceSource::publish()
{
    if (!m_settings || m_packageId.isEmpty() || QUuid(m_hostId).isNull()
        || m_appearance.isEmpty()) {
        setReady(false);
        return;
    }
    const QVariantMap snapshot{
        {QStringLiteral("packageId"), m_packageId},
        {QStringLiteral("hostId"), m_hostId},
        {QStringLiteral("sequence"), QVariant::fromValue(++m_sequence)},
        {QStringLiteral("machineUi"), machineUiSnapshot().toJson().toVariantMap()},
        {QStringLiteral("appearance"), m_appearance},
        {QStringLiteral("pressureBar"), m_device ? m_device->pressure() : 0.0},
        {QStringLiteral("flowMlPerSecond"), m_device ? m_device->flow() : 0.0},
        {QStringLiteral("accessibilityEnabled"), m_accessibility && m_accessibility->enabled()},
        {QStringLiteral("extractionAnnouncementsEnabled"), !m_accessibility || m_accessibility->extractionAnnouncementsEnabled()},
        {QStringLiteral("extractionAnnouncementInterval"), m_accessibility ? m_accessibility->extractionAnnouncementInterval() : 5},
        {QStringLiteral("extractionAnnouncementMode"), m_accessibility ? m_accessibility->extractionAnnouncementMode() : QStringLiteral("both")}
    };
    if (!ServicePayload::fits(snapshot, 128 * 1024)) {
        setReady(false);
        return;
    }
    setSnapshot(snapshot);
    setReady(true);
}

MachineUiSnapshot NativeHostSessionServiceSource::machineUiSnapshot() const
{
    return captureMachineUi(m_device, m_machineState, m_mainController, m_scaleDeviceProxy,
                            m_bleManager, m_usbScaleManager, m_settings, m_belkaPortal);
}

MachineUiSnapshot NativeHostSessionServiceSource::captureMachineUi(DE1Device* m_device,
    MachineState* m_machineState, MainController* m_mainController,
    ScaleDeviceProxy* m_scaleDeviceProxy, BLEManager* m_bleManager,
    UsbScaleManager* m_usbScaleManager, Settings* m_settings, BelkaPortalDevice* m_belkaPortal)
{
#ifdef Q_OS_IOS
    Q_UNUSED(m_usbScaleManager)
#endif
    MachineUiSnapshot machineUi;
    if (m_device) {
        machineUi.de1Connected = m_device->isConnected();
        machineUi.de1GuiEnabled = m_device->isGuiEnabled();
        machineUi.simulationMode = m_device->simulationMode();
        machineUi.de1IsHeadless = m_device->isHeadless();
        machineUi.de1State = m_device->stateInt();
        machineUi.de1SubState = m_device->subStateInt();
        machineUi.stateString = m_device->stateString();
        machineUi.subStateString = m_device->subStateString();
        machineUi.temperature = m_device->temperature();
        machineUi.steamTemperature = m_device->steamTemperature();
        machineUi.waterLevel = m_device->waterLevel();
        machineUi.waterLevelMm = m_device->waterLevelMm();
        machineUi.waterLevelMl = m_device->waterLevelMl();
        machineUi.refillKitDetected = m_device->refillKitDetected();
    }
    if (m_scaleDeviceProxy) {
        machineUi.scaleConnected = m_scaleDeviceProxy->connected();
        machineUi.scaleIsFlowScale = m_scaleDeviceProxy->isFlowScale();
        machineUi.scaleName = m_scaleDeviceProxy->name();
        machineUi.scaleBatteryLevel = m_scaleDeviceProxy->batteryLevel();
        machineUi.scaleCharging = m_scaleDeviceProxy->charging();
    }
    if (m_bleManager)
        machineUi.bleScaleConnectionFailed = m_bleManager->scaleConnectionFailed();
#ifndef Q_OS_IOS
    if (m_usbScaleManager)
        machineUi.usbScaleConnected = m_usbScaleManager->isScaleConnected();
#endif
    if (m_machineState) {
        machineUi.controlContextId = m_machineState->controlContextId();
        machineUi.phase = static_cast<MachinePhase>(
            static_cast<int>(m_machineState->phase()));
        machineUi.isFlowing = m_machineState->isFlowing();
        machineUi.isHeating = m_machineState->isHeating();
        machineUi.isReady = m_machineState->isReady();
        machineUi.scaleWeight = m_machineState->scaleWeight();
        machineUi.scaleFlowRate = m_machineState->scaleFlowRate();
        machineUi.cumulativeVolume = m_machineState->cumulativeVolume();
        machineUi.preinfusionVolume = m_machineState->preinfusionVolume();
        machineUi.pourVolume = m_machineState->pourVolume();
        machineUi.shotTime = m_machineState->shotTime();
        machineUi.targetWeight = m_machineState->targetWeight();
        machineUi.targetVolume = m_machineState->targetVolume();
        machineUi.smoothedScaleFlowRate = m_machineState->smoothedScaleFlowRate();
        machineUi.canDecreaseTargetWeight = m_machineState->canDecreaseTargetWeight();
        machineUi.hotWaterTargetWeight = m_machineState->hotWaterTargetWeight();
        machineUi.standbySwitchOpen = m_machineState->standbySwitchOpen();
    }
    if (m_mainController) {
        machineUi.filteredGoalPressure = m_mainController->filteredGoalPressure();
        machineUi.filteredGoalFlow = m_mainController->filteredGoalFlow();
        machineUi.steamHeaterOn = m_mainController->steamHeaterOn();
        machineUi.steamSoftStopped = m_mainController->steamSoftStopped();
        machineUi.steamAutoFlushCountdown = m_mainController->steamAutoFlushCountdown();
        machineUi.temperatureIsRealOverride = m_mainController->temperatureIsRealOverride();
        machineUi.yieldIsRealOverride = m_mainController->yieldIsRealOverride();
        machineUi.activeBaselineYieldG = m_mainController->activeBaselineYieldG();
        machineUi.activeRecipeTempOffsetC = m_mainController->activeRecipeTempOffsetC();
    }
    if (m_settings) {
        machineUi.hardwareSteamTwoTapStop = m_settings->hardware()->steamTwoTapStop();
        machineUi.primaryScaleAddress = m_settings->primaryScaleAddress();
        machineUi.brewHasTemperatureOverride = m_settings->brew()->hasTemperatureOverride();
        machineUi.brewTemperatureOverride = m_settings->brew()->temperatureOverride();
        machineUi.brewSteamTemperature = m_settings->brew()->steamTemperature();
        machineUi.brewSteamTimeout = m_settings->brew()->steamTimeout();
        machineUi.brewSteamFlow = m_settings->brew()->steamFlow();
        machineUi.brewSteamAutoFlushSeconds = m_settings->brew()->steamAutoFlushSeconds();
        machineUi.brewMilkAutoCaptureEnabled = m_settings->brew()->milkAutoCaptureEnabled();
        machineUi.brewSteamSecondsPerGram = m_settings->brew()->steamSecondsPerGram();
        machineUi.brewLastSteamMilkG = m_settings->brew()->lastSteamMilkG();
        machineUi.brewLastSteamTimeS = m_settings->brew()->lastSteamTimeS();
        machineUi.brewDoseCupTareWeight = m_settings->brew()->doseCupTareWeight();
        machineUi.appWaterLevelDisplayUnit = m_settings->app()->waterLevelDisplayUnit();
        machineUi.appWaterRefillPoint = m_settings->app()->waterRefillPoint();
        machineUi.appRefillKitOverride = m_settings->app()->refillKitOverride();
    }
    if (m_mainController && m_mainController->profileManager()) {
        const ProfileManager* profileManager = m_mainController->profileManager();
        machineUi.profileTargetTemperature = profileManager->profileTargetTemperature();
        machineUi.profileTargetWeight = profileManager->profileTargetWeight();
        machineUi.profileEffectiveTargetWeight = profileManager->targetWeight();
        machineUi.profileBrewByRatio = profileManager->brewByRatio();
        machineUi.profileBrewByRatioDose = profileManager->brewByRatioDose();
        machineUi.profileBrewByRatioActive = profileManager->brewByRatioActive();
    }
    if (m_belkaPortal) {
        machineUi.portalState = m_belkaPortal->state();
        machineUi.portalActive = m_belkaPortal->active();
        machineUi.portalHasReading = m_belkaPortal->hasReading();
        machineUi.portalTemperatureC = m_belkaPortal->temperatureC();
        machineUi.portalEcRaw = m_belkaPortal->ecRaw();
        machineUi.portalOwned = m_belkaPortal->owned();
        machineUi.portalMachineBusy = m_belkaPortal->machineBusy();
    }
    return machineUi;
}
