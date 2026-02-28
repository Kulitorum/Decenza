#pragma once

#include "ble/scaledevice.h"

#include <QTimer>

#ifndef Q_OS_ANDROID
#include <QSerialPort>
#endif

/**
 * USB implementation of ScaleDevice for the Half Decent Scale.
 *
 * Uses the same 7-byte binary protocol as the BLE Decent Scale:
 *   [0x03, type, data0, data1, data2, data3, XOR]
 *
 * Weight packets: type 0xCE or 0xCA, data0:data1 = big-endian int16 / 10.0 grams
 * Button packets: type 0xAA, data0 = button number
 * Tare command:   type 0x0F
 * Init command:   type 0x20
 * LED command:    type 0x0A
 *
 * On Android: polls AndroidUsbScaleHelper via QTimer (20ms).
 * On desktop: uses QSerialPort readyRead signal.
 *
 * Serial config: 115200 baud, 8N1, no flow control.
 */
class UsbDecentScale : public ScaleDevice {
    Q_OBJECT

public:
    explicit UsbDecentScale(QObject* parent = nullptr);
    ~UsbDecentScale() override;

    // -- ScaleDevice interface --
    void connectToDevice(const QBluetoothDeviceInfo& device) override;
    QString name() const override { return QStringLiteral("Half Decent Scale (USB)"); }
    QString type() const override { return QStringLiteral("decent-usb"); }

public slots:
    void tare() override;
    void startTimer() override;
    void stopTimer() override;
    void resetTimer() override;
    void wake() override;
    void sleep() override;

    // -- USB-specific API --

    /**
     * Open the USB scale connection and start communication.
     * On Android: starts read polling on already-open JNI connection.
     * On desktop: opens QSerialPort with given port name.
     */
    void open(const QString& portName = QString());

    /** Disconnect from the scale. */
    void close();

private slots:
#ifdef Q_OS_ANDROID
    void onReadTimer();
#else
    void onReadyRead();
    void onErrorOccurred(QSerialPort::SerialPortError error);
#endif
    void onHeartbeatTimer();

private:
    void processBuffer();
    void processPacket(const QByteArray& packet);
    void sendCommand(const QByteArray& commandData);
    void writeRaw(const QByteArray& data);

    static uint8_t calculateXor(const QByteArray& data);

    QByteArray m_buffer;
    QTimer m_heartbeatTimer;

#ifdef Q_OS_ANDROID
    QTimer m_readTimer;
#else
    QSerialPort* m_port = nullptr;
#endif
};
