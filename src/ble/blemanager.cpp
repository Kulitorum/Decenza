#include "blemanager.h"
#include "scaledevice.h"
#include "protocol/de1characteristics.h"
#include "scales/scalefactory.h"
#include <QBluetoothUuid>
#include <QCoreApplication>
#include <QDebug>
#include <QLocationPermission>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

BLEManager::BLEManager(QObject* parent)
    : QObject(parent)
{
    m_discoveryAgent = new QBluetoothDeviceDiscoveryAgent(this);
    m_discoveryAgent->setLowEnergyDiscoveryTimeout(15000);  // 15 seconds per scan cycle

    connect(m_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
            this, &BLEManager::onDeviceDiscovered);
    connect(m_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::finished,
            this, &BLEManager::onScanFinished);
    connect(m_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::errorOccurred,
            this, &BLEManager::onScanError);

    // Timer for scale connection timeout (20 seconds)
    m_scaleConnectionTimer = new QTimer(this);
    m_scaleConnectionTimer->setSingleShot(true);
    m_scaleConnectionTimer->setInterval(20000);
    connect(m_scaleConnectionTimer, &QTimer::timeout, this, &BLEManager::onScaleConnectionTimeout);
}

BLEManager::~BLEManager() {
    if (m_scanning) {
        stopScan();
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
        map["address"] = device.address().toString();
        result.append(map);
    }
    return result;
}

QVariantList BLEManager::discoveredScales() const {
    QVariantList result;
    for (const auto& pair : m_scales) {
        QVariantMap map;
        map["name"] = pair.first.name();
        map["address"] = pair.first.address().toString();
        map["type"] = pair.second;
        result.append(map);
    }
    return result;
}

QBluetoothDeviceInfo BLEManager::getScaleDeviceInfo(const QString& address) const {
    QBluetoothAddress addr(address);
    for (const auto& pair : m_scales) {
        if (pair.first.address() == addr) {
            return pair.first;
        }
    }
    return QBluetoothDeviceInfo();
}

QString BLEManager::getScaleType(const QString& address) const {
    QBluetoothAddress addr(address);
    for (const auto& pair : m_scales) {
        if (pair.first.address() == addr) {
            return pair.second;
        }
    }
    return QString();
}

void BLEManager::startScan() {
    if (m_scanning) return;

    // Check and request Bluetooth permission on Android
    requestBluetoothPermission();
}

