#include "usb/usbmanager.h"
#include "usb/serialtransport.h"

#ifdef Q_OS_ANDROID
#include "usb/androidusbhelper.h"
#endif

#include <QDebug>

USBManager::USBManager(QObject* parent)
    : QObject(parent)
{
    m_pollTimer.setInterval(POLL_INTERVAL_MS);
    connect(&m_pollTimer, &QTimer::timeout, this, &USBManager::pollPorts);
}

USBManager::~USBManager()
{
    stopPolling();
#ifdef Q_OS_ANDROID
    cleanupAndroidProbe(true);
#else
    cleanupProbe();
#endif
}

// ---------------------------------------------------------------------------
// Properties
// ---------------------------------------------------------------------------

bool USBManager::isDe1Connected() const
{
    return m_transport != nullptr;
}

QString USBManager::portName() const
{
    return m_connectedPortName;
}

QString USBManager::serialNumber() const
{
    return m_connectedSerialNumber;
}

// ---------------------------------------------------------------------------
// Polling control
// ---------------------------------------------------------------------------

void USBManager::startPolling()
{
    if (m_pollTimer.isActive()) {
        return;
    }

    qDebug() << "[USB] Starting port polling every" << POLL_INTERVAL_MS << "ms";
    emit logMessage(QStringLiteral("[USB] Polling started"));

    // Do an immediate poll, then start the timer
    pollPorts();
    m_pollTimer.start();
}

void USBManager::stopPolling()
{
    m_pollTimer.stop();
#ifdef Q_OS_ANDROID
    cleanupAndroidProbe(true);
#else
    cleanupProbe();
#endif
}

// ---------------------------------------------------------------------------
// Port polling — dispatches to platform-specific implementation
// ---------------------------------------------------------------------------

void USBManager::pollPorts()
{
#ifdef Q_OS_ANDROID
    pollPortsAndroid();
#else
    pollPortsDesktop();
#endif
}

// ===========================================================================
// Android implementation — uses JNI to call Android USB Host API
// ===========================================================================

#ifdef Q_OS_ANDROID

void USBManager::pollPortsAndroid()
{
    bool devicePresent = AndroidUsbHelper::hasDevice();

    // Log device presence on first poll
    if (!m_hasLoggedInitialPorts) {
        m_hasLoggedInitialPorts = true;
        if (devicePresent) {
            QString info = AndroidUsbHelper::deviceInfo();
            qDebug() << "[USB] Android USB device found:" << info;
            emit logMessage(QStringLiteral("[USB] Device found: %1").arg(info));
        } else {
            qDebug() << "[USB] No WCH USB device detected via Android USB Host API";
            emit logMessage(QStringLiteral("[USB] No USB device detected"));
        }
    }

    // Check if connected device disappeared
    if (m_transport && !devicePresent) {
        qWarning() << "[USB] Connected USB device disappeared";
        emit logMessage(QStringLiteral("[USB] USB device disconnected"));

        m_connectedPortName.clear();
        m_connectedSerialNumber.clear();
        m_transport = nullptr;
        m_androidPermissionRequested = false;

        emit de1Lost();
        emit de1ConnectedChanged();
        return;
    }

    // Check if probing device disappeared
    if (m_androidProbing && !devicePresent) {
        qDebug() << "[USB] Probing device disappeared, aborting probe";
        cleanupAndroidProbe(true);
        return;
    }

    // Already connected — nothing to do
    if (m_transport) return;

    // Already probing — wait for result
    if (m_androidProbing) return;

    // No device — nothing to do
    if (!devicePresent) {
        // Reset permission flag so we can re-request if device is reconnected
        m_androidPermissionRequested = false;
        // Reset initial log flag so we log again when a device appears
        m_hasLoggedInitialPorts = false;
        return;
    }

    // Check permission
    if (!AndroidUsbHelper::hasPermission()) {
        if (!m_androidPermissionRequested) {
            m_androidPermissionRequested = true;
            qDebug() << "[USB] Requesting Android USB permission...";
            emit logMessage(QStringLiteral("[USB] Requesting USB permission..."));
            AndroidUsbHelper::requestPermission();
        }
        return;
    }

    // Device present with permission — probe it
    probeAndroid();
}

