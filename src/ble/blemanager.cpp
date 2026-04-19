#include "blemanager.h"
#include "blecapability.h"
#include "scaledevice.h"
#include "protocol/de1characteristics.h"
#include "scales/scalefactory.h"
#include "refractometers/difluidr2.h"
#include <QBluetoothLocalDevice>
#include <QBluetoothUuid>
#include <QCoreApplication>
#include <QDebug>
#include <QBluetoothPermission>
#include <QLocationPermission>
#include <QStandardPaths>
#include <QDir>
#include <QTextStream>
#include <QDateTime>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

#ifdef Q_OS_IOS
#include <UIKit/UIKit.h>
#endif

#ifdef Q_OS_MACOS
#include <QDesktopServices>
#include <QUrl>
#endif

BLEManager* BLEManager::s_instance = nullptr;

BLEManager::BLEManager(QObject* parent)
    : QObject(parent)
{
    s_instance = this;

    // Discovery agent is created lazily in ensureDiscoveryAgent() to avoid
    // initializing CoreBluetooth (and triggering TCC privacy checks) when
    // BLE is disabled (e.g. simulation mode on Mac debug builds).

    // Track Bluetooth adapter power state.
    // QBluetoothLocalDevice is not available on iOS — CoreBluetooth manages state there.
#ifndef Q_OS_IOS
    m_localDevice = new QBluetoothLocalDevice(this);
    connect(m_localDevice, &QBluetoothLocalDevice::hostModeStateChanged,
            this, &BLEManager::onHostModeStateChanged);
#endif

    // Timer for scale connection timeout (20 seconds)
    m_scaleConnectionTimer = new QTimer(this);
    m_scaleConnectionTimer->setSingleShot(true);
    m_scaleConnectionTimer->setInterval(20000);
    connect(m_scaleConnectionTimer, &QTimer::timeout, this, &BLEManager::onScaleConnectionTimeout);

    // Eagerly run the Linux capability check so any qWarning lands early in
    // startup logs; subsequent calls hit the cached result.
    (void) BleCapability::linuxMissing();
}

bool BLEManager::isBluetoothAvailable() const
{
    if (m_disabled) return true;  // simulator mode — always report available
#ifdef Q_OS_IOS
    return true;  // CoreBluetooth manages state; QBluetoothLocalDevice not available on iOS
#else
#ifdef Q_OS_ANDROID
    // On Android, QBluetoothLocalDevice::hostMode() returns HostPoweredOff until the
    // BLUETOOTH_CONNECT runtime permission has been granted (Android 12+). On a fresh
    // install, permission is Undetermined — if we bail out here, the scan flow never
    // gets a chance to call requestBluetoothPermission(), and the user is stuck with
    // "Bluetooth is powered off" even though the adapter is fine. Report available
    // only while the status is Undetermined so the scan proceeds and prompts the
    // user. If the user has explicitly denied, fall through to the real hostMode()
    // check (which will report PoweredOff) so bluetoothAvailable accurately reflects
    // that BLE is unusable.
    QBluetoothPermission perm;
    perm.setCommunicationModes(QBluetoothPermission::Access);
    if (qApp->checkPermission(perm) == Qt::PermissionStatus::Undetermined) {
        return true;
    }
#endif
    return m_localDevice->hostMode() != QBluetoothLocalDevice::HostPoweredOff;
#endif
}

void BLEManager::onHostModeStateChanged(QBluetoothLocalDevice::HostMode mode)
{
    qDebug() << "BLEManager: Bluetooth host mode changed to" << mode;
    emit bluetoothAvailableChanged();
}

void BLEManager::ensureDiscoveryAgent() {
    if (m_discoveryAgent) return;

    m_discoveryAgent = new QBluetoothDeviceDiscoveryAgent(this);
    m_discoveryAgent->setLowEnergyDiscoveryTimeout(15000);  // 15 seconds per scan cycle

    connect(m_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
            this, &BLEManager::onDeviceDiscovered);
    connect(m_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::finished,
            this, &BLEManager::onScanFinished);
    connect(m_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::errorOccurred,
            this, &BLEManager::onScanError);
}

BLEManager::~BLEManager() {
    if (m_scanning) {
        stopScan();
    }
    if (s_instance == this) s_instance = nullptr;
}

void BLEManager::requestBluezCacheHint()
{
    if (m_disabled) return;  // don't burn the one-shot token in simulator mode
    if (BleCapability::takeBluezCacheHintToken()) {
        emit linuxBlueZCacheHintNeeded();
    }
}

void BLEManager::setDisabled(bool disabled) {
    if (m_disabled != disabled) {
        m_disabled = disabled;
        if (m_disabled) {
            if (m_scanning) {
                stopScan();
            }
            m_scaleConnectionTimer->stop();
            // Disconnect physical scale so FlowScale takes over
            emit disconnectScaleRequested();
        }
        qDebug() << "BLEManager: BLE operations" << (disabled ? "disabled (simulator mode)" : "enabled");
        emit disabledChanged();
    }
}

