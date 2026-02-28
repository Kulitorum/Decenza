package io.github.kulitorum.decenza_de1;

import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.hardware.usb.UsbConstants;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbEndpoint;
import android.hardware.usb.UsbInterface;
import android.hardware.usb.UsbManager;
import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.util.HashMap;

/**
 * Android USB Host API wrapper for DE1 serial communication.
 *
 * QSerialPort/QSerialPortInfo cannot read VID/PID or open serial ports on
 * Android because:
 * 1. sysfs paths for VID/PID are restricted (QSerialPortInfo reports VID=0)
 * 2. /dev/ttyACM0 is owned by radio:radio (QSerialPort::open fails)
 *
 * This class uses Android's USB Host API which properly enumerates devices,
 * manages permissions, and provides bulk transfer I/O through the CDC-ACM
 * protocol.
 *
 * Called from C++ via QJniObject. All methods are static (singleton pattern).
 */
public class AndroidUsbSerial {

    private static final String TAG = "AndroidUsbSerial";
    private static final int VENDOR_ID_WCH = 0x1A86;  // QinHeng/WCH (CH340, CH9102, etc.)
    private static final int PRODUCT_ID_DE1 = 0x55D3;  // CH9102 — DE1 espresso machine
    private static final String PERMISSION_ACTION = "io.github.kulitorum.decenza_de1.USB_PERMISSION";

    // CDC-ACM control transfer constants
    private static final int SET_LINE_CODING = 0x20;
    private static final int SET_CONTROL_LINE_STATE = 0x22;

    // Connection state (singleton — one USB serial device at a time)
    private static UsbDeviceConnection sConnection;
    private static UsbEndpoint sEndpointIn;
    private static UsbEndpoint sEndpointOut;
    private static int sControlInterfaceId = 0;
    private static Thread sReadThread;
    private static volatile boolean sReading = false;
    private static volatile boolean sDisconnected = false;
    private static final Object sBufferLock = new Object();
    private static ByteArrayOutputStream sReadBuffer = new ByteArrayOutputStream();
    private static volatile String sLastError = "";

    // -----------------------------------------------------------------------
    // Device discovery
    // -----------------------------------------------------------------------

    /**
     * Find the DE1 USB device (WCH CH9102, PID 0x55D3) attached to this Android device.
     * Returns null if none found. Filters by both VID and PID to avoid
     * claiming other WCH devices (e.g., Half Decent Scale uses CH340/0x7523).
     */
    private static UsbDevice findDevice(Context context) {
        UsbManager manager = (UsbManager) context.getSystemService(Context.USB_SERVICE);
        if (manager == null) return null;

        HashMap<String, UsbDevice> deviceList = manager.getDeviceList();
        for (UsbDevice device : deviceList.values()) {
            if (device.getVendorId() == VENDOR_ID_WCH
                    && device.getProductId() == PRODUCT_ID_DE1) {
                return device;
            }
        }
        return null;
    }

    /**
     * Check if a WCH USB device is attached.
     * Called from C++ via JNI.
     */
    public static boolean hasDevice(Context context) {
        return findDevice(context) != null;
    }

    /**
     * Check if we have permission to access the USB device.
     * Permission is granted either by USB_DEVICE_ATTACHED intent (user tapped OK)
     * or by requestPermission() dialog.
     */
    public static boolean hasPermission(Context context) {
        UsbDevice device = findDevice(context);
        if (device == null) return false;
        UsbManager manager = (UsbManager) context.getSystemService(Context.USB_SERVICE);
        return manager != null && manager.hasPermission(device);
    }

    /**
     * Request USB device permission. Shows a system dialog.
     * Result is checked via hasPermission() on next poll cycle.
     */
    public static void requestPermission(Context context) {
        UsbDevice device = findDevice(context);
        if (device == null) return;
        UsbManager manager = (UsbManager) context.getSystemService(Context.USB_SERVICE);
        if (manager == null) return;

        PendingIntent pi = PendingIntent.getBroadcast(
                context, 0,
                new Intent(PERMISSION_ACTION),
                PendingIntent.FLAG_IMMUTABLE);
        manager.requestPermission(device, pi);
        Log.d(TAG, "Requested USB permission for device " + device.getDeviceName());
    }

    /**
     * Get device info as "vendorId:productId:serialNumber".
     * Serial number may be empty on some Android versions.
     */
    public static String getDeviceInfo(Context context) {
        UsbDevice device = findDevice(context);
        if (device == null) return "";

        String serial = "";
        try {
            serial = device.getSerialNumber();
            if (serial == null) serial = "";
        } catch (SecurityException e) {
            // getSerialNumber() may throw on some Android versions without permission
            Log.w(TAG, "Cannot read serial number: " + e.getMessage());
        }

        return device.getVendorId() + ":" + device.getProductId() + ":" + serial;
    }

    // -----------------------------------------------------------------------
    // Connection management
    // -----------------------------------------------------------------------