void USBManager::probeAndroid()
{
    m_androidProbing = true;
    m_probeBuffer.clear();

    QString info = AndroidUsbHelper::deviceInfo();
    qDebug() << "[USB] Probing Android USB device:" << info;
    emit logMessage(QStringLiteral("[USB] Probing USB device: %1").arg(info));

    if (!AndroidUsbHelper::open()) {
        QString err = AndroidUsbHelper::lastError();
        qWarning() << "[USB] Failed to open Android USB:" << err;
        emit logMessage(QStringLiteral("[USB] Failed to open: %1").arg(err));
        m_androidProbing = false;
        return;
    }

    // Send probe: subscribe to shot sample endpoint
    // If a DE1 is on the other end, it will respond with [M] data
    QByteArray probeCmd = QByteArrayLiteral("<+M>\n");
    int written = AndroidUsbHelper::write(probeCmd);
    qDebug() << "[USB] Sent probe <+M>, wrote" << written << "bytes";

    // Set up timeout timer
    m_androidProbeTimer = new QTimer(this);
    m_androidProbeTimer->setSingleShot(true);
    m_androidProbeTimer->setInterval(PROBE_TIMEOUT_MS);
    connect(m_androidProbeTimer, &QTimer::timeout, this, &USBManager::onAndroidProbeTimeout);

    // Set up read poll timer (check for probe response every 50ms)
    m_androidReadTimer = new QTimer(this);
    m_androidReadTimer->setInterval(50);
    connect(m_androidReadTimer, &QTimer::timeout, this, &USBManager::onAndroidProbeRead);

    m_androidProbeTimer->start();
    m_androidReadTimer->start();
}

void USBManager::onAndroidProbeRead()
{
    QByteArray data = AndroidUsbHelper::readAvailable();
    if (data.isEmpty()) return;

    m_probeBuffer.append(data);

    qDebug() << "[USB] Probe received" << data.size() << "bytes, total:" << m_probeBuffer.size()
             << "data:" << m_probeBuffer;

    // Look for [M] in the response — confirms this is a DE1
    if (m_probeBuffer.contains("[M]")) {
        // Parse device info: "vendorId:productId:serialNumber"
        QString info = AndroidUsbHelper::deviceInfo();
        QStringList parts = info.split(QLatin1Char(':'));
        QString sn = (parts.size() > 2) ? parts[2] : QString();

        qDebug() << "[USB] DE1 confirmed via Android USB! S/N:" << sn;
        emit logMessage(QStringLiteral("[USB] DE1 found via Android USB (S/N: %1)")
                            .arg(sn.isEmpty() ? QStringLiteral("N/A") : sn));

        // Stop probe timers but DON'T close the connection — SerialTransport will use it
        cleanupAndroidProbe(false);

        // Create SerialTransport backed by the already-open Android USB connection
        m_transport = new SerialTransport(QStringLiteral("android-usb"), this);
        m_transport->setSerialNumber(sn);
        m_connectedPortName = QStringLiteral("Android USB");
        m_connectedSerialNumber = sn;

        // Open starts the read timer and subscribes (connection already open via JNI)
        m_transport->open();

        emit de1ConnectedChanged();
        emit de1Discovered(m_transport);
    }
}

void USBManager::onAndroidProbeTimeout()
{
    qDebug() << "[USB] Android USB probe timeout (received" << m_probeBuffer.size()
             << "bytes:" << m_probeBuffer.toHex() << ")";
    emit logMessage(QStringLiteral("[USB] Probe timeout (got %1 bytes)")
                        .arg(m_probeBuffer.size()));
    cleanupAndroidProbe(true);
}

