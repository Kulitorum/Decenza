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
 * Android USB Host API wrapper for Half Decent Scale communication.
 *
 * The scale uses a WCH CH340 USB-to-serial chip (VID 0x1A86, PID 0x7522/0x7523).
 * Unlike the DE1's CH9102 (which is CDC-ACM), the CH340 uses vendor-specific
 * control transfers for baud rate and line configuration.
 *
 * Protocol: 115200 8N1, 7-byte binary packets (same as BLE):
 *   Weight: [0x03, 0xCE, MSB, LSB, 0x00, 0x00, XOR]
 *   Tare:   [0x03, 0x0F, ...]
 *   Init:   [0x03, 0x20, 0x01, ...]
 *
 * Called from C++ via QJniObject. All methods are static (singleton pattern).
 */
public class AndroidUsbScale {

    private static final String TAG = "Decenza";
    private static final int VENDOR_ID_WCH = 0x1A86;
    private static final int PRODUCT_ID_SCALE_1 = 0x7522;  // CH340 variant — Half Decent Scale
    private static final int PRODUCT_ID_SCALE_2 = 0x7523;  // CH340 variant — Half Decent Scale
    private static final String PERMISSION_ACTION = "io.github.kulitorum.decenza_de1.USB_SCALE_PERMISSION";

    // CH340 vendor-specific control transfer constants
    private static final int CH340_REQ_READ_VERSION = 0x5F;
    private static final int CH340_REQ_SERIAL_INIT  = 0xA1;
    private static final int CH340_REQ_WRITE_REG    = 0x9A;
    private static final int CH340_REQ_MODEM_CTRL   = 0xA4;

    // CH340 baud rate calculation constants
    private static final long CH340_BAUDBASE_FACTOR = 1532620800L;
    private static final int  CH340_BAUDBASE_DIVMAX = 3;

    // Connection state (singleton — one USB scale at a time)
    private static UsbDeviceConnection sConnection;
    private static UsbEndpoint sEndpointIn;
    private static UsbEndpoint sEndpointOut;
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
     * Find the Half Decent Scale (WCH CH340, PID 0x7523).
     * Returns null if none found.
     */
    private static UsbDevice findDevice(Context context) {
        UsbManager manager = (UsbManager) context.getSystemService(Context.USB_SERVICE);
        if (manager == null) {
            Log.w(TAG, "[USB] Scale Java: UsbManager is null");
            return null;
        }

        HashMap<String, UsbDevice> deviceList = manager.getDeviceList();
        for (UsbDevice device : deviceList.values()) {
            if (device.getVendorId() == VENDOR_ID_WCH
                    && (device.getProductId() == PRODUCT_ID_SCALE_1
                        || device.getProductId() == PRODUCT_ID_SCALE_2)) {
                return device;
            }
        }
        return null;
    }

    /** Check if a Half Decent Scale is attached. */
    public static boolean hasDevice(Context context) {
        return findDevice(context) != null;
    }

    /** Check if we have permission to access the USB scale. */
    public static boolean hasPermission(Context context) {
        UsbDevice device = findDevice(context);
        if (device == null) return false;
        UsbManager manager = (UsbManager) context.getSystemService(Context.USB_SERVICE);
        return manager != null && manager.hasPermission(device);
    }

