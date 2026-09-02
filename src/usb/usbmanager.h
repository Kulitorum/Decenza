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
 * On Android: uses JNI to call the Android USB Host API (USBManager), which
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

    // See UsbScaleManager::onHotplugEvent — same contract, same reason: the tick is
    // hasDevice()-driven, so hotplug runs it rather than duplicating it.
    void onHotplugEvent();

    Q_INVOKABLE void disconnectUsb();

    SerialTransport* transport() const { return m_transport; }

signals:
    void de1ConnectedChanged();
    void de1Discovered(SerialTransport* transport);
    void de1Lost();
    void logMessage(const QString& message);

private slots:
    void onPollTimerTick();

private:
    // A confirmed DE1 whose transport then failed to open. Deletes the
    // half-built transport and re-arms discovery so the next poll re-probes the
    // still-plugged-in machine.
    //
    // This must run BEFORE m_transport is assigned, and the callers are written
    // that way on purpose. isDe1Connected() is `m_transport != nullptr`, and
    // main.cpp reads it to decide whether the DE1 is being handled over USB —
    // both on de1Discovered (skip the BLE connect) and in the reconnect ladder
    // (skip the retry entirely). A non-null pointer to a transport that never
    // opened therefore does not merely leak: it convinces the app that USB owns
    // the machine, suppressing the BLE reconnect for the rest of the session.
    // The DE1 ends up unreachable over both transports until a restart.
    //
    // Safe to delete here precisely because de1Discovered() has not been emitted
    // yet, so nothing downstream holds the pointer. That is NOT true once the
    // transport is live — see the port-disappeared path, which clears the
    // pointer without deleting because DE1Device owns it by then.
    void discardUnopenedTransport(SerialTransport* transport, const QString& portLabel);

    QTimer m_pollTimer;
    SerialTransport* m_transport = nullptr;
    bool m_userDisconnected = false;  // Suppress auto-reconnect after user-initiated disconnect
    QString m_connectedPortName;
    QString m_connectedSerialNumber;
    QByteArray m_probeBuffer;
    bool m_hasLoggedInitialPorts = false;  // Prevent repeated first-poll logging

#ifdef Q_OS_ANDROID
    // Android: JNI-based device detection and probing
    void onPollTimerTickAndroid();
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
    void onPollTimerTickDesktop();
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
    static constexpr uint16_t PRODUCT_ID_DE1 = 0x55D3;  // CH9102 — DE1 only (not scale)
};