void USBManager::cleanupAndroidProbe(bool closeConnection)
{
    if (m_androidProbeTimer) {
        m_androidProbeTimer->stop();
        m_androidProbeTimer->deleteLater();
        m_androidProbeTimer = nullptr;
    }

    if (m_androidReadTimer) {
        m_androidReadTimer->stop();
        m_androidReadTimer->deleteLater();
        m_androidReadTimer = nullptr;
    }

    if (closeConnection) {
        AndroidUsbHelper::close();
    }

    m_probeBuffer.clear();
    m_androidProbing = false;
}

#endif // Q_OS_ANDROID

// ===========================================================================
// Desktop implementation — uses QSerialPort / QSerialPortInfo
// ===========================================================================

#ifndef Q_OS_ANDROID

void USBManager::pollPortsDesktop()
{
    const auto ports = QSerialPortInfo::availablePorts();

    // Log all ports on first poll for debugging (once only)
    if (!m_hasLoggedInitialPorts && !ports.isEmpty()) {
        m_hasLoggedInitialPorts = true;
        for (const auto& port : ports) {
            qDebug() << "[USB] Found port:" << port.portName()
                     << "VID:" << Qt::hex << port.vendorIdentifier()
                     << "PID:" << port.productIdentifier() << Qt::dec
                     << "desc:" << port.description()
                     << "mfg:" << port.manufacturer()
                     << "serial:" << port.serialNumber()
                     << "sysLoc:" << port.systemLocation();
            emit logMessage(QStringLiteral("[USB] Port %1 VID:%2 PID:%3 %4")
                                .arg(port.portName())
                                .arg(port.vendorIdentifier(), 4, 16, QLatin1Char('0'))
                                .arg(port.productIdentifier(), 4, 16, QLatin1Char('0'))
                                .arg(port.description()));
        }
        if (ports.isEmpty()) {
            qDebug() << "[USB] No serial ports found";
        }
    }

    // Build set of currently-present port names (filtered by VID)
    QSet<QString> currentPorts;
    QList<QSerialPortInfo> candidatePorts;

    for (const auto& port : ports) {
        // Filter by WCH vendor ID (CH340, CH9102, etc.)
        if (port.vendorIdentifier() == VENDOR_ID_WCH) {
            currentPorts.insert(port.portName());

            // If this is a new port we haven't seen, it's a probe candidate
            if (!m_knownPorts.contains(port.portName())
                && !m_probingPorts.contains(port.portName())) {
                candidatePorts.append(port);
            }
        }
    }

    // Check if our connected port disappeared
    if (!m_connectedPortName.isEmpty() && !currentPorts.contains(m_connectedPortName)) {
        qWarning() << "[USB] Connected port" << m_connectedPortName << "disappeared";
        emit logMessage(QStringLiteral("[USB] Port %1 disconnected").arg(m_connectedPortName));

        m_connectedPortName.clear();
        m_connectedSerialNumber.clear();

        // Clear transport pointer but don't delete — DE1Device may own it via setTransport
        m_transport = nullptr;

        emit de1Lost();
        emit de1ConnectedChanged();
    }

    // Check if a port being probed disappeared
    if (m_probePort && !currentPorts.contains(m_probingPortInfo.portName())) {
        qDebug() << "[USB] Probing port" << m_probingPortInfo.portName() << "disappeared, aborting probe";
        cleanupProbe();
    }

    // Update known ports
    m_knownPorts = currentPorts;

    // Probe new candidates (one at a time)
    if (!m_probePort && !candidatePorts.isEmpty() && !m_transport) {
        probePort(candidatePorts.first());
    }
}

