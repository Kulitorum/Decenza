#include "usb/usbmanager.h"
#include "usb/serialtransport.h"

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
    cleanupProbe();
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

    qDebug() << "USBManager: Starting USB port polling every" << POLL_INTERVAL_MS << "ms";
    emit logMessage(QStringLiteral("USB polling started"));

    // Do an immediate poll, then start the timer
    pollPorts();
    m_pollTimer.start();
}

void USBManager::stopPolling()
{
    m_pollTimer.stop();
    cleanupProbe();
}

// ---------------------------------------------------------------------------
// Port polling
// ---------------------------------------------------------------------------

void USBManager::pollPorts()
{
    const auto ports = QSerialPortInfo::availablePorts();

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
        qWarning() << "USBManager: Connected port" << m_connectedPortName << "disappeared";
        emit logMessage(QStringLiteral("USB port %1 disconnected").arg(m_connectedPortName));

        m_connectedPortName.clear();
        m_connectedSerialNumber.clear();

        // Clear transport pointer but don't delete — DE1Device may own it via setTransport
        m_transport = nullptr;

        emit de1Lost();
        emit de1ConnectedChanged();
    }

    // Check if a port being probed disappeared
    if (m_probePort && !currentPorts.contains(m_probingPortInfo.portName())) {
        qDebug() << "USBManager: Probing port" << m_probingPortInfo.portName() << "disappeared, aborting probe";
        cleanupProbe();
    }

    // Update known ports
    m_knownPorts = currentPorts;

    // Probe new candidates (one at a time)
    if (!m_probePort && !candidatePorts.isEmpty() && !m_transport) {
        probePort(candidatePorts.first());
    }
}

// ---------------------------------------------------------------------------
// Port probing
// ---------------------------------------------------------------------------

void USBManager::probePort(const QSerialPortInfo& portInfo)
{
    if (m_probePort) {
        // Already probing another port
        return;
    }

    if (m_transport) {
        // Already connected, no need to probe
        return;
    }

    qDebug() << "USBManager: Probing port" << portInfo.portName()
             << "VID:" << Qt::hex << portInfo.vendorIdentifier()
             << "PID:" << portInfo.productIdentifier() << Qt::dec;
    emit logMessage(QStringLiteral("Probing USB port %1").arg(portInfo.portName()));

    m_probingPortInfo = portInfo;
    m_probingPorts.insert(portInfo.portName());
    m_probeBuffer.clear();

    // Create temporary serial port for probing
    m_probePort = new QSerialPort(this);
    m_probePort->setPortName(portInfo.portName());
    m_probePort->setBaudRate(115200);
    m_probePort->setDataBits(QSerialPort::Data8);
    m_probePort->setStopBits(QSerialPort::OneStop);
    m_probePort->setParity(QSerialPort::NoParity);
    m_probePort->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_probePort->open(QIODevice::ReadWrite)) {
        qWarning() << "USBManager: Failed to open" << portInfo.portName()
                    << "for probing:" << m_probePort->errorString();
        emit logMessage(QStringLiteral("Failed to open %1: %2")
                            .arg(portInfo.portName(), m_probePort->errorString()));
        cleanupProbe();
        return;
    }

    // DE1 serial protocol requires DTR and RTS off
    m_probePort->setDataTerminalReady(false);
    m_probePort->setRequestToSend(false);

    // Listen for response data
    connect(m_probePort, &QSerialPort::readyRead, this, &USBManager::onProbeReadyRead);

    // Set up timeout
    m_probeTimer = new QTimer(this);
    m_probeTimer->setSingleShot(true);
    m_probeTimer->setInterval(PROBE_TIMEOUT_MS);
    connect(m_probeTimer, &QTimer::timeout, this, &USBManager::onProbeTimeout);
    m_probeTimer->start();

    // Send subscribe to shot sample endpoint: <+M>\n
    // If a DE1 is on the other end, it will respond with [M] data
    m_probePort->write("<+M>\n");
}

void USBManager::onProbeReadyRead()
{
    if (!m_probePort) {
        return;
    }

    m_probeBuffer.append(m_probePort->readAll());

    // Look for [M] in the response — indicates this is a DE1
    if (m_probeBuffer.contains("[M]")) {
        QString confirmedPortName = m_probingPortInfo.portName();
        QString sn = m_probingPortInfo.serialNumber();

        qDebug() << "USBManager: DE1 confirmed on port" << confirmedPortName
                 << "serial:" << sn;
        emit logMessage(QStringLiteral("DE1 found on %1 (S/N: %2)")
                            .arg(confirmedPortName, sn.isEmpty() ? QStringLiteral("N/A") : sn));

        // Close the probe port before creating the transport
        cleanupProbe();

        // Create the real SerialTransport
        m_transport = new SerialTransport(confirmedPortName, this);
        m_transport->setSerialNumber(sn);
        m_connectedPortName = confirmedPortName;
        m_connectedSerialNumber = sn;

        // Open the transport so it's ready for DE1Device
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

    qDebug() << "USBManager: Probe timeout on port" << m_probingPortInfo.portName()
             << "- not a DE1 (received:" << m_probeBuffer.size() << "bytes)";
    emit logMessage(QStringLiteral("Port %1 is not a DE1 (probe timeout)")
                        .arg(m_probingPortInfo.portName()));

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

    // Remove from probing set so pollPorts doesn't try to re-probe
    // (it's now in m_knownPorts and won't be a candidate again)
    m_probingPorts.remove(m_probingPortInfo.portName());
    m_probeBuffer.clear();
}