bool BLEManager::isScanning() const {
    return m_scanning;
}

QVariantList BLEManager::discoveredDevices() const {
    QVariantList result;
    for (const auto& device : m_de1Devices) {
        QVariantMap map;
        map["name"] = device.name();
        map["address"] = getDeviceIdentifier(device);
        result.append(map);
    }
    return result;
}

QVariantList BLEManager::discoveredScales() const {
    QVariantList result;
    for (const auto& pair : m_scales) {
        QVariantMap map;
        map["name"] = pair.first.name();
        map["address"] = getDeviceIdentifier(pair.first);
        map["type"] = pair.second;
        result.append(map);
    }
    return result;
}

QBluetoothDeviceInfo BLEManager::getScaleDeviceInfo(const QString& address) const {
    for (const auto& pair : m_scales) {
        if (deviceIdentifiersMatch(pair.first, address)) {
            return pair.first;
        }
    }
    return QBluetoothDeviceInfo();
}

QString BLEManager::getScaleType(const QString& address) const {
    for (const auto& pair : m_scales) {
        if (deviceIdentifiersMatch(pair.first, address)) {
            return pair.second;
        }
    }
    return QString();
}

void BLEManager::connectToScale(const QString& address) {
    for (const auto& pair : m_scales) {
        if (deviceIdentifiersMatch(pair.first, address)) {
            appendScaleLog(QString("Connecting to %1...").arg(pair.first.name()));
            // If already connected to a different scale, disconnect it first so
            // the scaleDiscovered handler can connect to the new one.
            if (m_scaleDevice && m_scaleDevice->isConnected()
                    && address.compare(m_savedScaleAddress, Qt::CaseInsensitive) != 0) {
                emit disconnectScaleRequested();
            }
            emit scaleDiscovered(pair.first, pair.second);
            return;
        }
    }
    qWarning() << "Scale not found in discovered list:" << address;
}

void BLEManager::startScan() {
    if (m_disabled && !m_scanningForScales) {
        // In simulator mode, suppress DE1 scanning but allow scale/refractometer scans
        // (m_scanningForScales is set by scanForDevices() before calling here).
        qDebug() << "BLEManager: DE1 scan request ignored (simulator mode)";
        return;
    }

    if (m_scanning) {
        return;
    }

    if (!isBluetoothAvailable()) {
        qDebug() << "BLEManager: Scan request ignored (Bluetooth is powered off)";
        return;
    }

    // Check and request Bluetooth permission on Android
    requestBluetoothPermission();
}

void BLEManager::requestBluetoothPermission() {
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    emit de1LogMessage("Checking permissions...");

#ifdef Q_OS_ANDROID
    // First check/request location permission (required for BLE scanning on Android)
    QLocationPermission locationPermission;
    locationPermission.setAccuracy(QLocationPermission::Precise);

    if (qApp->checkPermission(locationPermission) == Qt::PermissionStatus::Undetermined) {
        emit de1LogMessage("Requesting location permission...");
        qApp->requestPermission(locationPermission, this, [this](const QPermission& permission) {
            if (permission.status() == Qt::PermissionStatus::Granted) {
                emit de1LogMessage("Location permission granted");
                requestBluetoothPermission();  // Continue with Bluetooth permission
            } else {
                emit de1LogMessage("Location permission denied");
                emit errorOccurred("Location permission denied - required for Bluetooth scanning");
            }
        });
        return;
    } else if (qApp->checkPermission(locationPermission) == Qt::PermissionStatus::Denied) {
        emit de1LogMessage("Location permission denied");
        emit errorOccurred("Location permission required. Please enable in Settings.");
        return;
    }
#endif

    // Check Bluetooth permission (required on both Android and iOS)
    QBluetoothPermission bluetoothPermission;
    bluetoothPermission.setCommunicationModes(QBluetoothPermission::Access);

    switch (qApp->checkPermission(bluetoothPermission)) {
    case Qt::PermissionStatus::Undetermined:
        emit de1LogMessage("Requesting Bluetooth permission...");
        qApp->requestPermission(bluetoothPermission, this, [this](const QPermission& permission) {
            if (permission.status() == Qt::PermissionStatus::Granted) {
                emit de1LogMessage("Bluetooth permission granted");
                // isBluetoothAvailable() now switches from the Undetermined bypass
                // to the real hostMode() check. Notify QML bindings so any "Bluetooth
                // unavailable" UI re-evaluates immediately.
                emit bluetoothAvailableChanged();
                doStartScan();
            } else {
                emit de1LogMessage("Bluetooth permission denied");
                // Transition Undetermined → Denied also flips isBluetoothAvailable()
                // from true to false (via the hostMode() fall-through).
                emit bluetoothAvailableChanged();
                emit errorOccurred("Bluetooth permission denied");
            }
        });
        return;
    case Qt::PermissionStatus::Denied:
        emit de1LogMessage("Bluetooth permission denied");
        emit errorOccurred("Bluetooth permission required. Please enable in Settings.");
        return;
    case Qt::PermissionStatus::Granted:
        emit de1LogMessage("Permissions OK");
        break;
    }
#endif
    doStartScan();
}

