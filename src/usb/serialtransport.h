#pragma once

#include "ble/de1transport.h"

#include <QSerialPort>
#include <QSet>

/**
 * Serial (USB-C) transport for DE1 communication.
 *
 * Implements DE1Transport using QSerialPort over a USB-C wired connection.
 * The DE1's serial protocol maps BLE characteristic UUIDs to single-letter
 * codes (A-R) and hex-encodes binary payloads as ASCII text lines.
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
 *
 * Lifecycle:
 *   1. Construct SerialTransport with port name
 *   2. Call open() to open the port and begin communication
 *   3. Emits connected() when ready
 *   4. Call disconnect() or delete to tear down
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

    /** The OS serial port name (e.g., "COM3" or "/dev/ttyACM0"). */
    QString portName() const;

    /** DE1 serial number (set by USBManager after identification). */
    QString serialNumber() const;
    void setSerialNumber(const QString& sn);

    /**
     * Open the serial port and begin communication.
     * Configures 115200/8N1, subscribes to DE1 notifications,
     * and emits connected() on success or errorOccurred() on failure.
     */
    void open();

private slots:
    void onReadyRead();
    void onErrorOccurred(QSerialPort::SerialPortError error);

private:
    void processLine(const QString& line);

    /** Convert a full BLE UUID to the single-letter serial protocol code.
     *  Returns '\0' if the UUID is not a DE1 characteristic (0xA001..0xA012). */
    static char uuidToLetter(const QBluetoothUuid& uuid);

    /** Convert a single-letter serial protocol code back to a full BLE UUID.
     *  Returns a null QBluetoothUuid if the letter is out of range. */
    static QBluetoothUuid letterToUuid(char letter);

    /** Decode an ASCII hex string ("0102ab") to binary bytes. */
    static QByteArray hexStringToBytes(const QString& hex);

    /** Encode binary bytes to an ASCII hex string ("0102ab"). */
    static QString bytesToHexString(const QByteArray& data);

    QSerialPort* m_port = nullptr;
    QString m_portName;
    QString m_serialNumber;
    QByteArray m_buffer;        ///< Line buffer for incomplete serial reads
    bool m_connected = false;
    QSet<char> m_subscribed;    ///< Letters we have already subscribed to
};
