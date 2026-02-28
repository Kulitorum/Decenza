#pragma once

#include <QByteArray>
#include <QString>

/**
 * C++ wrapper for AndroidUsbSerial Java class (JNI bridge).
 *
 * On Android, QSerialPort/QSerialPortInfo cannot access USB serial devices:
 * - QSerialPortInfo reports VID=0 (sysfs paths restricted)
 * - QSerialPort can't open /dev/ttyACM0 (owned by radio:radio)
 *
 * This helper uses Android's USB Host API via JNI for proper device
 * enumeration, permission handling, and CDC-ACM bulk transfer I/O.
 *
 * All methods are static — mirrors the Java singleton pattern.
 * On non-Android platforms, all methods are no-ops returning failure.
 */
class AndroidUsbHelper {
public:
    // -- Device discovery --

    /** Check if a WCH USB device (CH340/CH9102) is attached. */
    static bool hasDevice();

    /** Check if we have Android USB permission for the device. */
    static bool hasPermission();

    /** Request USB permission (shows system dialog). Non-blocking. */
    static void requestPermission();

    /**
     * Get device info string: "vendorId:productId:serialNumber".
     * Serial number may be empty on some Android versions.
     */
    static QString deviceInfo();

    // -- Connection management --

    /**
     * Open the USB device and configure CDC-ACM serial (115200 8N1).
     * Starts a background read thread in Java.
     * @return true on success
     */
    static bool open();

    /** Close the USB connection and stop the read thread. */
    static void close();

    /** Check if the connection is open and active. */
    static bool isOpen();

    // -- I/O --

    /**
     * Write data to the USB device.
     * @return number of bytes written, or -1 on error
     */
    static int write(const QByteArray& data);

    /**
     * Read all data accumulated since the last call.
     * Returns empty QByteArray if no data available.
     * Thread-safe — reads from Java-side buffer filled by read thread.
     */
    static QByteArray readAvailable();

    /** Get the last error message from the Java side. */
    static QString lastError();
};