void BLEManager::doStartScan() {
    // Always clear the DE1 device list for a fresh scan
    m_de1Devices.clear();
    emit devicesChanged();
    // Only clear scales/refractometers when the user explicitly asked to scan for them;
    // a DE1-only scan must not wipe the discovered scale list.
    if (m_scanningForScales) {
        m_scales.clear();
        m_refractometerDevices.clear();
        emit scalesChanged();
        emit refractometersChanged();
    }
    m_scanning = true;
    emit scanningChanged();
    emit scanStarted();  // Notify that scan has actually started
    emit de1LogMessage("Scanning for devices...");

    // Scan for BLE devices only
    ensureDiscoveryAgent();
    m_discoveryAgent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
}

void BLEManager::stopScan() {
    if (!m_scanning) return;

    emit de1LogMessage("Scan stopped");
    if (m_discoveryAgent)
        m_discoveryAgent->stop();
    m_scanning = false;
    m_scanningForScales = false;
    m_userInitiatedScaleScan = false;
    emit scanningChanged();
}

void BLEManager::clearDevices() {
    m_de1Devices.clear();
    m_scales.clear();
    m_refractometerDevices.clear();
    emit devicesChanged();
    emit scalesChanged();
    emit refractometersChanged();
}

void BLEManager::onDeviceDiscovered(const QBluetoothDeviceInfo& device) {
    // Check if it's a DE1
    if (isDE1Device(device)) {
        // Avoid duplicates
        for (const auto& existing : m_de1Devices) {
            if (getDeviceIdentifier(existing) == getDeviceIdentifier(device)) {
                return;
            }
        }
        m_de1Devices.append(device);
        emit devicesChanged();
        emit de1LogMessage(QString("Found DE1: %1 (%2)").arg(device.name()).arg(getDeviceIdentifier(device)));
        emit de1Discovered(device);
        return;
    }

    // Only look for scales/refractometers if user requested it or we're looking for saved device
    if (!m_scanningForScales) {
        return;
    }

    // Check if it's a refractometer BEFORE scale detection (prevents R2 misclassification)
    if (DiFluidR2::isR2Device(device.name())) {
        // Avoid duplicates
        for (const auto& existing : m_refractometerDevices) {
            if (getDeviceIdentifier(existing) == getDeviceIdentifier(device)) {
                return;
            }
        }
        m_refractometerDevices.append(device);
        emit refractometersChanged();
        qDebug() << "[BLE] Found refractometer:" << device.name() << "at" << getDeviceIdentifier(device);
        appendScaleLog(QString("Found refractometer: %1 (%2)").arg(device.name(), getDeviceIdentifier(device)));

        // Auto-connect if this is our saved refractometer
        if (!m_savedRefractometerAddress.isEmpty()
            && deviceIdentifiersMatch(device, m_savedRefractometerAddress)) {
            emit refractometerDiscovered(device);
        } else if (m_userInitiatedScaleScan) {
            // User scan — show all devices (emit for UI listing only, not auto-connect)
        }
        return;
    }

    // Check if it's a scale
    QString scaleType = getScaleType(device);
    if (!scaleType.isEmpty()) {
        // Avoid duplicates
        for (const auto& pair : m_scales) {
            if (getDeviceIdentifier(pair.first) == getDeviceIdentifier(device)) {
                return;
            }
        }
        m_scales.append({device, scaleType});
        emit scalesChanged();
        qDebug() << "[BLE] Found scale:" << device.name() << "type:" << scaleType << "at" << getDeviceIdentifier(device);
        appendScaleLog(QString("Found %1: %2 (%3)").arg(scaleType).arg(device.name()).arg(getDeviceIdentifier(device)));

        // If we're doing a direct wake and this is our saved scale found via scan,
        // log it and clear the direct connect state. The scan-discovered device has
        // proper BLE metadata which may help with connection.
        if (m_directConnectInProgress && deviceIdentifiersMatch(device, m_directConnectAddress)) {
            appendScaleLog("Direct wake: found saved scale in scan, using scanned device");
            m_directConnectInProgress = false;
            m_directConnectAddress.clear();
        }

        // During auto-reconnect (not user-initiated scan), only connect to primary scale.
        // This prevents #440: nearby non-primary scales hijacking the connection.
        if (!m_userInitiatedScaleScan && !m_savedScaleAddress.isEmpty()) {
            if (!deviceIdentifiersMatch(device, m_savedScaleAddress)) {
                appendScaleLog(QString("Ignoring non-primary scale: %1 (%2)").arg(device.name(), getDeviceIdentifier(device)));
                return;
            }
        }

        // Emit for user-initiated scan (all scales) or primary match during auto-reconnect
        emit scaleDiscovered(device, scaleType);
    }
}

