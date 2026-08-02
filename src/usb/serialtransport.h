#pragma once

#include "ble/de1transport.h"
#include "usb/serialstall.h"

#include <QElapsedTimer>
#include <QSet>
#include <QTimer>

#ifndef Q_OS_ANDROID
#include <QSerialPort>
#endif

/**
 * Serial (USB-C) transport for DE1 communication.
 *
 * Implements DE1Transport for USB-C wired connections. The DE1's serial protocol
 * maps BLE characteristic UUIDs to single-letter codes (A-R) and hex-encodes
 * binary payloads as ASCII text lines.
 *
 * On desktop: uses QSerialPort for I/O.
 * On Android: uses AndroidUsbHelper (JNI → Android USB Host API) for I/O,
 * because QSerialPort cannot access USB serial devices on Android.
 *
 * Protocol summary (from Decaid firmware analysis):
 *   Host  -> DE1:  <LETTER>hexdata\n     (write)
 *   DE1   -> Host: [LETTER]hexdata\n     (notification/response)
 *   Subscribe:     <+LETTER>\n
 *   Unsubscribe:   <-LETTER>\n
 *
 * Endpoint mapping:
 *   UUID 0000A001 -> 'A', 0000A002 -> 'B', ..., 0000A012 -> 'R'
 *   Formula: letter = 'A' + (shortUuid - 0xA001)
 *
 * Serial config: 115200 baud, 8N1, no flow control, DTR=false, RTS=false.
 */
class SerialTransport : public DE1Transport {
    Q_OBJECT

public:
    explicit SerialTransport(const QString& portName, QObject* parent = nullptr);
    ~SerialTransport() override;

    // -- DE1Transport interface --
    void write(const QBluetoothUuid& uuid, const QByteArray& data) override;
    void read(const QBluetoothUuid& uuid) override;
    void subscribe(const QBluetoothUuid& uuid) override;
    void subscribeAll() override;
    void disconnect() override;
    bool isConnected() const override;
    QString transportName() const override { return QStringLiteral("USB-C"); }

    // -- Serial-specific API --

    /** The OS serial port name (e.g., "COM3", "/dev/ttyACM0", or "android-usb"). */
    QString portName() const;

    /** DE1 serial number (set by USBManager after identification). */
    QString serialNumber() const;
    void setSerialNumber(const QString& sn);

    /**
     * Open the serial port and begin communication.
     * On desktop: opens QSerialPort with 115200/8N1.
     * On Android: starts read polling on already-open JNI connection.
     * Subscribes to DE1 notifications and emits connected() on success.
     */
    void open();

private slots:
#ifdef Q_OS_ANDROID
    void onAndroidReadTimer();
#else
    void onReadyRead();
    void onErrorOccurred(QSerialPort::SerialPortError error);
#endif

private:
    void processLine(const QString& line);
    void processBuffer();  ///< Extract and process complete lines from m_buffer

    /** Write raw bytes to the serial connection (platform-specific). */
    void writeRaw(const QByteArray& data);

    static char uuidToLetter(const QBluetoothUuid& uuid);
    static QBluetoothUuid letterToUuid(char letter);
    static QByteArray hexStringToBytes(const QString& hex);
    static QString bytesToHexString(const QByteArray& data);

    QString m_portName;
    QString m_serialNumber;
    QByteArray m_buffer;
    bool m_connected = false;
    QSet<char> m_subscribed;

    // Stall detection for a port that is OPEN but has gone silent — the serial
    // counterpart of BleTransport's zombie-link check, and until now this path had
    // nothing of the kind. A USB DE1 whose port opens and whose subscribes are
    // written but which never starts notifying (or stops mid-session) produced
    // "Port opened", maybe "Machine info", then silence forever: no WARN, and
    // "Disconnected:" never fires because the port is still open. Deleting the
    // per-frame RX line was right on volume — ~600 DEBUG lines a shot — but it was
    // the only evidence of that negative case, so this replaces it with one line.
    //
    // Checked on WRITE, not on a timer. In practice the write that keeps this
    // fed is BatteryManager's 60 s forced setUsbChargerOn (its gate is transport-
    // level isConnected(), so it fires even if the DE1 itself never notifies) —
    // DE1Device has no periodic write of its own once connected. Either way a
    // write already is the periodic event, and it makes the signature exactly
    // BleTransport's: we are still talking to the machine and it has stopped
    // answering. Event-driven also keeps this out of the "timers as guards" trap
    // — nothing here needs a timer to exist. (If BatteryManager's cadence or gate
    // ever changes, re-check that this detector still gets fed.)
    //
    // Diagnostic only: it warns and does nothing else. BleTransport's equivalent
    // tears the link down, which is why that one is evaluated solely at a reconnect
    // attempt — a false positive there costs a reconnect. Here it costs one log
    // line, which is what makes the threshold below acceptable to ship.
    //
    // The threshold is PROVISIONAL and deliberately double BleTransport's 30 s. The
    // DE1's minimum push cadence is unmeasured across machine phases — the same
    // open question BleTransport's comment flags (harden-de1-ble-reliability tasks
    // 5.2 / 8.5) — and Sleep in particular is unverified: if the firmware stops
    // pushing water level while asleep, a tighter threshold would warn on every
    // sleep. Tighten it once the raw cadence is measured on hardware, not before.
    // The decision itself lives in usb/serialstall.h as a self-contained
    // Detector, because none of this class is reachable from a test: open()
    // needs a real port and write() early-returns without one. See that header
    // for the full lifecycle (arm/disarm/noteInbound/shouldWarn) and why it
    // replaced a free function taking four interchangeable parameters.
    static constexpr int INBOUND_STALE_MS = 60000;
    SerialStall::Detector m_stall{INBOUND_STALE_MS};
    // Wall-clock source for the detector — a stopwatch read on demand, not a
    // firing timer, so this is data, not the "timers as guards" pattern.
    QElapsedTimer m_clock;

#ifdef Q_OS_ANDROID
    QTimer m_readTimer;  ///< Polls AndroidUsbHelper::readAvailable() at ~20ms
#else
    QSerialPort* m_port = nullptr;
#endif
};
