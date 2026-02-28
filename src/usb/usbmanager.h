#pragma once

#include <QObject>
#include <QSet>
#include <QTimer>

#ifndef Q_OS_ANDROID
#include <QSerialPort>
#include <QSerialPortInfo>
#endif

class SerialTransport;

/**
 * USB device discovery manager for DE1 espresso machines.
 *
 * On desktop: polls QSerialPortInfo, filters by vendor ID (QinHeng/WCH),
 * and probes new ports by sending a subscribe command.
 *
 * On Android: uses JNI to call the Android USB Host API (UsbManager), which
 * is the only way to enumerate USB devices and do serial I/O on Android
 * (QSerialPortInfo reports VID=0, QSerialPort can't open /dev/ttyACM0).
 *
 * When a DE1 is confirmed, a SerialTransport is created and de1Discovered()
 * is emitted. When the port disappears (cable unplugged), de1Lost() is emitted.
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
    QTimer m_pollTimer;
    SerialTransport* m_transport = nullptr;
    QString m_connectedPortName;
    QString m_connectedSerialNumber;
    QByteArray m_probeBuffer;
    bool m_hasLoggedInitialPorts = false;  // Prevent repeated first-poll logging

#ifdef Q_OS_ANDROID
    // Android: JNI-based device detection and probing
    void pollPortsAndroid();
    void probeAndroid();
    void onAndroidProbeRead();
    void onAndroidProbeTimeout();
    void cleanupAndroidProbe(bool closeConnection);

    bool m_androidProbing = false;
    bool m_androidPermissionRequested = false;
    QTimer* m_androidProbeTimer = nullptr;
    QTimer* m_androidReadTimer = nullptr;
#else
    // Desktop: QSerialPort-based detection and probing
    void pollPortsDesktop();
    void probePort(const QSerialPortInfo& portInfo);
    void onProbeReadyRead();
    void onProbeTimeout();
    void cleanupProbe();

    QSet<QString> m_knownPorts;
    QSet<QString> m_probingPorts;
    QSerialPort* m_probePort = nullptr;
    QTimer* m_probeTimer = nullptr;
    QSerialPortInfo m_probingPortInfo;
#endif

    static constexpr int POLL_INTERVAL_MS = 2000;
    static constexpr int PROBE_TIMEOUT_MS = 2000;
    static constexpr uint16_t VENDOR_ID_WCH = 0x1A86;
};