void BLEManager::onScanFinished() {
    m_scanning = false;
    m_scanningForScales = false;
    m_userInitiatedScaleScan = false;
    emit de1LogMessage("Scan complete");
    appendScaleLog("Scan complete");
    emit scanningChanged();
}

void BLEManager::onScanError(QBluetoothDeviceDiscoveryAgent::Error error) {
    QString errorMsg;
    switch (error) {
        case QBluetoothDeviceDiscoveryAgent::NoError:
            return;  // No error, nothing to do
        case QBluetoothDeviceDiscoveryAgent::PoweredOffError:
            errorMsg = "Bluetooth is powered off";
            break;
        case QBluetoothDeviceDiscoveryAgent::InputOutputError:
            errorMsg = "Bluetooth I/O error";
            break;
        case QBluetoothDeviceDiscoveryAgent::InvalidBluetoothAdapterError:
            errorMsg = "Invalid Bluetooth adapter";
            break;
        case QBluetoothDeviceDiscoveryAgent::UnsupportedPlatformError:
            errorMsg = "Platform does not support Bluetooth LE";
            break;
        case QBluetoothDeviceDiscoveryAgent::UnsupportedDiscoveryMethod:
            errorMsg = "Unsupported discovery method";
            break;
        case QBluetoothDeviceDiscoveryAgent::LocationServiceTurnedOffError:
            errorMsg = "Location services are turned off";
            break;
        case QBluetoothDeviceDiscoveryAgent::MissingPermissionsError:
            errorMsg = "Bluetooth permission denied. Please allow Bluetooth access in Settings.";
            break;
        default:
            errorMsg = QString("Bluetooth error (code %1)").arg(static_cast<int>(error));
            break;
    }
    qWarning() << "BLEManager scan error:" << errorMsg << "code:" << static_cast<int>(error);
    emit de1LogMessage(QString("Error: %1").arg(errorMsg));
    appendScaleLog(QString("Error: %1").arg(errorMsg));
    emit errorOccurred(errorMsg);
    m_scanning = false;
    m_scanningForScales = false;
    m_userInitiatedScaleScan = false;
    emit scanningChanged();
}

bool BLEManager::isDE1Device(const QBluetoothDeviceInfo& device) const {
    // Check by name - "DE1" is the standard prefix, "BENGLE" is used for developer/debug units
    QString name = device.name();
    if (name.startsWith("DE1", Qt::CaseInsensitive) ||
        name.startsWith("BENGLE", Qt::CaseInsensitive)) {
        return true;
    }

    // Check by service UUID
    QList<QBluetoothUuid> uuids = device.serviceUuids();
    for (const auto& uuid : uuids) {
        if (uuid == DE1::SERVICE_UUID) {
            return true;
        }
    }

    return false;
}

QString BLEManager::getScaleType(const QBluetoothDeviceInfo& device) const {
    ScaleType type = ScaleFactory::detectScaleType(device);
    if (type == ScaleType::Unknown) {
        return "";
    }
    return ScaleFactory::scaleTypeName(type);
}

void BLEManager::setScaleDevice(ScaleDevice* scale) {
    if (m_scaleDevice) {
        disconnect(m_scaleDevice, nullptr, this, nullptr);
    }

    m_scaleDevice = scale;

    if (m_scaleDevice) {
        connect(m_scaleDevice, &ScaleDevice::connectedChanged,
                this, &BLEManager::onScaleConnectedChanged);
        // Connect scale's debug log to our logging system
        connect(m_scaleDevice, &ScaleDevice::logMessage,
                this, &BLEManager::appendScaleLog);
    }
}

void BLEManager::onScaleConnectedChanged() {
    if (m_scaleDevice && m_scaleDevice->isConnected()) {
        // Scale connected - stop timers, clear failure flag, clear direct connect state
        m_scaleConnectionTimer->stop();
        m_directConnectInProgress = false;
        m_directConnectAddress.clear();
        m_flowScaleFallbackEmitted = false;  // Allow dialog again if scale disconnects and reconnect fails
        if (m_scaleConnectionFailed) {
            m_scaleConnectionFailed = false;
            emit scaleConnectionFailedChanged();
        }
        qDebug() << "BLEManager: Scale connected";
    } else {
        // Scale disconnected - notify UI immediately
        qDebug() << "BLEManager: Scale disconnected";
        appendScaleLog("Scale disconnected");
        emit scaleDisconnected();
    }
}