void BLEManager::requestBluetoothPermission() {
#ifdef Q_OS_ANDROID
    emit de1LogMessage("Checking permissions...");

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

    // Now check Bluetooth permission
    QBluetoothPermission bluetoothPermission;
    bluetoothPermission.setCommunicationModes(QBluetoothPermission::Access);

    switch (qApp->checkPermission(bluetoothPermission)) {
    case Qt::PermissionStatus::Undetermined:
        emit de1LogMessage("Requesting Bluetooth permission...");
        qApp->requestPermission(bluetoothPermission, this, [this](const QPermission& permission) {
            if (permission.status() == Qt::PermissionStatus::Granted) {
                emit de1LogMessage("Bluetooth permission granted");
                doStartScan();
            } else {
                emit de1LogMessage("Bluetooth permission denied");
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
    clearDevices();
    m_scanning = true;
    emit scanningChanged();
    emit de1LogMessage("Scanning for devices...");

    // Scan for BLE devices only
    m_discoveryAgent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
}

void BLEManager::stopScan() {
    if (!m_scanning) return;

    emit de1LogMessage("Scan stopped");
    m_discoveryAgent->stop();
    m_scanning = false;
    m_scanningForScales = false;
    emit scanningChanged();
}

void BLEManager::clearDevices() {
    m_de1Devices.clear();
    m_scales.clear();
    emit devicesChanged();
    emit scalesChanged();
}

void BLEManager::onDeviceDiscovered(const QBluetoothDeviceInfo& device) {
    // Check if it's a DE1
    if (isDE1Device(device)) {
        // Avoid duplicates
        for (const auto& existing : m_de1Devices) {
            if (existing.address() == device.address()) {
                return;
            }
        }
        m_de1Devices.append(device);
        emit devicesChanged();
        emit de1LogMessage(QString("Found DE1: %1 (%2)").arg(device.name()).arg(device.address().toString()));
        emit de1Discovered(device);
        return;
    }

    // Only look for scales if user requested it or we're looking for saved scale
    if (!m_scanningForScales) {
        return;
    }

    // Check if it's a scale
    QString scaleType = getScaleType(device);
    if (!scaleType.isEmpty()) {
        // Avoid duplicates
        for (const auto& pair : m_scales) {
            if (pair.first.address() == device.address()) {
                return;
            }
        }
        m_scales.append({device, scaleType});
        emit scalesChanged();
        emit scaleLogMessage(QString("Found %1: %2 (%3)").arg(scaleType).arg(device.name()).arg(device.address().toString()));
        emit scaleDiscovered(device, scaleType);
    }
}

void BLEManager::onScanFinished() {
    m_scanning = false;
    m_scanningForScales = false;
    emit de1LogMessage("Scan complete");
    emit scaleLogMessage("Scan complete");
    emit scanningChanged();
}

void BLEManager::onScanError(QBluetoothDeviceDiscoveryAgent::Error error) {
    QString errorMsg;
    switch (error) {
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
        default:
            errorMsg = "Unknown Bluetooth error";
            break;
    }
    emit de1LogMessage(QString("Error: %1").arg(errorMsg));
    emit scaleLogMessage(QString("Error: %1").arg(errorMsg));
    emit errorOccurred(errorMsg);
    m_scanning = false;
    m_scanningForScales = false;
    emit scanningChanged();
}

bool BLEManager::isDE1Device(const QBluetoothDeviceInfo& device) const {
    // Check by name
    QString name = device.name();
    if (name.startsWith("DE1", Qt::CaseInsensitive)) {
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
    }
}

void BLEManager::onScaleConnectedChanged() {
    if (m_scaleDevice && m_scaleDevice->isConnected()) {
        // Scale connected - stop timeout timer, clear failure flag
        qDebug() << "Scale connected";
        m_scaleConnectionTimer->stop();
        if (m_scaleConnectionFailed) {
            m_scaleConnectionFailed = false;
            emit scaleConnectionFailedChanged();
        }
    }
}

void BLEManager::onScaleConnectionTimeout() {
    if (!m_scaleDevice || !m_scaleDevice->isConnected()) {
        qDebug() << "Scale connection timeout - scale not responding";
        m_scaleConnectionFailed = true;
        emit scaleConnectionFailedChanged();
    }
}

void BLEManager::setSavedScaleAddress(const QString& address, const QString& type) {
    m_savedScaleAddress = address;
    m_savedScaleType = type;
    qDebug() << "Saved scale address:" << address << "type:" << type;
}

void BLEManager::clearSavedScale() {
    m_savedScaleAddress.clear();
    m_savedScaleType.clear();
    m_scaleConnectionFailed = false;
    emit scaleConnectionFailedChanged();
    qDebug() << "Cleared saved scale";
}

void BLEManager::scanForScales() {
    qDebug() << "User requested scale scan";
    emit scaleLogMessage("Starting scale scan...");
    m_scaleConnectionFailed = false;
    emit scaleConnectionFailedChanged();

    // If already scanning, we need to restart to include scales
    if (m_scanning) {
        stopScan();
    }

    // Set flag AFTER stopScan (which clears it)
    m_scanningForScales = true;
    startScan();
}

void BLEManager::tryDirectConnectToScale() {
    if (m_savedScaleAddress.isEmpty() || m_savedScaleType.isEmpty()) {
        qDebug() << "No saved scale address, cannot try direct connect";
        return;
    }

    if (m_scaleDevice && m_scaleDevice->isConnected()) {
        qDebug() << "Scale already connected";
        return;
    }

    qDebug() << "Trying direct connect to wake scale:" << m_savedScaleAddress << "type:" << m_savedScaleType;

    // Create a QBluetoothDeviceInfo from just the address - this allows us to
    // connect directly to a sleeping scale without scanning. The BLE connection
    // request itself will wake the scale (this is how the official de1app works).
    QBluetoothAddress address(m_savedScaleAddress);
    QBluetoothDeviceInfo deviceInfo(address, m_savedScaleType, QBluetoothDeviceInfo::LowEnergyCoreConfiguration);

    // Start timeout timer
    m_scaleConnectionTimer->start();

    // Emit scaleDiscovered to trigger connection via main.cpp handler
    // The direct BLE connect request will wake the sleeping scale
    emit scaleDiscovered(deviceInfo, m_savedScaleType);

    // Also start scanning as fallback - if direct connect fails, we might find
    // the scale through regular discovery (e.g., if it woke up from button press)
    m_scanningForScales = true;
    if (!m_scanning) {
        startScan();
    }
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
