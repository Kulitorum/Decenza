#pragma once

#include "ble/de1transport.h"

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
 * Protocol summary (from reaprime firmware analysis):
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

#ifdef Q_OS_ANDROID
    QTimer m_readTimer;  ///< Polls AndroidUsbHelper::readAvailable() at ~20ms
#else
    QSerialPort* m_port = nullptr;
#endif
};