void BLEManager::onScaleConnectionTimeout() {
    // Clear direct connect state on timeout
    m_directConnectInProgress = false;
    m_directConnectAddress.clear();

    if (!m_scaleDevice || !m_scaleDevice->isConnected()) {
        qWarning() << "BLEManager: Scale connection timeout - not found";
        m_scaleConnectionFailed = true;
        emit scaleConnectionFailedChanged();

        if (!m_flowScaleFallbackEmitted) {
            m_flowScaleFallbackEmitted = true;
            appendScaleLog("Scale not found - using FlowScale");
            emit flowScaleFallback();
        }
    }
}

void BLEManager::setSavedScaleAddress(const QString& address, const QString& type, const QString& name) {
    m_savedScaleAddress = address;
    m_savedScaleType = type;
    m_savedScaleName = name;
}

void BLEManager::resetScaleConnectionState() {
    // Only reset direct-connect state so a fresh attempt can proceed.
    // Do NOT reset m_scaleConnectionFailed or m_flowScaleFallbackEmitted here:
    // - m_scaleConnectionFailed: keeps UI showing "Not found" during retries (no flicker)
    // - m_flowScaleFallbackEmitted: prevents re-showing FlowScale dialog on each retry
    // Both are reset when the scale actually connects (onScaleConnectedChanged).
    m_directConnectInProgress = false;
    m_directConnectAddress.clear();
    m_scaleConnectionTimer->stop();
}

void BLEManager::clearSavedScale() {
    m_savedScaleAddress.clear();
    m_savedScaleType.clear();
    m_savedScaleName.clear();
    m_scaleConnectionFailed = false;
    m_scaleConnectionTimer->stop();
    m_flowScaleFallbackEmitted = false;
    emit scaleConnectionFailedChanged();
    // Stop any pending auto-reconnect timer in main.cpp
    emit disconnectScaleRequested();
}

// === Refractometer support ===

QVariantList BLEManager::discoveredRefractometers() const {
    QVariantList result;
    for (const auto& device : m_refractometerDevices) {
        QVariantMap map;
        map["name"] = device.name();
        map["address"] = getDeviceIdentifier(device);
        map["type"] = QStringLiteral("DiFluid R2");
        result.append(map);
    }
    return result;
}

bool BLEManager::isRefractometerConnected() const {
    return m_refractometerDevice && m_refractometerDevice->isConnected();
}

QBluetoothDeviceInfo BLEManager::getRefractometerDeviceInfo(const QString& address) const {
    for (const auto& device : m_refractometerDevices) {
        if (deviceIdentifiersMatch(device, address)) {
            return device;
        }
    }
    return QBluetoothDeviceInfo();
}

void BLEManager::connectToRefractometer(const QString& address) {
    QBluetoothDeviceInfo info = getRefractometerDeviceInfo(address);
    if (info.isValid()) {
        appendScaleLog(QString("Connecting to refractometer: %1 (%2)").arg(info.name(), address));
        emit refractometerDiscovered(info);
    }
}

void BLEManager::setSavedRefractometerAddress(const QString& address, const QString& name) {
    m_savedRefractometerAddress = address;
    m_savedRefractometerName = name;
}

void BLEManager::clearSavedRefractometer() {
    m_savedRefractometerAddress.clear();
    m_savedRefractometerName.clear();
    m_refractometerDevice = nullptr;
    emit refractometerConnectedChanged();
    emit disconnectRefractometerRequested();
}

void BLEManager::setRefractometerDevice(DiFluidR2* device) {
    if (m_refractometerDevice) {
        disconnect(m_refractometerDevice, nullptr, this, nullptr);
    }
    m_refractometerDevice = device;
    if (m_refractometerDevice) {
        connect(m_refractometerDevice, &DiFluidR2::connectedChanged,
                this, &BLEManager::refractometerConnectedChanged);
    }
    emit refractometerConnectedChanged();
}

void BLEManager::tryDirectConnectToRefractometer() {
    if (m_savedRefractometerAddress.isEmpty() || m_disabled) return;
    // Piggyback on the scale scan infrastructure — set the flag so
    // onDeviceDiscovered processes refractometer advertisements
    if (!m_scanningForScales) {
        m_scanningForScales = true;
        startScan();
    }
}

void BLEManager::setSavedDE1Address(const QString& address, const QString& name) {
    m_savedDE1Address = address;
    m_savedDE1Name = name;
}

void BLEManager::clearSavedDE1() {
    m_savedDE1Address.clear();
    m_savedDE1Name.clear();
}

