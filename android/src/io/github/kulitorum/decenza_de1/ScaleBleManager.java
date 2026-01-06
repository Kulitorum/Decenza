package io.github.kulitorum.decenza_de1;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.content.Context;
import android.util.Log;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

import java.util.HashMap;
import java.util.Map;
import java.util.UUID;

import no.nordicsemi.android.ble.BleManager;
import no.nordicsemi.android.ble.callback.DataReceivedCallback;
import no.nordicsemi.android.ble.callback.DataSentCallback;
import no.nordicsemi.android.ble.data.Data;

/**
 * Scale BLE Manager using Nordic's battle-tested BLE library.
 * Handles connection, service discovery, notifications, and writes properly.
 */
public class ScaleBleManager extends BleManager {
    private static final String TAG = "ScaleBleManager";

    private volatile long mNativePtr;
    private BluetoothGatt mGatt;
    private BluetoothAdapter mBluetoothAdapter;

    // Cache of discovered characteristics by UUID string
    private final Map<String, BluetoothGattCharacteristic> mCharacteristics = new HashMap<>();

    // Native callback methods (implemented in C++)
    private native void nativeOnConnected(long ptr);
    private native void nativeOnDisconnected(long ptr);
    private native void nativeOnServiceDiscovered(long ptr, String serviceUuid);
    private native void nativeOnServicesDiscoveryFinished(long ptr);
    private native void nativeOnCharacteristicDiscovered(long ptr, String serviceUuid,
                                                          String charUuid, int properties);
    private native void nativeOnCharacteristicsDiscoveryFinished(long ptr, String serviceUuid);
    private native void nativeOnCharacteristicChanged(long ptr, String charUuid, byte[] value);
    private native void nativeOnCharacteristicRead(long ptr, String charUuid, byte[] value);
    private native void nativeOnCharacteristicWritten(long ptr, String charUuid);
    private native void nativeOnNotificationsEnabled(long ptr, String charUuid);
    private native void nativeOnError(long ptr, String message);

    public ScaleBleManager(@NonNull Context context, long nativePtr) {
        super(context);
        mNativePtr = nativePtr;

        BluetoothManager bluetoothManager = (BluetoothManager) context.getSystemService(Context.BLUETOOTH_SERVICE);
        if (bluetoothManager != null) {
            mBluetoothAdapter = bluetoothManager.getAdapter();
        }
        Log.d(TAG, "ScaleBleManager created with Nordic BLE library");
    }

    /**
     * Called from C++ destructor to invalidate the native pointer.
     */
    public void release() {
        Log.d(TAG, "release() called - invalidating native pointer");
        mNativePtr = 0;
        disconnect().enqueue();
    }

    public void connectToDevice(@NonNull String address) {
        if (mBluetoothAdapter == null) {
            if (mNativePtr != 0) nativeOnError(mNativePtr, "Bluetooth not available");
            return;
        }

        BluetoothDevice device = mBluetoothAdapter.getRemoteDevice(address);
        if (device == null) {
            if (mNativePtr != 0) nativeOnError(mNativePtr, "Device not found: " + address);
            return;
        }

        Log.d(TAG, "Connecting to: " + address + " device=" + device.getName() + " type=" + device.getType());

        try {
            connect(device)
                .retry(3, 200)
                .useAutoConnect(false)
                .timeout(10000)
                .done(dev -> {
                    Log.d(TAG, "Connected to: " + dev.getAddress());
                    if (mNativePtr != 0) nativeOnConnected(mNativePtr);
                })
                .fail((dev, status) -> {
                    Log.e(TAG, "Connection failed: status=" + status + " device=" + (dev != null ? dev.getAddress() : "null"));
                    if (mNativePtr != 0) nativeOnError(mNativePtr, "Connection failed: " + status);
                })
                .enqueue();
        } catch (Exception e) {
            Log.e(TAG, "Exception during connect: " + e.getMessage(), e);
            if (mNativePtr != 0) nativeOnError(mNativePtr, "Connect exception: " + e.getMessage());
        }
    }

    public void disconnectDevice() {
        disconnect().enqueue();
    }

    public void discoverServices() {
        // Nordic library discovers services automatically after connection
        // The services are available in isRequiredServiceSupported() callback
        // We just need to notify C++ that discovery is done
        Log.d(TAG, "discoverServices() - already done by Nordic library");
    }

    public void enableNotifications(String serviceUuid, String charUuid) {
        BluetoothGattCharacteristic characteristic = findCharacteristic(serviceUuid, charUuid);
        if (characteristic == null) {
            Log.e(TAG, "Characteristic not found for notifications: " + charUuid);
            if (mNativePtr != 0) nativeOnError(mNativePtr, "Characteristic not found: " + charUuid);
            return;
        }

        Log.d(TAG, "Enabling notifications for: " + charUuid);

        setNotificationCallback(characteristic)
            .with((device, data) -> {
                if (mNativePtr != 0) {
                    nativeOnCharacteristicChanged(mNativePtr, charUuid, data.getValue());
                }
            });

        enableNotifications(characteristic)
            .done(device -> {
                Log.d(TAG, "Notifications enabled for: " + charUuid);
                if (mNativePtr != 0) nativeOnNotificationsEnabled(mNativePtr, charUuid);
            })
            .fail((device, status) -> {
                // Nordic library keeps notification subscription even if CCCD write fails
                Log.w(TAG, "CCCD write failed but notifications may still work: " + status);
                if (mNativePtr != 0) nativeOnNotificationsEnabled(mNativePtr, charUuid);
            })
            .enqueue();
    }

