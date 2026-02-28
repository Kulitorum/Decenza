#pragma once

#include <QObject>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QSet>
#include <QTimer>

class SerialTransport;

/**
 * USB device discovery manager for DE1 espresso machines.
 *
 * Polls for serial ports, filters by vendor ID (QinHeng/WCH — covers CH340,
 * CH9102, and other USB-serial bridges used by the DE1), and probes new ports
 * by sending a subscribe command and waiting for a recognisable response.
 *
 * When a DE1 is confirmed, a SerialTransport is created and de1Discovered()
 * is emitted. When the port disappears (cable unplugged), de1Lost() is emitted.
 *
 * Usage:
 *   USBManager usbManager;
 *   connect(&usbManager, &USBManager::de1Discovered,
 *           [&](SerialTransport* t) { de1Device.setTransport(t); });
 *   usbManager.startPolling();
 */
class USBManager : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool de1Connected READ isDe1Connected NOTIFY de1ConnectedChanged)
    Q_PROPERTY(QString portName READ portName NOTIFY de1ConnectedChanged)
    Q_PROPERTY(QString serialNumber READ serialNumber NOTIFY de1ConnectedChanged)

public:
    explicit USBManager(QObject* parent = nullptr);
    ~USBManager() override;

    bool isDe1Connected() const;
    QString portName() const;
    QString serialNumber() const;

    void startPolling();
    void stopPolling();

    SerialTransport* transport() const { return m_transport; }

signals:
    void de1ConnectedChanged();
    void de1Discovered(SerialTransport* transport);
    void de1Lost();
    void logMessage(const QString& message);

private slots:
    void pollPorts();

private:
    void probePort(const QSerialPortInfo& portInfo);
    void onProbeReadyRead();
    void onProbeTimeout();
    void cleanupProbe();

    QTimer m_pollTimer;
    SerialTransport* m_transport = nullptr;  // Created by USBManager, transferred to DE1Device
    QString m_connectedPortName;
    QString m_connectedSerialNumber;
    QSet<QString> m_knownPorts;        // Port names we've already seen
    QSet<QString> m_probingPorts;      // Ports currently being probed

    // Probe state (one probe at a time)
    QSerialPort* m_probePort = nullptr;
    QTimer* m_probeTimer = nullptr;
    QSerialPortInfo m_probingPortInfo;
    QByteArray m_probeBuffer;

    static constexpr int POLL_INTERVAL_MS = 2000;
    static constexpr int PROBE_TIMEOUT_MS = 2000;
    static constexpr uint16_t VENDOR_ID_WCH = 0x1A86;
};