void BLEManager::tryDirectConnectToDE1() {
    if (m_disabled) {
        qDebug() << "BLEManager: tryDirectConnectToDE1 - disabled (simulator mode)";
        return;
    }

    if (m_savedDE1Address.isEmpty()) {
        qDebug() << "BLEManager: tryDirectConnectToDE1 - no saved DE1 address";
        return;
    }

    if (!isBluetoothAvailable()) {
        qDebug() << "BLEManager: tryDirectConnectToDE1 - Bluetooth is powered off, skipping";
        return;
    }

    // Don't attempt if already connected or connecting
    // (the de1Discovered handler in main.cpp checks this before connecting)

    QString deviceName = m_savedDE1Name.isEmpty() ? "DE1" : m_savedDE1Name;

#ifdef Q_OS_IOS
    // On iOS, we have a UUID, not a MAC address.
    // Direct connect with just a UUID rarely works - scan and match by UUID.
    qDebug() << "BLEManager: DE1 direct wake (iOS) - scanning for" << deviceName << "UUID:" << m_savedDE1Address;
    emit de1LogMessage(QString("Direct wake (iOS): scanning for %1").arg(deviceName));

    if (!m_scanning) {
        startScan();
    }
#else
    // On Android/desktop, we have a MAC address - try direct connect
    QString upperAddress = m_savedDE1Address.toUpper();
    QBluetoothAddress address(upperAddress);
    if (address.isNull()) {
        qWarning() << "BLEManager: tryDirectConnectToDE1 - invalid saved address:" << m_savedDE1Address;
        emit de1LogMessage(QString("Direct wake failed: invalid saved address"));
        if (!m_scanning) startScan();
        return;
    }
    QBluetoothDeviceInfo deviceInfo(address, deviceName, QBluetoothDeviceInfo::LowEnergyCoreConfiguration);

    qDebug() << "BLEManager: DE1 direct wake - connecting to" << deviceName << "at" << upperAddress;
    emit de1LogMessage(QString("Direct wake: connecting to %1 at %2").arg(deviceName, upperAddress));

    // Emit de1Discovered so main.cpp's handler connects to the device
    emit de1Discovered(deviceInfo);

    // Also start scanning in parallel - if the DE1 is advertising, we'll find it
    if (!m_scanning) {
        startScan();
    }
#endif
}

void BLEManager::scanForDevices() {
    // Note: m_disabled is intentionally not checked here — scale and refractometer
    // scanning is allowed in simulator mode so real hardware can be tested against
    // a simulated DE1. Only DE1 BLE (startScan without m_scanningForScales) is suppressed.
    appendScaleLog("Starting device scan...");
    m_scaleConnectionFailed = false;
    m_flowScaleFallbackEmitted = false;  // User-initiated scan resets the dialog guard
    emit scaleConnectionFailedChanged();

    if (!isBluetoothAvailable()) {
        qDebug() << "BLEManager: scanForDevices - Bluetooth is powered off, skipping";
        return;
    }

    // If already scanning, we need to restart to include scales
    if (m_scanning) {
        stopScan();
    }

    // Set flags AFTER stopScan (which clears them)
    m_scanningForScales = true;
    m_userInitiatedScaleScan = true;
    startScan();
}

void BLEManager::tryDirectConnectToScale() {
    if (m_disabled) {
        qDebug() << "BLEManager: tryDirectConnectToScale - disabled (simulator mode)";
        return;
    }

    if (m_savedScaleAddress.isEmpty() || m_savedScaleType.isEmpty()) {
        qDebug() << "BLEManager: tryDirectConnectToScale - no saved scale address/type";
        return;
    }

    if (!isBluetoothAvailable()) {
        qDebug() << "BLEManager: tryDirectConnectToScale - Bluetooth is powered off, skipping";
        return;
    }

    if (m_scaleDevice && m_scaleDevice->isConnected()) {
        qDebug() << "BLEManager: tryDirectConnectToScale - scale already connected";
        return;
    }

    // Direct wake strategy:
    // 1. Try direct connection to saved address (may wake sleeping scales that respond to connect requests)
    // 2. Also start scanning in parallel (finds scales that are actively advertising)
    // 3. Whichever succeeds first wins - we don't skip scan results even if direct connect is in progress
    //
    // de1app does both: ble connect + scanning, and calls ble_connect_to_scale again when
    // the device appears in scan results (bluetooth.tcl lines 2032 and 2252-2256)

    QString deviceName = m_savedScaleName.isEmpty() ? m_savedScaleType : m_savedScaleName;

#ifdef Q_OS_IOS
    // On iOS, we have a UUID, not a MAC address
    // Direct connect with just a UUID rarely works - we need to find the device via scanning
    // Just start scanning and match by UUID when found
    qDebug() << "BLEManager: Direct wake (iOS) - scanning for" << deviceName << "UUID:" << m_savedScaleAddress;
    appendScaleLog(QString("Direct wake (iOS): scanning for %1").arg(deviceName));

    m_directConnectInProgress = true;
    m_directConnectAddress = m_savedScaleAddress;  // UUID on iOS

    // Start timeout timer
    m_scaleConnectionTimer->start();

    // On iOS, we skip the direct connect attempt and rely on scanning
    m_scanningForScales = true;
    if (!m_scanning) {
        startScan();
    }
#else
    // On Android/desktop, we have a MAC address - try direct connect
    QString upperAddress = m_savedScaleAddress.toUpper();
    QBluetoothAddress address(upperAddress);
    QBluetoothDeviceInfo deviceInfo(address, deviceName, QBluetoothDeviceInfo::LowEnergyCoreConfiguration);

    qDebug() << "BLEManager: Direct wake - connecting to" << deviceName << "at" << upperAddress;
    appendScaleLog(QString("Direct wake: connecting to %1 at %2").arg(deviceName, m_savedScaleAddress));

    // Mark that we're doing a direct connect - but we won't skip scan results
    // Instead, onDeviceDiscovered will check if scale is already connected
    m_directConnectInProgress = true;
    m_directConnectAddress = upperAddress;

    // Start timeout timer
    m_scaleConnectionTimer->start();

    // Try direct connection - this may wake the scale
    emit scaleDiscovered(deviceInfo, m_savedScaleType);

    // Also start scanning in parallel - if the scale is advertising, we'll find it
    // and connect via the scan path (which has a real QBluetoothDeviceInfo)
    m_scanningForScales = true;
    if (!m_scanning) {
        startScan();
    }
#endif
}