    /** Request USB scale permission. Shows a system dialog. */
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
        Log.d(TAG, "Requested USB permission for scale " + device.getDeviceName());
    }

    /** Get device info as "vendorId:productId:serialNumber". */
    public static String getDeviceInfo(Context context) {
        UsbDevice device = findDevice(context);
        if (device == null) return "";

        String serial = "";
        try {
            serial = device.getSerialNumber();
            if (serial == null) serial = "";
        } catch (SecurityException e) {
            Log.w(TAG, "Cannot read serial number: " + e.getMessage());
        }

        return device.getVendorId() + ":" + device.getProductId() + ":" + serial;
    }

    // -----------------------------------------------------------------------
    // Connection management
    // -----------------------------------------------------------------------

    /**
     * Open the USB scale and configure CH340 serial (115200 8N1).
     * Starts a background thread for continuous bulk reads.
     */
    public static boolean open(Context context) {
        if (sConnection != null) {
            sLastError = "Already open";
            return false;
        }

        UsbDevice device = findDevice(context);
        if (device == null) {
            sLastError = "No USB scale found";
            return false;
        }

        UsbManager manager = (UsbManager) context.getSystemService(Context.USB_SERVICE);
        if (manager == null || !manager.hasPermission(device)) {
            sLastError = "No USB permission";
            return false;
        }

        Log.d(TAG, "[USB] Opening scale: VID=" + String.format("0x%04X", device.getVendorId())
                + " PID=" + String.format("0x%04X", device.getProductId())
                + " interfaces=" + device.getInterfaceCount());

        // CH340 uses a single vendor-specific interface (class 0xFF) with bulk endpoints.
        // Find the interface that has both bulk IN and OUT.
        UsbInterface dataIface = null;

        for (int i = 0; i < device.getInterfaceCount(); i++) {
            UsbInterface iface = device.getInterface(i);
            Log.d(TAG, "[USB]   Interface " + i + ": class=" + iface.getInterfaceClass()
                    + " subclass=" + iface.getInterfaceSubclass()
                    + " endpoints=" + iface.getEndpointCount());

            if (hasBulkEndpoints(iface)) {
                dataIface = iface;
                Log.d(TAG, "[USB]   Using interface " + i + " (has bulk endpoints)");
                break;
            }
        }

        if (dataIface == null) {
            sLastError = "No suitable USB interface found";
            return false;
        }

        // Find bulk IN and OUT endpoints
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

        // Open the device
        UsbDeviceConnection conn = manager.openDevice(device);
        if (conn == null) {
            sLastError = "UsbManager.openDevice() returned null";
            return false;
        }

        // Claim data interface
        if (!conn.claimInterface(dataIface, true)) {
            sLastError = "Failed to claim data interface";
            conn.close();
            return false;
        }

        // Configure CH340: init, set 115200 baud, 8N1, DTR+RTS
        if (!ch340Init(conn, 115200)) {
            sLastError = "CH340 initialization failed";
            conn.close();
            return false;
        }

        // Store connection state
        sConnection = conn;
        sEndpointIn = epIn;
        sEndpointOut = epOut;
        sDisconnected = false;

        synchronized (sBufferLock) {
            sReadBuffer.reset();
        }

        // Start background read thread
        sReading = true;
        sReadThread = new Thread(() -> {
            Log.d(TAG, "[USB] Scale read thread started");
            byte[] buf = new byte[512];
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
                        consecutiveErrors++;
                        if (consecutiveErrors > 50) {
                            Log.w(TAG, "[USB] Scale read thread: too many errors, assuming disconnect");
                            sDisconnected = true;
                            break;
                        }
                    } else {
                        consecutiveErrors = 0;
                    }
                } catch (Exception e) {
                    Log.e(TAG, "[USB] Scale read thread exception: " + e.getMessage());
                    sDisconnected = true;
                    break;
                }
            }
            Log.d(TAG, "[USB] Scale read thread stopped (disconnected=" + sDisconnected + ")");
        }, "USB-Scale-Read");
        sReadThread.setDaemon(true);
        sReadThread.start();

        sLastError = "";
        Log.d(TAG, "[USB] Scale opened successfully");
        return true;
    }

    /** Write data to the USB scale. */
    public static int write(byte[] data) {
        if (sConnection == null || sEndpointOut == null || sDisconnected) return -1;
        try {
            return sConnection.bulkTransfer(sEndpointOut, data, data.length, 1000);
        } catch (Exception e) {
            Log.e(TAG, "Scale write error: " + e.getMessage());
            return -1;
        }
    }

    /** Read all data accumulated since the last call. */
    public static byte[] readAvailable() {
        synchronized (sBufferLock) {
            if (sReadBuffer.size() == 0) return new byte[0];
            byte[] data = sReadBuffer.toByteArray();
            sReadBuffer.reset();
            return data;
        }
    }

    /** Close the USB scale connection. */
    public static void close() {
        Log.d(TAG, "Closing USB scale connection");
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
                Log.w(TAG, "Error closing scale connection: " + e.getMessage());
            }
            sConnection = null;
        }

        sEndpointIn = null;
        sEndpointOut = null;
        sDisconnected = false;

        synchronized (sBufferLock) {
            sReadBuffer.reset();
        }

        Log.d(TAG, "USB scale closed");
    }

    /** Check if the scale connection is open and active. */
    public static boolean isOpen() {
        return sConnection != null && sReading && !sDisconnected;
    }

    /** Get the last error message. */
    public static String getLastError() {
        return sLastError;
    }

    // -----------------------------------------------------------------------
    // CH340 initialization (vendor-specific control transfers)
    // -----------------------------------------------------------------------

    /**
     * Initialize CH340 chip: read version, serial init, set baud rate, 8N1, DTR+RTS.
     * Based on Linux ch341 kernel driver and usb-serial-for-android Ch34xSerialDriver.
     */
    private static boolean ch340Init(UsbDeviceConnection conn, int baudRate) {
        int result;

        // 1. Read chip version (0xC0 = vendor IN)
        byte[] versionBuf = new byte[8];
        result = conn.controlTransfer(0xC0, CH340_REQ_READ_VERSION, 0, 0, versionBuf, 8, 1000);
        Log.d(TAG, "[USB] CH340 version read: " + result + " bytes"
                + " [" + String.format("0x%02X 0x%02X", versionBuf[0], versionBuf[1]) + "]");

        // 2. Serial init (0x40 = vendor OUT)
        result = conn.controlTransfer(0x40, CH340_REQ_SERIAL_INIT, 0, 0, null, 0, 1000);
        Log.d(TAG, "[USB] CH340 serial init: " + result);

        // 3. Set baud rate
        if (!ch340SetBaudRate(conn, baudRate)) {
            return false;
        }

        // 4. Set 8N1 (8 data bits, no parity, 1 stop bit)
        // LCR value: 0xC0 (enable RX+TX) | 0x03 (8 data bits) = 0xC3
        int lcr = 0xC3;
        result = conn.controlTransfer(0x40, CH340_REQ_WRITE_REG, 0x2518, lcr, null, 0, 1000);
        Log.d(TAG, "[USB] CH340 LCR (8N1): " + result);

        // 5. Set modem control: DTR + RTS active
        // CH340 uses inverted logic: ~(DTR=0x20 | RTS=0x40) = ~0x60 = 0xFF9F
        result = conn.controlTransfer(0x40, CH340_REQ_MODEM_CTRL, 0xFF9F, 0, null, 0, 1000);
        Log.d(TAG, "[USB] CH340 modem ctrl (DTR+RTS): " + result);

        return true;
    }

    /**
     * Set CH340 baud rate via prescaler/divisor registers.
     * Algorithm from Linux ch341 driver / usb-serial-for-android.
     */
    private static boolean ch340SetBaudRate(UsbDeviceConnection conn, int baudRate) {
        long factor;
        long divisor;

        if (baudRate == 921600) {
            divisor = 7;
            factor = 0xF300;
        } else {
            factor = CH340_BAUDBASE_FACTOR / baudRate;
            divisor = CH340_BAUDBASE_DIVMAX;

            while (factor > 0xFFF0 && divisor > 0) {
                factor >>= 3;
                divisor--;
            }

            if (factor > 0xFFF0) {
                Log.e(TAG, "[USB] CH340 unsupported baud rate: " + baudRate);
                return false;
            }

            factor = 0x10000 - factor;
        }

        divisor |= 0x0080;

        int val1 = (int) ((factor & 0xFF00) | divisor);
        int val2 = (int) (factor & 0xFF);

        Log.d(TAG, "[USB] CH340 baud " + baudRate + ": reg 0x1312=" + String.format("0x%04X", val1)
                + " reg 0x0F2C=" + String.format("0x%04X", val2));

        int result = conn.controlTransfer(0x40, CH340_REQ_WRITE_REG, 0x1312, val1, null, 0, 1000);
        Log.d(TAG, "[USB] CH340 baud prescaler: " + result);

        result = conn.controlTransfer(0x40, CH340_REQ_WRITE_REG, 0x0F2C, val2, null, 0, 1000);
        Log.d(TAG, "[USB] CH340 baud divisor: " + result);

        return true;
    }

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

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
