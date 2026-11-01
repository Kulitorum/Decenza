#include "settingsconnectionsservicesource.h"

#include "ble/blemanager.h"
#include "ble/belkaportaldevice.h"
#include "ble/de1device.h"
#include "ble/scaledeviceproxy.h"
#include "core/hdsfirmwareupdatecontroller.h"
#include "core/settings.h"
#include "history/shothistorystorage.h"
#include "machine/machinestate.h"
#ifndef Q_OS_IOS
#include "usb/usbmanager.h"
#include "usb/usbscalemanager.h"
#endif
#include "servicevalue.h"

#include <QHostAddress>
#include <QMetaType>
#include <QRegularExpression>
#include <QVariant>
#include <QUuid>
#include <limits>

namespace {
QString startReason(ServiceRequestIds::Start start)
{
    switch (start) {
    case ServiceRequestIds::Start::InvalidId: return QStringLiteral("invalidRequest");
    case ServiceRequestIds::Start::DuplicateId: return QStringLiteral("duplicateRequest");
    case ServiceRequestIds::Start::LimitReached: return QStringLiteral("busy");
    case ServiceRequestIds::Start::Accepted: break;
    }
    return {};
}

bool validHostName(QString value)
{
    value = value.trimmed();
    if (value.isEmpty() || value.size() > 253 || value.contains(QRegularExpression("[\\s/\\\\?#@]"))
        || value.contains(QStringLiteral("://")))
        return false;
    QHostAddress address;
    if (address.setAddress(value))
        return true;
    // DNS names, including the supported `.local` names. Reject empty labels,
    // leading/trailing hyphens, and URL delimiters before handing off to BLEManager.
    static const QRegularExpression hostname(
        QStringLiteral("^(?=.{1,253}$)(?:[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?)(?:\\.(?:[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?))*\\.?$"));
    return hostname.match(value).hasMatch();
}

} // namespace

NativeSettingsConnectionsServiceSource::NativeSettingsConnectionsServiceSource(
    Settings* settings, BLEManager* bleManager, DE1Device* de1Device,
    ScaleDeviceProxy* scaleDevice, MachineState* machineState, USBManager* usbManager,
    UsbScaleManager* usbScaleManager, BelkaPortalDevice* portalDevice,
    HdsFirmwareUpdateController* hdsFirmwareUpdate, QObject* parent)
    : ConnectionsStateSimpleSource(parent)
    , m_settings(settings)
    , m_bleManager(bleManager)
    , m_de1Device(de1Device)
    , m_scaleDevice(scaleDevice)
    , m_machineState(machineState)
#ifndef Q_OS_IOS
    , m_usbManager(usbManager)
    , m_usbScaleManager(usbScaleManager)