void BLEManager::openLocationSettings()
{
#ifdef Q_OS_ANDROID
    QJniObject action = QJniObject::fromString("android.settings.LOCATION_SOURCE_SETTINGS");
    QJniObject intent("android/content/Intent", "(Ljava/lang/String;)V", action.object<jstring>());
    intent.callMethod<QJniObject>("addFlags", "(I)Landroid/content/Intent;", 0x10000000);  // FLAG_ACTIVITY_NEW_TASK

    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (activity.isValid() && intent.isValid()) {
        activity.callMethod<void>("startActivity", "(Landroid/content/Intent;)V", intent.object());
    }
#else
    qDebug() << "openLocationSettings is only available on Android";
#endif
}

void BLEManager::openBluetoothSettings()
{
#ifdef Q_OS_ANDROID
    QJniObject action = QJniObject::fromString("android.settings.BLUETOOTH_SETTINGS");
    QJniObject intent("android/content/Intent", "(Ljava/lang/String;)V", action.object<jstring>());
    intent.callMethod<QJniObject>("addFlags", "(I)Landroid/content/Intent;", 0x10000000);  // FLAG_ACTIVITY_NEW_TASK

    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (activity.isValid() && intent.isValid()) {
        activity.callMethod<void>("startActivity", "(Landroid/content/Intent;)V", intent.object());
    }
#elif defined(Q_OS_IOS)
    // iOS: Open the app's Settings page. iOS doesn't allow deep-linking directly to
    // the Bluetooth settings screen, but UIApplicationOpenSettingsURLString takes the
    // user to Settings > Decenza where they can see Bluetooth permission status.
    NSURL* url = [NSURL URLWithString:UIApplicationOpenSettingsURLString];
    if (url && [[UIApplication sharedApplication] canOpenURL:url]) {
        [[UIApplication sharedApplication] openURL:url options:@{} completionHandler:nil];
    }
#elif defined(Q_OS_MACOS)
    // macOS: Open System Settings to Bluetooth privacy pane
    QDesktopServices::openUrl(QUrl("x-apple.systempreferences:com.apple.preference.security?Privacy_Bluetooth"));
#else
    qDebug() << "openBluetoothSettings is not implemented for this platform";
#endif
}

// Scale debug logging methods
void BLEManager::appendScaleLog(const QString& message) {
    QString timestampedMsg = QDateTime::currentDateTime().toString("[hh:mm:ss.zzz] ") + message;
    m_scaleLogMessages.append(timestampedMsg);
    emit scaleLogMessage(message);

    // Keep log size reasonable (last 1000 messages)
    while (m_scaleLogMessages.size() > 1000) {
        m_scaleLogMessages.removeFirst();
    }
}

void BLEManager::clearScaleLog() {
    m_scaleLogMessages.clear();
    emit scaleLogMessage("Log cleared");
}

QString BLEManager::getScaleLogPath() const {
    return m_scaleLogFilePath;
}

void BLEManager::writeScaleLogToFile() {
    // Get app's cache directory for the log file
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(cacheDir);
    m_scaleLogFilePath = cacheDir + "/scale_debug_log.txt";

    QFile file(m_scaleLogFilePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << "=== Decenza Scale Debug Log ===" << Qt::endl;
        out << "Generated: " << QDateTime::currentDateTime().toString(Qt::ISODate) << Qt::endl;
        out << "================================" << Qt::endl << Qt::endl;

        for (const QString& msg : m_scaleLogMessages) {
            out << msg << Qt::endl;
        }
        file.close();
        qDebug() << "Scale log written to:" << m_scaleLogFilePath;
    } else {
        qWarning() << "Failed to write scale log to:" << m_scaleLogFilePath;
    }
}

