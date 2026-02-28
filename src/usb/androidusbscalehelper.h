#pragma once

#include <QByteArray>
#include <QString>

/**
 * C++ wrapper for AndroidUsbScale Java class (JNI bridge).
 *
 * Mirrors AndroidUsbHelper but targets the Half Decent Scale (PID 0x7523)
 * instead of the DE1 (PID 0x55D3). Both share VID 0x1A86 (WCH).
 *
 * All methods are static — mirrors the Java singleton pattern.
 * On non-Android platforms, all methods are no-ops returning failure.
 */
class AndroidUsbScaleHelper {
public:
    // -- Device discovery --

    /** Check if a Half Decent Scale (WCH CH340) is attached. */
    static bool hasDevice();

    /** Check if we have Android USB permission for the scale. */
    static bool hasPermission();

    /** Request USB permission (shows system dialog). Non-blocking. */
    static void requestPermission();

    /** Get device info string: "vendorId:productId:serialNumber". */
    static QString deviceInfo();

    // -- Connection management --

    /** Open the scale and configure CDC-ACM serial (115200 8N1). */
    static bool open();

    /** Close the USB scale connection and stop the read thread. */
    static void close();

    /** Check if the scale connection is open and active. */
    static bool isOpen();

    // -- I/O --

    /** Write data to the USB scale. Returns bytes written or -1. */
    static int write(const QByteArray& data);

    /** Read all data accumulated since the last call. */
    static QByteArray readAvailable();

    /** Get the last error message from the Java side. */
    static QString lastError();
};