void USBManager::probePort(const QSerialPortInfo& portInfo)
{
    if (m_probePort) {
        return;
    }

    if (m_transport) {
        return;
    }

    qDebug() << "[USB] Probing port" << portInfo.portName()
             << "VID:" << Qt::hex << portInfo.vendorIdentifier()
             << "PID:" << portInfo.productIdentifier() << Qt::dec
             << "sysLoc:" << portInfo.systemLocation();
    emit logMessage(QStringLiteral("[USB] Probing %1 (%2)")
                        .arg(portInfo.portName(), portInfo.systemLocation()));

    m_probingPortInfo = portInfo;
    m_probingPorts.insert(portInfo.portName());
    m_probeBuffer.clear();

    m_probePort = new QSerialPort(this);
    m_probePort->setPortName(portInfo.portName());
    m_probePort->setBaudRate(115200);
    m_probePort->setDataBits(QSerialPort::Data8);
    m_probePort->setStopBits(QSerialPort::OneStop);
    m_probePort->setParity(QSerialPort::NoParity);
    m_probePort->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_probePort->open(QIODevice::ReadWrite)) {
        qWarning() << "[USB] Failed to open" << portInfo.portName()
                    << "for probing:" << m_probePort->errorString();
        emit logMessage(QStringLiteral("[USB] FAILED to open %1: %2")
                            .arg(portInfo.portName(), m_probePort->errorString()));
        cleanupProbe();
        return;
    }

    m_probePort->setDataTerminalReady(false);
    m_probePort->setRequestToSend(false);

    connect(m_probePort, &QSerialPort::readyRead, this, &USBManager::onProbeReadyRead);

    m_probeTimer = new QTimer(this);
    m_probeTimer->setSingleShot(true);
    m_probeTimer->setInterval(PROBE_TIMEOUT_MS);
    connect(m_probeTimer, &QTimer::timeout, this, &USBManager::onProbeTimeout);
    m_probeTimer->start();

    m_probePort->write("<+M>\n");
}

void USBManager::onProbeReadyRead()
{
    if (!m_probePort) {
        return;
    }

    m_probeBuffer.append(m_probePort->readAll());

    if (m_probeBuffer.contains("[M]")) {
        QString confirmedPortName = m_probingPortInfo.portName();
        QString sn = m_probingPortInfo.serialNumber();

        qDebug() << "[USB] DE1 confirmed on port" << confirmedPortName
                 << "serial:" << sn;
        emit logMessage(QStringLiteral("[USB] DE1 found on %1 (S/N: %2)")
                            .arg(confirmedPortName, sn.isEmpty() ? QStringLiteral("N/A") : sn));

        cleanupProbe();

        m_transport = new SerialTransport(confirmedPortName, this);
        m_transport->setSerialNumber(sn);
        m_connectedPortName = confirmedPortName;
        m_connectedSerialNumber = sn;

        m_transport->open();

        emit de1ConnectedChanged();
        emit de1Discovered(m_transport);
    }
}

void USBManager::onProbeTimeout()
{
    if (!m_probePort) {
        return;
    }

    qDebug() << "[USB] Probe timeout on" << m_probingPortInfo.portName()
             << "- not a DE1 (received:" << m_probeBuffer.size() << "bytes:"
             << m_probeBuffer.toHex() << ")";
    emit logMessage(QStringLiteral("[USB] Probe timeout on %1 (got %2 bytes)")
                        .arg(m_probingPortInfo.portName())
                        .arg(m_probeBuffer.size()));

    cleanupProbe();
}

void USBManager::cleanupProbe()
{
    if (m_probeTimer) {
        m_probeTimer->stop();
        m_probeTimer->deleteLater();
        m_probeTimer = nullptr;
    }

    if (m_probePort) {
        if (m_probePort->isOpen()) {
            m_probePort->close();
        }
        m_probePort->deleteLater();
        m_probePort = nullptr;
    }

    m_probingPorts.remove(m_probingPortInfo.portName());
    m_probeBuffer.clear();
}

#endif // !Q_OS_ANDROID