void BLEManager::shareScaleLog() {
    // First write the log to a file
    writeScaleLogToFile();

    if (m_scaleLogFilePath.isEmpty()) {
        qWarning() << "No log file path available";
        return;
    }

#ifdef Q_OS_ANDROID
    // Use Android's share intent
    QJniObject context = QNativeInterface::QAndroidApplication::context();

    // Create a file URI using FileProvider for Android 7+
    QJniObject fileObj = QJniObject::fromString(m_scaleLogFilePath);
    QJniObject file("java/io/File", "(Ljava/lang/String;)V", fileObj.object<jstring>());

    // Get the app's package name for FileProvider authority
    QJniObject packageName = context.callObjectMethod("getPackageName", "()Ljava/lang/String;");
    QString authority = packageName.toString() + ".fileprovider";
    QJniObject authorityObj = QJniObject::fromString(authority);

    // Get content URI via FileProvider
    QJniObject uri = QJniObject::callStaticObjectMethod(
        "androidx/core/content/FileProvider",
        "getUriForFile",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/io/File;)Landroid/net/Uri;",
        context.object(),
        authorityObj.object<jstring>(),
        file.object());

    if (!uri.isValid()) {
        qWarning() << "Failed to get content URI for file";
        // Fallback: just notify user of file location
        emit scaleLogMessage("Log saved to: " + m_scaleLogFilePath);
        return;
    }

    // Create share intent
    QJniObject actionSend = QJniObject::fromString("android.intent.action.SEND");
    QJniObject intent("android/content/Intent", "(Ljava/lang/String;)V", actionSend.object<jstring>());

    QJniObject mimeType = QJniObject::fromString("text/plain");
    intent.callObjectMethod("setType", "(Ljava/lang/String;)Landroid/content/Intent;", mimeType.object<jstring>());

    QJniObject extraStream = QJniObject::getStaticObjectField<jstring>("android/content/Intent", "EXTRA_STREAM");
    intent.callObjectMethod("putExtra", "(Ljava/lang/String;Landroid/os/Parcelable;)Landroid/content/Intent;",
                           extraStream.object<jstring>(), uri.object());

    // Add grant read permission flag
    intent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", 1);  // FLAG_GRANT_READ_URI_PERMISSION

    // Create chooser
    QJniObject chooserTitle = QJniObject::fromString("Share Scale Debug Log");
    QJniObject chooser = QJniObject::callStaticObjectMethod(
        "android/content/Intent",
        "createChooser",
        "(Landroid/content/Intent;Ljava/lang/CharSequence;)Landroid/content/Intent;",
        intent.object(),
        chooserTitle.object<jstring>());

    chooser.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", 0x10000000);  // FLAG_ACTIVITY_NEW_TASK

    // Start the chooser activity
    context.callMethod<void>("startActivity", "(Landroid/content/Intent;)V", chooser.object());

    emit scaleLogMessage("Opening share dialog...");

#elif defined(Q_OS_IOS)
    // iOS: Use UIActivityViewController for sharing
    NSString* filePath = m_scaleLogFilePath.toNSString();
    NSURL* fileURL = [NSURL fileURLWithPath:filePath];

    if (![[NSFileManager defaultManager] fileExistsAtPath:filePath]) {
        qWarning() << "Log file does not exist:" << m_scaleLogFilePath;
        emit scaleLogMessage("Error: Log file not found");
        return;
    }

    // Create activity view controller with the file URL
    NSArray* activityItems = @[fileURL];
    UIActivityViewController* activityVC = [[UIActivityViewController alloc]
        initWithActivityItems:activityItems
        applicationActivities:nil];

    // Get the root view controller to present from
    UIWindow* keyWindow = nil;
    for (UIScene* scene in [UIApplication sharedApplication].connectedScenes) {
        if ([scene isKindOfClass:[UIWindowScene class]]) {
            UIWindowScene* windowScene = (UIWindowScene*)scene;
            for (UIWindow* window in windowScene.windows) {
                if (window.isKeyWindow) {
                    keyWindow = window;
                    break;
                }
            }
        }
        if (keyWindow) break;
    }

    UIViewController* rootVC = keyWindow.rootViewController;
    if (rootVC) {
        // For iPad, we need to set the popover presentation
        if (UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad) {
            activityVC.popoverPresentationController.sourceView = rootVC.view;
            activityVC.popoverPresentationController.sourceRect = CGRectMake(
                rootVC.view.bounds.size.width / 2,
                rootVC.view.bounds.size.height / 2,
                0, 0);
        }

        [rootVC presentViewController:activityVC animated:YES completion:nil];
        emit scaleLogMessage("Opening share dialog...");
    } else {
        qWarning() << "Could not find root view controller for sharing";
        emit scaleLogMessage("Error: Could not open share dialog");
    }

#else
    // Desktop: just show the file path
    emit scaleLogMessage("Log saved to: " + m_scaleLogFilePath);
    qDebug() << "Scale log saved to:" << m_scaleLogFilePath;
#endif
}