    /**
     * Open the USB device and configure CDC-ACM serial (115200 8N1, DTR/RTS off).
     * Starts a background thread for continuous bulk reads.
     *
     * @return true on success, false on failure (check getLastError())
     */
    public static boolean open(Context context) {
        if (sConnection != null) {
            sLastError = "Already open";
            return false;
        }

        UsbDevice device = findDevice(context);
        if (device == null) {
            sLastError = "No WCH USB device found";
            return false;
        }

        UsbManager manager = (UsbManager) context.getSystemService(Context.USB_SERVICE);
        if (manager == null || !manager.hasPermission(device)) {
            sLastError = "No USB permission";
            return false;
        }

        Log.d(TAG, "Opening USB device: VID=" + String.format("0x%04X", device.getVendorId())
                + " PID=" + String.format("0x%04X", device.getProductId())
                + " interfaces=" + device.getInterfaceCount());

        // Find CDC-ACM interfaces.
        // CDC-ACM has two interfaces:
        //   - Communication Class (class 2, subclass 2) — control endpoint
        //   - Data Class (class 10 / 0x0A) — bulk IN + bulk OUT endpoints
        // Some chips (WCH) may use vendor-specific class (0xFF) instead.
        UsbInterface controlIface = null;
        UsbInterface dataIface = null;

        for (int i = 0; i < device.getInterfaceCount(); i++) {
            UsbInterface iface = device.getInterface(i);
            Log.d(TAG, "  Interface " + i + ": class=" + iface.getInterfaceClass()
                    + " subclass=" + iface.getInterfaceSubclass()
                    + " protocol=" + iface.getInterfaceProtocol()
                    + " endpoints=" + iface.getEndpointCount());

            if (iface.getInterfaceClass() == UsbConstants.USB_CLASS_COMM) {
                controlIface = iface;
            } else if (iface.getInterfaceClass() == UsbConstants.USB_CLASS_CDC_DATA) {
                dataIface = iface;
            }
        }

        // Fallback: if no CDC-ACM interfaces found, look for any interface with bulk endpoints
        // (handles vendor-specific class codes)
        if (dataIface == null) {
            Log.d(TAG, "No CDC-ACM data interface found, trying fallback (vendor-specific)");
            for (int i = 0; i < device.getInterfaceCount(); i++) {
                UsbInterface iface = device.getInterface(i);
                if (hasBulkEndpoints(iface)) {
                    dataIface = iface;
                    Log.d(TAG, "  Using interface " + i + " (class=" + iface.getInterfaceClass()
                            + ") as data interface (has bulk endpoints)");
                    break;
                }
            }
        }

        if (dataIface == null) {
            sLastError = "No suitable USB interface found (no CDC-ACM or bulk endpoints)";
            return false;
        }

        // Find bulk IN and OUT endpoints on the data interface
        UsbEndpoint epIn = null;
        UsbEndpoint epOut = null;
        for (int i = 0; i < dataIface.getEndpointCount(); i++) {
            UsbEndpoint ep = dataIface.getEndpoint(i);
            if (ep.getType() == UsbConstants.USB_ENDPOINT_XFER_BULK) {
                if (ep.getDirection() == UsbConstants.USB_DIR_IN) {
                    epIn = ep;
                } else {
                    epOut = ep;
                }
            }
        }

        if (epIn == null || epOut == null) {
            sLastError = "Missing bulk endpoints (IN=" + (epIn != null) + " OUT=" + (epOut != null) + ")";
            return false;
        }

        Log.d(TAG, "Bulk IN: ep=" + epIn.getAddress() + " maxPacket=" + epIn.getMaxPacketSize());
        Log.d(TAG, "Bulk OUT: ep=" + epOut.getAddress() + " maxPacket=" + epOut.getMaxPacketSize());

        // Open the device
        UsbDeviceConnection conn = manager.openDevice(device);
        if (conn == null) {
            sLastError = "UsbManager.openDevice() returned null";
            return false;
        }

        // Claim interfaces
        if (controlIface != null) {
            if (!conn.claimInterface(controlIface, true)) {
                Log.w(TAG, "Failed to claim control interface (non-fatal)");
            }
        }
        if (!conn.claimInterface(dataIface, true)) {
            sLastError = "Failed to claim data interface";
            conn.close();
            return false;
        }

        // Configure serial parameters via CDC SET_LINE_CODING
        // 7-byte structure: dwDTERate(4), bCharFormat(1), bParityType(1), bDataBits(1)
        int ctrlIfaceId = (controlIface != null) ? controlIface.getId() : 0;
        byte[] lineCoding = new byte[7];
        // 115200 baud = 0x0001C200 little-endian
        lineCoding[0] = (byte) 0x00;
        lineCoding[1] = (byte) 0xC2;
        lineCoding[2] = (byte) 0x01;
        lineCoding[3] = (byte) 0x00;
        lineCoding[4] = 0;  // 1 stop bit
        lineCoding[5] = 0;  // No parity
        lineCoding[6] = 8;  // 8 data bits

        int result = conn.controlTransfer(
                0x21,  // bmRequestType: class, interface, host-to-device
                SET_LINE_CODING,
                0, ctrlIfaceId,
                lineCoding, 7, 1000);
        Log.d(TAG, "SET_LINE_CODING result: " + result);

        // SET_CONTROL_LINE_STATE: DTR=0, RTS=0 (DE1 requires both off)
        result = conn.controlTransfer(
                0x21,  // bmRequestType: class, interface, host-to-device
                SET_CONTROL_LINE_STATE,
                0x0000,  // wValue: bit 0 = DTR, bit 1 = RTS — both off
                ctrlIfaceId,
                null, 0, 1000);
        Log.d(TAG, "SET_CONTROL_LINE_STATE result: " + result);

        // Store connection state
        sConnection = conn;
        sEndpointIn = epIn;
        sEndpointOut = epOut;
        sControlInterfaceId = ctrlIfaceId;
        sDisconnected = false;

        synchronized (sBufferLock) {
            sReadBuffer.reset();
        }

        // Start background read thread
        sReading = true;
        sReadThread = new Thread(() -> {
            Log.d(TAG, "Read thread started");
            byte[] buf = new byte[1024];
            int consecutiveErrors = 0;

            while (sReading) {
                try {
                    int len = sConnection.bulkTransfer(sEndpointIn, buf, buf.length, 100);
                    if (len > 0) {
                        consecutiveErrors = 0;
                        synchronized (sBufferLock) {
                            sReadBuffer.write(buf, 0, len);
                        }
                    } else if (len < 0) {
                        // -1 means timeout (normal with short timeout) or error
                        consecutiveErrors++;
                        if (consecutiveErrors > 50) {
                            // 50 consecutive errors * 100ms timeout = ~5 seconds of no data
                            // Likely device disconnected
                            Log.w(TAG, "Read thread: too many consecutive errors, assuming disconnect");
                            sDisconnected = true;
                            break;
                        }
                    } else {
                        // len == 0: no data, reset error counter
                        consecutiveErrors = 0;
                    }
                } catch (Exception e) {
                    Log.e(TAG, "Read thread exception: " + e.getMessage());
                    sDisconnected = true;
                    break;
                }
            }
            Log.d(TAG, "Read thread stopped (disconnected=" + sDisconnected + ")");
        }, "USB-Serial-Read");
        sReadThread.setDaemon(true);
        sReadThread.start();

        sLastError = "";
        Log.d(TAG, "USB serial opened successfully");
        return true;
    }

