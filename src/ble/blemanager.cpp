#include "blemanager.h"
#include "protocol/de1characteristics.h"
#include <QBluetoothUuid>

BLEManager::BLEManager(QObject* parent)
    : QObject(parent)
{
    m_discoveryAgent = new QBluetoothDeviceDiscoveryAgent(this);
    m_discoveryAgent->setLowEnergyDiscoveryTimeout(30000);  // 30 seconds

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

void BLEManager::startScan() {
    if (m_scanning) return;

    clearDevices();
    m_scanning = true;
    emit scanningChanged();

    // Scan for BLE devices only
    m_discoveryAgent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
}

void BLEManager::stopScan() {
    if (!m_scanning) return;

    m_discoveryAgent->stop();
    m_scanning = false;
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
        emit de1Discovered(device);
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
        emit scaleDiscovered(device, scaleType);
    }
}

void BLEManager::onScanFinished() {
    m_scanning = false;
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
    emit errorOccurred(errorMsg);
    m_scanning = false;
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
    QString name = device.name();
    QList<QBluetoothUuid> uuids = device.serviceUuids();

    // Check by name patterns
    if (name.startsWith("Decent Scale", Qt::CaseInsensitive) ||
        name.startsWith("DE-", Qt::CaseInsensitive)) {
        return "decent";
    }

    if (name.startsWith("ACAIA", Qt::CaseInsensitive) ||
        name.startsWith("PYXIS", Qt::CaseInsensitive) ||
        name.startsWith("PEARL", Qt::CaseInsensitive)) {
        return "acaia";
    }

    if (name.contains("FELICITA", Qt::CaseInsensitive)) {
        return "felicita";
    }

    if (name.contains("BOOKOO", Qt::CaseInsensitive)) {
        return "bookoo";
    }

    if (name.contains("EUREKA", Qt::CaseInsensitive) ||
        name.contains("PRECISA", Qt::CaseInsensitive)) {
        return "eureka";
    }

    if (name.contains("JIMMY", Qt::CaseInsensitive) ||
        name.contains("HIROIA", Qt::CaseInsensitive)) {
        return "hiroia";
    }

    if (name.contains("DIFLUID", Qt::CaseInsensitive)) {
        return "difluid";
    }

    // Check by service UUIDs
    for (const auto& uuid : uuids) {
        if (uuid == Scale::Decent::SERVICE) {
            return "decent";
        }
        if (uuid == Scale::Acaia::SERVICE) {
            return "acaia";
        }
        if (uuid == Scale::Felicita::SERVICE) {
            return "felicita";
        }
    }

    return "";  // Not a recognized scale
}