    public void writeCharacteristic(String serviceUuid, String charUuid, byte[] data, int writeType) {
        BluetoothGattCharacteristic characteristic = findCharacteristic(serviceUuid, charUuid);
        if (characteristic == null) {
            Log.e(TAG, "Characteristic not found for write: " + charUuid);
            if (mNativePtr != 0) nativeOnError(mNativePtr, "Characteristic not found: " + charUuid);
            return;
        }

        // Map writeType: 1 = NO_RESPONSE, 2 = DEFAULT (with response)
        // This matches de1app's ble_write_type_no_response=1, ble_write_type_default=2
        int androidWriteType = (writeType == 1)
            ? BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
            : BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT;

        Log.d(TAG, "Writing to: " + charUuid + " data=" + bytesToHex(data) +
              " writeType=" + (writeType == 1 ? "NO_RESPONSE" : "DEFAULT"));

        writeCharacteristic(characteristic, data, androidWriteType)
            .done(device -> {
                Log.d(TAG, "Write successful: " + charUuid);
                if (mNativePtr != 0) nativeOnCharacteristicWritten(mNativePtr, charUuid);
            })
            .fail((device, status) -> {
                Log.w(TAG, "Write failed: " + charUuid + " status=" + status);
                // Don't report error for write failures - some scales are picky
            })
            .enqueue();
    }

    public void readCharacteristic(String serviceUuid, String charUuid) {
        BluetoothGattCharacteristic characteristic = findCharacteristic(serviceUuid, charUuid);
        if (characteristic == null) {
            Log.e(TAG, "Characteristic not found for read: " + charUuid);
            if (mNativePtr != 0) nativeOnError(mNativePtr, "Characteristic not found: " + charUuid);
            return;
        }

        readCharacteristic(characteristic)
            .with((device, data) -> {
                if (mNativePtr != 0) {
                    nativeOnCharacteristicRead(mNativePtr, charUuid, data.getValue());
                }
            })
            .fail((device, status) -> {
                Log.e(TAG, "Read failed: " + charUuid);
                if (mNativePtr != 0) nativeOnError(mNativePtr, "Read failed: " + charUuid);
            })
            .enqueue();
    }

    @Nullable
    private BluetoothGattCharacteristic findCharacteristic(String serviceUuid, String charUuid) {
        // Check cache first
        String key = serviceUuid + "/" + charUuid;
        if (mCharacteristics.containsKey(key)) {
            return mCharacteristics.get(key);
        }

        // Look up in GATT
        if (mGatt == null) return null;

        BluetoothGattService service = mGatt.getService(UUID.fromString(serviceUuid));
        if (service == null) return null;

        BluetoothGattCharacteristic characteristic = service.getCharacteristic(UUID.fromString(charUuid));
        if (characteristic != null) {
            mCharacteristics.put(key, characteristic);
        }
        return characteristic;
    }

    // ========== BleManager callbacks ==========

    @Override
    protected boolean isRequiredServiceSupported(@NonNull BluetoothGatt gatt) {
        mGatt = gatt;
        mCharacteristics.clear();

        Log.d(TAG, "Services discovered, notifying C++");

        // Notify C++ about all discovered services and characteristics
        for (BluetoothGattService service : gatt.getServices()) {
            String serviceUuid = service.getUuid().toString();

            if (mNativePtr != 0) {
                nativeOnServiceDiscovered(mNativePtr, serviceUuid);
            }

            for (BluetoothGattCharacteristic c : service.getCharacteristics()) {
                String charUuid = c.getUuid().toString();
                int props = c.getProperties();

                // Cache it
                mCharacteristics.put(serviceUuid + "/" + charUuid, c);

                if (mNativePtr != 0) {
                    nativeOnCharacteristicDiscovered(mNativePtr, serviceUuid, charUuid, props);
                }
            }

            if (mNativePtr != 0) {
                nativeOnCharacteristicsDiscoveryFinished(mNativePtr, serviceUuid);
            }
        }

        if (mNativePtr != 0) {
            nativeOnServicesDiscoveryFinished(mNativePtr);
        }

        // Return true to indicate we found what we need (we accept any scale)
        return true;
    }

    @Override
    protected void onServicesInvalidated() {
        Log.d(TAG, "Services invalidated (disconnected)");
        mGatt = null;
        mCharacteristics.clear();

        if (mNativePtr != 0) {
            nativeOnDisconnected(mNativePtr);
        }
    }

    @Override
    protected void initialize() {
        Log.d(TAG, "initialize() - device ready for operations");
        // Don't do anything here - let C++ control the initialization sequence
    }

    @Override
    public void log(int priority, @NonNull String message) {
        // Only log warnings and errors to reduce logcat spam
        if (priority >= Log.WARN) {
            Log.println(priority, TAG, message);
        }
    }

    // Helper to convert bytes to hex for logging
    private static String bytesToHex(byte[] bytes) {
        if (bytes == null) return "null";
        StringBuilder sb = new StringBuilder();
        for (byte b : bytes) {
            sb.append(String.format("%02x ", b));
        }
        return sb.toString().trim();
    }
}