    /**
     * Write data to the USB device via bulk OUT endpoint.
     *
     * @return number of bytes written, or -1 on error
     */
    public static int write(byte[] data) {
        if (sConnection == null || sEndpointOut == null || sDisconnected) return -1;
        try {
            return sConnection.bulkTransfer(sEndpointOut, data, data.length, 1000);
        } catch (Exception e) {
            Log.e(TAG, "Write error: " + e.getMessage());
            return -1;
        }
    }

    /**
     * Read all data accumulated since the last call.
     * Returns an empty array if no data is available.
     * Thread-safe — reads from a buffer filled by the background read thread.
     */
    public static byte[] readAvailable() {
        synchronized (sBufferLock) {
            if (sReadBuffer.size() == 0) return new byte[0];
            byte[] data = sReadBuffer.toByteArray();
            sReadBuffer.reset();
            return data;
        }
    }

    /**
     * Close the USB connection and stop the read thread.
     */
    public static void close() {
        Log.d(TAG, "Closing USB serial connection");
        sReading = false;

        if (sReadThread != null) {
            try {
                sReadThread.join(500);
            } catch (InterruptedException ignored) {
            }
            sReadThread = null;
        }

        if (sConnection != null) {
            try {
                sConnection.close();
            } catch (Exception e) {
                Log.w(TAG, "Error closing connection: " + e.getMessage());
            }
            sConnection = null;
        }

        sEndpointIn = null;
        sEndpointOut = null;
        sDisconnected = false;

        synchronized (sBufferLock) {
            sReadBuffer.reset();
        }

        Log.d(TAG, "USB serial closed");
    }

    /**
     * Check if the USB connection is open and active.
     */
    public static boolean isOpen() {
        return sConnection != null && sReading && !sDisconnected;
    }

    /**
     * Get the last error message (set on open() failure, etc.).
     */
    public static String getLastError() {
        return sLastError;
    }

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    /**
     * Check if a USB interface has at least one bulk IN and one bulk OUT endpoint.
     */
    private static boolean hasBulkEndpoints(UsbInterface iface) {
        boolean hasIn = false, hasOut = false;
        for (int i = 0; i < iface.getEndpointCount(); i++) {
            UsbEndpoint ep = iface.getEndpoint(i);
            if (ep.getType() == UsbConstants.USB_ENDPOINT_XFER_BULK) {
                if (ep.getDirection() == UsbConstants.USB_DIR_IN) hasIn = true;
                else hasOut = true;
            }
        }
        return hasIn && hasOut;
    }
}