#endif
    , m_portalDevice(portalDevice)
    , m_hdsFirmwareUpdate(hdsFirmwareUpdate)
{
#ifdef Q_OS_IOS
    Q_UNUSED(usbManager)
    Q_UNUSED(usbScaleManager)
#endif
    if (m_settings) {
        connect(m_settings, &Settings::knownScalesChanged, this, [this] {
            m_knownScalesRevision = m_knownScalesRevision == std::numeric_limits<int>::max()
                ? 1 : m_knownScalesRevision + 1;
            refreshSnapshot();
        });
        connect(m_settings, &Settings::savedRefractometerChanged, this, [this] {
            m_refractometerRevision = m_refractometerRevision == std::numeric_limits<int>::max()
                ? 1 : m_refractometerRevision + 1;
            refreshSnapshot();
        });
        if (m_settings->hardware()) {
            connect(m_settings->hardware(), &SettingsHardware::portalMountingModeChanged,
                    this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
            connect(m_settings->hardware(), &SettingsHardware::portalMountingModeChanged,
                    this, &NativeSettingsConnectionsServiceSource::refreshPortalCalibration);
        }
        connect(m_settings->dye(), &SettingsDye::activeBagIdChanged,
                this, &NativeSettingsConnectionsServiceSource::refreshPortalCalibration);
        connect(m_settings->dye(), &SettingsDye::dyeBeanBrandChanged,
                this, &NativeSettingsConnectionsServiceSource::refreshPortalCalibration);
        connect(m_settings->dye(), &SettingsDye::dyeBeanTypeChanged,
                this, &NativeSettingsConnectionsServiceSource::refreshPortalCalibration);
    }

    if (m_bleManager) {
        connect(m_bleManager, &BLEManager::scanningChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
        connect(m_bleManager, &BLEManager::bluetoothAvailableChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
        connect(m_bleManager, &BLEManager::devicesChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
        connect(m_bleManager, &BLEManager::scalesChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
        connect(m_bleManager, &BLEManager::refractometersChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
        connect(m_bleManager, &BLEManager::scaleConnectionFailedChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
        connect(m_bleManager, &BLEManager::scaleConnectingChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
        connect(m_bleManager, &BLEManager::refractometerConnectedChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
        const auto cancelManualWifiOwner = [this] {
            const QString requestId = m_manualWifiConnectOwner.requestId();
            if (requestId.isEmpty())
                return;
            emit manualWifiValidationCancelled(requestId);
            m_manualWifiConnectOwner.release(requestId);
            refreshSnapshot();
        };
        connect(m_bleManager, &BLEManager::manualWifiValidationCancelled, this,
                [cancelManualWifiOwner](const QString&) { cancelManualWifiOwner(); });
        connect(m_bleManager, &BLEManager::disconnectScaleRequested, this,
                [this, cancelManualWifiOwner] {
            // connectToWifiScale may first drop the currently connected scale;
            // that is part of starting this request, not cancellation.
            if (!m_startingManualWifiConnect)
                cancelManualWifiOwner();
        });
        connect(m_bleManager, &BLEManager::scaleDiscovered, this,
                [this, cancelManualWifiOwner](const QBluetoothDeviceInfo&, const QString&) {
            // A different discovery can replace the pending WiFi driver before
            // it emits a disconnect request (for example an auto-connect scan).
            if (!m_startingManualWifiConnect)
                cancelManualWifiOwner();
        });
        connect(m_bleManager, &BLEManager::manualWifiMdnsDiscovered, this,
                [this](const QString& hostname, const QString& ip) {
            m_manualWifiHostname = hostname;
            m_manualWifiIp = ip;
            const QString requestId = m_manualWifiProbeOwner.requestId();
            if (!requestId.isEmpty())
                emit manualWifiMdnsDiscovered(requestId, hostname, ip);
            refreshSnapshot();
        });
        connect(m_bleManager, &BLEManager::manualWifiMdnsProbeFinished, this, [this] {
            m_manualWifiProbeInProgress = false;
            const QString requestId = m_manualWifiProbeOwner.requestId();
            if (!requestId.isEmpty()) {
                emit manualWifiMdnsProbeFinished(requestId);
                m_manualWifiProbeOwner.release(requestId);
            }
            refreshSnapshot();
        });
        connect(m_bleManager, &BLEManager::manualWifiValidationFailed, this,
                [this](const QString& host) {
            m_manualWifiError = host;
            const QString requestId = m_manualWifiConnectOwner.requestId();
            if (!requestId.isEmpty()) {
                emit manualWifiValidationFailed(requestId, host);
                m_manualWifiConnectOwner.release(requestId);
            }
            refreshSnapshot();
        });
        connect(m_bleManager, &BLEManager::manualWifiValidationSucceeded, this,
                [this](const QString& host) {
            m_manualWifiError.clear();
            const QString requestId = m_manualWifiConnectOwner.requestId();
            if (!requestId.isEmpty()) {
                emit manualWifiValidationSucceeded(requestId, host);
                m_manualWifiConnectOwner.release(requestId);
            }
            refreshSnapshot();
        });
    }

    if (m_de1Device) {
        connect(m_de1Device, &DE1Device::connectedChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
        connect(m_de1Device, &DE1Device::connectingChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
        connect(m_de1Device, &DE1Device::stateChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
        connect(m_de1Device, &DE1Device::firmwareVersionChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
        connect(m_de1Device, &DE1Device::simulationModeChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
    }
    if (m_scaleDevice) {
        connect(m_scaleDevice, &ScaleDeviceProxy::connectedChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
        connect(m_scaleDevice, &ScaleDeviceProxy::batteryLevelChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
        connect(m_scaleDevice, &ScaleDeviceProxy::chargingChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
        connect(m_scaleDevice, &ScaleDeviceProxy::targetChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
    }
#ifndef Q_OS_IOS
    if (m_usbManager)
        connect(m_usbManager, &USBManager::de1ConnectedChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
    if (m_usbScaleManager)
        connect(m_usbScaleManager, &UsbScaleManager::scaleConnectedChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
#endif
    if (m_portalDevice) {
        connect(m_portalDevice, &BelkaPortalDevice::stateChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
        connect(m_portalDevice, &BelkaPortalDevice::devicesChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
        connect(m_portalDevice, &BelkaPortalDevice::machineBusyChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
        connect(m_portalDevice, &BelkaPortalDevice::savedDeviceChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
        connect(m_portalDevice, &BelkaPortalDevice::readingChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
        connect(m_portalDevice, &BelkaPortalDevice::errorMessageChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
        connect(m_portalDevice, &BelkaPortalDevice::displayCommandStatusChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
        connect(m_portalDevice, &BelkaPortalDevice::syncDisplayChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
    }
    if (m_hdsFirmwareUpdate) {
        connect(m_hdsFirmwareUpdate, &HdsFirmwareUpdateController::activeScaleChanged, this, [this] {
            refreshSnapshot();
            emit activeHdsScaleChanged();
        });
        connect(m_hdsFirmwareUpdate, &HdsFirmwareUpdateController::checkingChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
        connect(m_hdsFirmwareUpdate, &HdsFirmwareUpdateController::updateAvailableChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
        connect(m_hdsFirmwareUpdate, &HdsFirmwareUpdateController::availabilityChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
        connect(m_hdsFirmwareUpdate, &HdsFirmwareUpdateController::releaseNotesChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
        connect(m_hdsFirmwareUpdate, &HdsFirmwareUpdateController::releaseNotesLoadingChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
        connect(m_hdsFirmwareUpdate, &HdsFirmwareUpdateController::updateStartedChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
        connect(m_hdsFirmwareUpdate, &HdsFirmwareUpdateController::updateErrorChanged, this, &NativeSettingsConnectionsServiceSource::refreshSnapshot);
    }
    refreshSnapshot();
}

void NativeSettingsConnectionsServiceSource::refresh()
{
    refreshSnapshot();
    refreshPortalCalibration();
}

void NativeSettingsConnectionsServiceSource::setShotHistory(ShotHistoryStorage* history)
{
    if (m_shotHistory) disconnect(m_shotHistory, nullptr, this, nullptr);
    m_shotHistory = history;
    if (history) {
        connect(history, &ShotHistoryStorage::portalCalibrationReady, this,
            [this](const QString& id, const QVariantMap& calibration, const QString& reason) {
                if (id != m_portalCalibrationRequestId) return;
                auto value = calibration;
                value.insert(QStringLiteral("loading"), false);
                value.insert(QStringLiteral("error"), reason);
                setPortalCalibration(value);
            });
        connect(history, &ShotHistoryStorage::readyChanged, this, &NativeSettingsConnectionsServiceSource::refreshPortalCalibration);
        connect(history, &ShotHistoryStorage::shotSaved, this, &NativeSettingsConnectionsServiceSource::refreshPortalCalibration);
        connect(history, &ShotHistoryStorage::shotDeleted, this, &NativeSettingsConnectionsServiceSource::refreshPortalCalibration);
        connect(history, &ShotHistoryStorage::importedDatabaseChanged, this, &NativeSettingsConnectionsServiceSource::refreshPortalCalibration);
        connect(history, &ShotHistoryStorage::shotMetadataUpdated, this,
                [this](qint64, bool success) { if (success) refreshPortalCalibration(); });
    }
    refreshPortalCalibration();
}

void NativeSettingsConnectionsServiceSource::refreshPortalCalibration()
{
    m_portalCalibrationRequestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    setPortalCalibration({{QStringLiteral("loading"), bool(m_shotHistory)}});
    if (!m_shotHistory || !m_settings) return;
    const auto dye = m_settings->dye();
    const auto mode = m_settings->hardware()->portalMountingMode() == QLatin1String("independent")
        ? PortalTailAnalyzer::MountingMode::Independent : PortalTailAnalyzer::MountingMode::OnScale;
    m_shotHistory->requestPortalCalibration(m_portalCalibrationRequestId, dye->activeBagId(),
        dye->dyeBeanBrand(), dye->dyeBeanType(), mode);
}

void NativeSettingsConnectionsServiceSource::refreshSnapshot()
{
    QVariantMap snapshot;
    const auto settings = m_settings.data();
    const auto ble = m_bleManager.data();
    const auto de1 = m_de1Device.data();
    const auto scale = m_scaleDevice.data();
    const auto portal = m_portalDevice.data();
    const auto firmware = m_hdsFirmwareUpdate.data();

    if (settings) {
        snapshot.insert(QStringLiteral("knownScales"), settings->knownScales());
        snapshot.insert(QStringLiteral("primaryScaleAddress"), settings->primaryScaleAddress());
        snapshot.insert(QStringLiteral("knownScalesRevision"), m_knownScalesRevision);
        snapshot.insert(QStringLiteral("savedRefractometerAddress"), settings->savedRefractometerAddress());
        snapshot.insert(QStringLiteral("savedRefractometerName"), settings->savedRefractometerName());
        snapshot.insert(QStringLiteral("refractometerRevision"), m_refractometerRevision);
    }
    if (ble) {
        snapshot.insert(QStringLiteral("discoveredDevices"), ble->discoveredDevices());
        snapshot.insert(QStringLiteral("discoveredScales"), ble->discoveredScales());
        snapshot.insert(QStringLiteral("discoveredRefractometers"), ble->discoveredRefractometers());
        snapshot.insert(QStringLiteral("scanning"), ble->isScanning());
        snapshot.insert(QStringLiteral("bluetoothAvailable"), ble->isBluetoothAvailable());
        snapshot.insert(QStringLiteral("scaleConnecting"), ble->scaleConnecting());
        snapshot.insert(QStringLiteral("scaleConnectionFailed"), ble->scaleConnectionFailed());
        snapshot.insert(QStringLiteral("refractometerConnected"), ble->isRefractometerConnected());
        snapshot.insert(QStringLiteral("manualWifiInProgress"), ble->isManualWifiConnect());
    }
    if (de1) {
        snapshot.insert(QStringLiteral("de1"), QVariantMap{
            {QStringLiteral("connected"), de1->isConnected()},
            {QStringLiteral("connecting"), de1->isConnecting()},
            {QStringLiteral("simulationMode"), de1->simulationMode()},
            {QStringLiteral("connectionType"), de1->connectionType()},
            {QStringLiteral("firmwareVersion"), de1->firmwareVersion()},
            {QStringLiteral("stateString"), de1->stateString()}});
    }
    QVariantMap usbMap{{QStringLiteral("available"), false},
                       {QStringLiteral("de1Connected"), false},
                       {QStringLiteral("portName"), QString()},
                       {QStringLiteral("serialNumber"), QString()}};
#ifndef Q_OS_IOS
    const auto usb = m_usbManager.data();
    usbMap.insert(QStringLiteral("available"), true);
    if (usb) {
        usbMap.insert(QStringLiteral("de1Connected"), usb->isDe1Connected());
        usbMap.insert(QStringLiteral("portName"), usb->portName());
        usbMap.insert(QStringLiteral("serialNumber"), usb->serialNumber());
    }
#endif
    snapshot.insert(QStringLiteral("usb"), usbMap);
#ifndef Q_OS_IOS
    const auto usbScale = m_usbScaleManager.data();
    snapshot.insert(QStringLiteral("usbScaleConnected"), usbScale && usbScale->isScaleConnected());
#else
    snapshot.insert(QStringLiteral("usbScaleConnected"), false);
#endif
    if (scale) {
        snapshot.insert(QStringLiteral("scale"), QVariantMap{
            {QStringLiteral("connected"), scale->connected()},
            {QStringLiteral("name"), scale->name()},
            {QStringLiteral("isFlowScale"), scale->isFlowScale()},
            {QStringLiteral("isSimulated"), scale->isSimulated()},
            {QStringLiteral("batteryLevel"), scale->batteryLevel()},
            {QStringLiteral("charging"), scale->charging()}});
    }
    if (portal) {
        snapshot.insert(QStringLiteral("portal"), QVariantMap{
            {QStringLiteral("state"), portal->state()},
            {QStringLiteral("active"), portal->active()},
            {QStringLiteral("owned"), portal->owned()},
            {QStringLiteral("machineBusy"), portal->machineBusy()},
            {QStringLiteral("hasReading"), portal->hasReading()},
            {QStringLiteral("ecRaw"), portal->ecRaw()},
            {QStringLiteral("temperatureC"), portal->temperatureC()},
            {QStringLiteral("savedAddress"), portal->savedAddress()},
            {QStringLiteral("savedName"), portal->savedName()},
            {QStringLiteral("devices"), portal->devices()},
            {QStringLiteral("errorMessage"), portal->errorMessage()},
            {QStringLiteral("syncDisplay"), portal->syncDisplay()},
            {QStringLiteral("mountingMode"),
             (settings && settings->hardware()) ? settings->hardware()->portalMountingMode()
                                                : QStringLiteral("onScale")},
            {QStringLiteral("canControlDisplay"), portal->canControlDisplay()},
            {QStringLiteral("displayCommandStatus"), portal->displayCommandStatus()},
            {QStringLiteral("lastPacket"), portal->lastPacket()},
            {QStringLiteral("packetCount"), portal->packetCount()}});
    }
    if (firmware) {
        snapshot.insert(QStringLiteral("hdsFirmware"), QVariantMap{
            {QStringLiteral("checking"), firmware->checking()},
            {QStringLiteral("updateAvailable"), firmware->updateAvailable()},
            {QStringLiteral("installedVersion"), firmware->installedVersion()},
            {QStringLiteral("availableVersion"), firmware->availableVersion()},
            {QStringLiteral("releaseNotes"), firmware->releaseNotes()},
            {QStringLiteral("releaseNotesLoading"), firmware->releaseNotesLoading()},
            {QStringLiteral("updateStarted"), firmware->updateStarted()},
            {QStringLiteral("updateError"), firmware->updateError()}});
    }
    snapshot.insert(QStringLiteral("wifiManualSuggestion"), QVariantMap{
        {QStringLiteral("hostname"), m_manualWifiHostname},
        {QStringLiteral("ip"), m_manualWifiIp},
        {QStringLiteral("probeInProgress"), m_manualWifiProbeInProgress},
        {QStringLiteral("connectInProgress"), ble && ble->isManualWifiConnect()},
        {QStringLiteral("error"), m_manualWifiError}});
    setSnapshot(snapshot);
    setReady(settings && ble && de1 && scale && portal && firmware);
}

bool NativeSettingsConnectionsServiceSource::exactArguments(
    const QVariantMap& arguments, const QStringList& keys) const
{
    if (arguments.size() != keys.size())
        return false;
    for (const QString& key : keys) {
        if (!arguments.contains(key))
            return false;
    }
    return true;
}

QString NativeSettingsConnectionsServiceSource::boundedText(
    const QVariantMap& arguments, const QString& key, int maxLength) const
{
    const QVariant value = arguments.value(key);
    if (value.metaType().id() != QMetaType::QString)
        return {};
    const QString text = value.toString();
    if (text.trimmed().isEmpty() || text.size() > maxLength || text.contains(QChar::Null))
        return {};
    for (const QChar c : text) {
        if (c.category() == QChar::Other_Control)
            return {};
    }
    return text;
}

bool NativeSettingsConnectionsServiceSource::containsAddress(
    const QVariantList& rows, const QString& key, const QString& address) const
{
    for (const QVariant& value : rows) {
        const QVariantMap row = value.toMap();
        if (row.value(key).toString().compare(address, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

void NativeSettingsConnectionsServiceSource::execute(QString requestId,
                                                      QString operation,
                                                      QVariantMap arguments)
{
    const auto started = m_requestIds.begin(requestId);
    if (started != ServiceRequestIds::Start::Accepted) {
        emit operationCompleted(requestId, operation, false, {}, startReason(started));
        return;
    }

    const auto reject = [this, &requestId, &operation](const QString& reason) {
        finish(requestId, operation, false, {}, reason);
    };
    const auto requested = [this, &requestId, &operation](const QVariantMap& result = QVariantMap{}) {
        finish(requestId, operation, true, result, {});
    };
    const auto checkAddressRow = [this](const QVariantList& rows, const QString& key,
                                        const QString& address) {
        return !address.isEmpty() && containsAddress(rows, key, address);
    };

    if (operation == QLatin1String("connectDe1")) {
        const QString address = boundedText(arguments, QStringLiteral("address"));
        if (!exactArguments(arguments, {QStringLiteral("address")}) || !m_bleManager || !m_de1Device
            || address.isEmpty() || !checkAddressRow(m_bleManager->discoveredDevices(), QStringLiteral("address"), address)) {
            reject(QStringLiteral("deviceUnavailable")); return;
        }
        m_de1Device->connectToDevice(address);
        requested({{QStringLiteral("dispatched"), true}}); return;
    }
    if (operation == QLatin1String("connectScale")) {
        const QString address = boundedText(arguments, QStringLiteral("address"));
        if (!exactArguments(arguments, {QStringLiteral("address")}) || !m_bleManager || address.isEmpty()
            || !checkAddressRow(m_bleManager->discoveredScales(), QStringLiteral("address"), address)) {
            reject(QStringLiteral("deviceUnavailable")); return;
        }
        m_bleManager->connectToScale(address);
        requested({{QStringLiteral("dispatched"), true}}); return;
    }
    if (operation == QLatin1String("connectRefractometer")) {
        const QString address = boundedText(arguments, QStringLiteral("address"));
        if (!exactArguments(arguments, {QStringLiteral("address")}) || !m_bleManager || address.isEmpty()
            || !checkAddressRow(m_bleManager->discoveredRefractometers(), QStringLiteral("address"), address)) {
            reject(QStringLiteral("deviceUnavailable")); return;
        }
        m_bleManager->connectToRefractometer(address);
        requested({{QStringLiteral("dispatched"), true}}); return;
    }
    if (operation == QLatin1String("connectPortal")) {
        const QString identifier = boundedText(arguments, QStringLiteral("identifier"));
        if (!exactArguments(arguments, {QStringLiteral("identifier")}) || !m_portalDevice || identifier.isEmpty()
            || !checkAddressRow(m_portalDevice->devices(), QStringLiteral("identifier"), identifier)
            || m_portalDevice->active() || m_portalDevice->machineBusy()) {
            reject(QStringLiteral("deviceUnavailable")); return;
        }
        m_portalDevice->connectDevice(identifier);
        requested({{QStringLiteral("dispatched"), true}}); return;
    }
    if (operation == QLatin1String("reconnectPortal")) {
        if (!arguments.isEmpty() || !m_portalDevice || !m_portalDevice->owned()
            || m_portalDevice->machineBusy() || m_portalDevice->state() == QLatin1String("disconnecting")) {
            reject(QStringLiteral("unavailable")); return;
        }
        const bool dispatched = m_portalDevice->reconnect();
        if (!dispatched) { reject(QStringLiteral("unavailable")); return; }
        requested({{QStringLiteral("dispatched"), true}}); return;
    }
    if (operation == QLatin1String("disconnectPortal")) {
        if (!arguments.isEmpty() || !m_portalDevice || !m_portalDevice->active()
            || m_portalDevice->machineBusy() || m_portalDevice->state() == QLatin1String("disconnecting")) {
            reject(QStringLiteral("unavailable")); return;
        }
        m_portalDevice->disconnectDevice();
        requested({{QStringLiteral("dispatched"), true}}); return;
    }
    if (operation == QLatin1String("forgetPortal")) {
        if (!arguments.isEmpty() || !m_portalDevice || !m_portalDevice->owned()
            || m_portalDevice->machineBusy() || m_portalDevice->state() == QLatin1String("disconnecting")) {
            reject(QStringLiteral("unavailable")); return;
        }
        m_portalDevice->forgetDevice();
        requested({{QStringLiteral("dispatched"), true}}); return;
    }
    if (operation == QLatin1String("setPortalSyncDisplay")) {
        const QVariant enabled = arguments.value(QStringLiteral("enabled"));
        if (!exactArguments(arguments, {QStringLiteral("enabled")})
            || enabled.metaType().id() != QMetaType::Bool || !m_portalDevice || m_portalDevice->machineBusy()) {
            reject(QStringLiteral("invalidArguments")); return;
        }
        m_portalDevice->setSyncDisplay(enabled.toBool());
        requested({{QStringLiteral("dispatched"), true}}); return;
    }
    if (operation == QLatin1String("setPortalMountingMode")) {
        const QString mode = boundedText(arguments, QStringLiteral("mode"), 32);
        if (!exactArguments(arguments, {QStringLiteral("mode")})
            || (mode != QLatin1String("onScale") && mode != QLatin1String("independent"))
            || !m_settings || !m_settings->hardware()) {
            reject(QStringLiteral("invalidArguments")); return;
        }
        m_settings->hardware()->setPortalMountingMode(mode);
        requested({{QStringLiteral("dispatched"), true}}); return;
    }
    if (operation == QLatin1String("setPortalGraphView")) {
        const QVariant show = arguments.value(QStringLiteral("show"));
        if (!exactArguments(arguments, {QStringLiteral("show")})
            || show.metaType().id() != QMetaType::Bool || !m_portalDevice
            || !m_portalDevice->canControlDisplay() || m_portalDevice->machineBusy()) {
            reject(QStringLiteral("unavailable")); return;
        }
        m_portalDevice->setGraphView(show.toBool());
        requested({{QStringLiteral("dispatched"), true}}); return;
    }
    if (operation == QLatin1String("connectManualWifi")) {
        const bool hasProbeId = arguments.contains(QStringLiteral("probeRequestId"));
        bool keysAreKnown = true;
        for (auto it = arguments.cbegin(); it != arguments.cend(); ++it) {
            if (it.key() != QLatin1String("hostname")
                && it.key() != QLatin1String("resolvedIp")
                && it.key() != QLatin1String("probeRequestId")) {
                keysAreKnown = false;
                break;
            }
        }
        if (!m_bleManager || arguments.isEmpty() || arguments.size() > 3 || !keysAreKnown
            || !arguments.contains(QStringLiteral("hostname"))
            || (arguments.contains(QStringLiteral("resolvedIp"))
                && arguments.value(QStringLiteral("resolvedIp")).metaType().id() != QMetaType::QString)
            || (hasProbeId
                && arguments.value(QStringLiteral("probeRequestId")).metaType().id() != QMetaType::QString)) {
            reject(QStringLiteral("invalidArguments")); return;
        }
        const QString hostname = boundedText(arguments, QStringLiteral("hostname"), 253);
        const QString resolvedIp = arguments.contains(QStringLiteral("resolvedIp"))
            ? boundedText(arguments, QStringLiteral("resolvedIp"), 64) : QString();
        QHostAddress ip;
        if (!validHostName(hostname) || (!resolvedIp.isEmpty() && !ip.setAddress(resolvedIp))) {
            reject(QStringLiteral("invalidAddress")); return;
        }
        const QString probeRequestId = hasProbeId
            ? arguments.value(QStringLiteral("probeRequestId")).toString() : QString();
        const bool continueOwnProbe = hasProbeId && m_manualWifiProbeOwner.owns(probeRequestId);
        if (hasProbeId && (probeRequestId.isEmpty() || !continueOwnProbe)) {
            reject(QStringLiteral("staleProbe")); return;
        }
        if (m_bleManager->isManualWifiConnect()
            || (!m_manualWifiProbeOwner.requestId().isEmpty() && !continueOwnProbe)
            || !m_manualWifiConnectOwner.requestId().isEmpty()) {
            reject(QStringLiteral("busy")); return;
        }
        if (continueOwnProbe) {
            if (!ManualWifiRequestOwner::transfer(m_manualWifiProbeOwner, probeRequestId,
                                                   m_manualWifiConnectOwner, requestId)) {
                reject(QStringLiteral("busy")); return;
            }
        } else if (!m_manualWifiConnectOwner.acquire(requestId)) {
            reject(QStringLiteral("busy")); return;
        }
        m_manualWifiError.clear();
        m_startingManualWifiConnect = true;
        m_bleManager->connectToWifiScale(hostname, resolvedIp);
        m_startingManualWifiConnect = false;
        requested({{QStringLiteral("dispatched"), true}}); return;
    }
    if (operation == QLatin1String("selectPrimaryScale")) {
        const QString address = boundedText(arguments, QStringLiteral("address"));
        int expectedRevision = 0;
        if (!exactArguments(arguments, {QStringLiteral("address"), QStringLiteral("expectedRevision")})
            || !ServiceValue::boundedInteger(arguments.value(QStringLiteral("expectedRevision")), 0,
                                               std::numeric_limits<int>::max(), &expectedRevision)
            || !m_settings || !m_bleManager || address.isEmpty()) {
            reject(QStringLiteral("invalidArguments")); return;
        }
        if (expectedRevision != m_knownScalesRevision) { reject(QStringLiteral("staleScaleList")); return; }
        const QVariantList rows = m_settings->knownScales();
        QVariantMap selected;
        for (const QVariant& value : rows) {
            const QVariantMap row = value.toMap();
            if (row.value(QStringLiteral("address")).toString().compare(address, Qt::CaseInsensitive) == 0) {
                selected = row; break;
            }
        }
        if (selected.isEmpty()) { reject(QStringLiteral("staleScaleList")); return; }
        m_settings->setPrimaryScale(address);
        m_bleManager->setSavedScaleAddress(address,
            selected.value(QStringLiteral("type")).toString(), selected.value(QStringLiteral("name")).toString());
        m_bleManager->connectToSavedScale();
        refreshSnapshot();
        requested({{QStringLiteral("dispatched"), true}}); return;
    }
    if (operation == QLatin1String("removeScale")) {
        const QString address = boundedText(arguments, QStringLiteral("address"));
        int expectedRevision = 0;
        if (!exactArguments(arguments, {QStringLiteral("address"), QStringLiteral("expectedRevision")})
            || !ServiceValue::boundedInteger(arguments.value(QStringLiteral("expectedRevision")), 0,
                                               std::numeric_limits<int>::max(), &expectedRevision)
            || !m_settings || !m_bleManager || address.isEmpty()) {
            reject(QStringLiteral("invalidArguments")); return;
        }
        if (expectedRevision != m_knownScalesRevision) { reject(QStringLiteral("staleScaleList")); return; }
        QVariantMap removed;
        for (const QVariant& value : m_settings->knownScales()) {
            const QVariantMap row = value.toMap();
            if (row.value(QStringLiteral("address")).toString().compare(address, Qt::CaseInsensitive) == 0) {
                removed = row; break;
            }
        }
        if (removed.isEmpty()) { reject(QStringLiteral("staleScaleList")); return; }
        const bool wasPrimary = removed.value(QStringLiteral("isPrimary")).toBool();
        if (!wasPrimary) { reject(QStringLiteral("notPrimary")); return; }
        if (wasPrimary) m_bleManager->clearSavedScale();
        m_settings->removeKnownScale(address);
        if (wasPrimary) {
            for (const QVariant& value : m_settings->knownScales()) {
                const QVariantMap row = value.toMap();
                if (!row.value(QStringLiteral("isPrimary")).toBool()) continue;
                m_bleManager->setSavedScaleAddress(row.value(QStringLiteral("address")).toString(),
                    row.value(QStringLiteral("type")).toString(), row.value(QStringLiteral("name")).toString());
                m_bleManager->connectToSavedScale();
                break;
            }
        }
        refreshSnapshot();
        requested({{QStringLiteral("removed"), true}}); return;
    }
    if (operation == QLatin1String("clearRefractometer")) {
        int expectedRevision = 0;
        if (!exactArguments(arguments, {QStringLiteral("expectedRevision")})
            || !ServiceValue::boundedInteger(arguments.value(QStringLiteral("expectedRevision")), 0,
                                               std::numeric_limits<int>::max(), &expectedRevision)
            || !m_settings || !m_bleManager) { reject(QStringLiteral("invalidArguments")); return; }
        if (expectedRevision != m_refractometerRevision) { reject(QStringLiteral("staleRefractometer")); return; }
        m_bleManager->clearSavedRefractometer();
        m_settings->setSavedRefractometerAddress(QString());
        m_settings->setSavedRefractometerName(QString());
        refreshSnapshot();
        requested({{QStringLiteral("cleared"), true}}); return;
    }
    if (operation == QLatin1String("disconnectUsb")) {
#ifndef Q_OS_IOS
        if (!arguments.isEmpty() || !m_usbManager || !m_usbManager->isDe1Connected()) { reject(QStringLiteral("unavailable")); return; }
        m_usbManager->disconnectUsb();
        requested({{QStringLiteral("dispatched"), true}}); return;
#else
        reject(QStringLiteral("unavailable")); return;
#endif
    }
    if (operation == QLatin1String("tareScale")) {
        if (!arguments.isEmpty() || !m_scaleDevice || !m_scaleDevice->connected() || !m_machineState) { reject(QStringLiteral("unavailable")); return; }
        // Through MachineState like every other tare, so weight capture follows it.
        m_machineState->tareScale();
        requested({{QStringLiteral("dispatched"), true}}); return;
    }
    if (operation == QLatin1String("openBluetoothSettings")) {
        if (!arguments.isEmpty() || !m_bleManager) { reject(QStringLiteral("unavailable")); return; }
        m_bleManager->openBluetoothSettings();
        requested({{QStringLiteral("dispatched"), true}}); return;
    }
    if (operation == QLatin1String("probeManualWifi")) {
        if (!arguments.isEmpty() || !m_bleManager) { reject(QStringLiteral("invalidArguments")); return; }
        if (m_manualWifiProbeInProgress || m_bleManager->isManualWifiConnect()
            || !m_manualWifiConnectOwner.requestId().isEmpty()
            || !m_manualWifiProbeOwner.acquire(requestId)) {
            reject(QStringLiteral("busy")); return;
        }
        m_manualWifiProbeInProgress = true;
        m_manualWifiHostname.clear();
        m_manualWifiIp.clear();
        m_bleManager->probeMdnsForManualEntry();
        refreshSnapshot();
        requested({{QStringLiteral("dispatched"), true}}); return;
    }
    if (operation == QLatin1String("loadHdsReleaseNotes")) {
        if (!arguments.isEmpty() || !m_hdsFirmwareUpdate) { reject(QStringLiteral("unavailable")); return; }
        m_hdsFirmwareUpdate->loadReleaseNotes();
        requested({{QStringLiteral("dispatched"), true}}); return;
    }
    if (operation == QLatin1String("startHdsFirmwareUpdate")) {
        if (!arguments.isEmpty() || !m_hdsFirmwareUpdate || !m_scaleDevice
            || !m_hdsFirmwareUpdate->updateAvailable() || !m_scaleDevice->connected()) {
            reject(QStringLiteral("unavailable")); return;
        }
        m_hdsFirmwareUpdate->startUpdate();
        requested({{QStringLiteral("dispatched"), true},
                   {QStringLiteral("confirmedAccepted"), false},
                   {QStringLiteral("progressAvailable"), false}});
        return;
    }
    if (operation == QLatin1String("requestSystemLogDownload")) {
        if (!arguments.isEmpty()) { reject(QStringLiteral("invalidArguments")); return; }
        // ShotServer already exposes a fixed, bounded log ZIP endpoint with an
        // attachment response. Return only that route; never serialize a host
        // filesystem path or let the client choose a URL.
        requested({{QStringLiteral("downloadUrl"), QStringLiteral("/api/debug/file/zip")},
                   {QStringLiteral("contentType"), QStringLiteral("application/zip")},
                   {QStringLiteral("fileName"), QStringLiteral("debug.zip")}});
        return;
    }

    reject(QStringLiteral("unknownOperation"));
}

void NativeSettingsConnectionsServiceSource::finish(
    const QString& requestId, const QString& operation, bool success,
    const QVariantMap& result, const QString& reason)
{
    if (!m_requestIds.finish(requestId))
        return;
    emit operationCompleted(requestId, operation, success, result, reason);
}
