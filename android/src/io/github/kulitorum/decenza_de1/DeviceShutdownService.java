package io.github.kulitorum.decenza_de1;

import android.app.Service;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.BluetoothStatusCodes;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Build;
import android.os.IBinder;
import android.util.Log;

import java.util.UUID;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/**
 * Service that handles device shutdown when the app is removed from recent tasks.
 * Uses Android's native BLE API to send sleep commands to DE1 and scales.
 */
public class DeviceShutdownService extends Service {
    private static final String TAG = "DeviceShutdownService";
    private static final String PREFS_NAME = "DeviceAddresses";
    private static final String KEY_DE1_ADDRESS = "de1_address";
    private static final String KEY_SCALE_ADDRESS = "scale_address";
    private static final String KEY_SCALE_TYPE = "scale_type";

    // DE1 BLE UUIDs
    private static final UUID DE1_SERVICE_UUID = UUID.fromString("0000A000-0000-1000-8000-00805F9B34FB");
    private static final UUID DE1_REQUESTED_STATE_UUID = UUID.fromString("0000A002-0000-1000-8000-00805F9B34FB");
    private static final byte DE1_STATE_SLEEP = 0x00;

    private BluetoothAdapter mBluetoothAdapter;

    @Override
    public void onCreate() {
        super.onCreate();
        BluetoothManager bluetoothManager = (BluetoothManager) getSystemService(Context.BLUETOOTH_SERVICE);
        if (bluetoothManager != null) {
            mBluetoothAdapter = bluetoothManager.getAdapter();
        }
        DiagnosticLog.d("App", TAG, "DeviceShutdownService created");
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        // Service should not restart if killed
        return START_NOT_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onTaskRemoved(Intent rootIntent) {
        DiagnosticLog.d("App", TAG, "onTaskRemoved - app was swiped away, sending sleep commands");

        // Get stored device addresses
        SharedPreferences prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE);
        String de1Address = prefs.getString(KEY_DE1_ADDRESS, null);
        String scaleAddress = prefs.getString(KEY_SCALE_ADDRESS, null);

        // Send sleep command to DE1 if we have its address
        if (de1Address != null && !de1Address.isEmpty()) {
            DiagnosticLog.d("App", TAG, "Sending sleep to DE1: " + de1Address);
            sendDe1Sleep(de1Address);
        }

        // For scales, we could add similar logic, but it's less critical
        // The DE1 staying on is the main concern

        super.onTaskRemoved(rootIntent);

        // Stop the service
        stopSelf();
    }

    @SuppressWarnings("deprecation")
    private void sendDe1Sleep(String address) {
        if (mBluetoothAdapter == null) {
            DiagnosticLog.e("App", TAG, "Bluetooth not available");
            return;
        }

        try {
            BluetoothDevice device = mBluetoothAdapter.getRemoteDevice(address);
            if (device == null) {
                DiagnosticLog.e("App", TAG, "Device not found: " + address);
                return;
            }

            // Use a latch to wait for the write to complete (with timeout)
            final CountDownLatch latch = new CountDownLatch(1);
            final boolean[] success = {false};

            BluetoothGattCallback callback = new BluetoothGattCallback() {
                private BluetoothGatt mGatt;

                @Override
                public void onConnectionStateChange(BluetoothGatt gatt, int status, int newState) {
                    mGatt = gatt;
                    if (newState == BluetoothProfile.STATE_CONNECTED) {
                        DiagnosticLog.d("App", TAG, "Connected to DE1, discovering services");
                        gatt.discoverServices();
                    } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                        DiagnosticLog.d("App", TAG, "Disconnected from DE1");
                        gatt.close();
                        latch.countDown();
                    }
                }

                @Override
                public void onServicesDiscovered(BluetoothGatt gatt, int status) {
                    if (status == BluetoothGatt.GATT_SUCCESS) {
                        DiagnosticLog.d("App", TAG, "Services discovered, sending sleep command");
                        BluetoothGattService service = gatt.getService(DE1_SERVICE_UUID);
                        if (service != null) {
                            BluetoothGattCharacteristic characteristic =
                                service.getCharacteristic(DE1_REQUESTED_STATE_UUID);
                            if (characteristic != null) {
                                boolean writeResult;
                                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                                    int result = gatt.writeCharacteristic(characteristic,
                                        new byte[]{DE1_STATE_SLEEP},
                                        BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);
                                    writeResult = (result == BluetoothStatusCodes.SUCCESS);
                                } else {
                                    characteristic.setValue(new byte[]{DE1_STATE_SLEEP});
                                    characteristic.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);
                                    writeResult = gatt.writeCharacteristic(characteristic);
                                }
                                DiagnosticLog.d("App", TAG, "Write initiated: " + writeResult);
                            } else {
                                DiagnosticLog.e("App", TAG, "RequestedState characteristic not found");
                                gatt.disconnect();
                            }
                        } else {
                            DiagnosticLog.e("App", TAG, "DE1 service not found");
                            gatt.disconnect();
                        }
                    } else {
                        DiagnosticLog.e("App", TAG, "Service discovery failed: " + status);
                        gatt.disconnect();
                    }
                }

                @Override
                public void onCharacteristicWrite(BluetoothGatt gatt,
                                                  BluetoothGattCharacteristic characteristic,
                                                  int status) {
                    if (status == BluetoothGatt.GATT_SUCCESS) {
                        DiagnosticLog.d("App", TAG, "Sleep command written successfully");
                        success[0] = true;
                    } else {
                        DiagnosticLog.e("App", TAG, "Failed to write sleep command: " + status);
                    }
                    gatt.disconnect();
                }
            };

            // Connect with autoConnect=false for faster connection
            BluetoothGatt gatt = device.connectGatt(this, false, callback);

            // Wait up to 5 seconds for the operation to complete
            boolean completed = latch.await(5, TimeUnit.SECONDS);
            if (!completed) {
                DiagnosticLog.w("App", TAG, "Timeout waiting for DE1 sleep command");
                if (gatt != null) {
                    gatt.disconnect();
                    gatt.close();
                }
            } else if (success[0]) {
                DiagnosticLog.d("App", TAG, "DE1 sleep command completed successfully");
            }

        } catch (Exception e) {
            DiagnosticLog.e("App", TAG, "Error sending sleep to DE1: " + e.getMessage(), e);
        }
    }

    // Static methods to store device addresses (called from C++ via JNI)
    public static void setDe1Address(Context context, String address) {
        DiagnosticLog.d("App", TAG, "Storing DE1 address: " + address);
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, MODE_PRIVATE);
        prefs.edit().putString(KEY_DE1_ADDRESS, address).apply();
    }

    public static void clearDe1Address(Context context) {
        DiagnosticLog.d("App", TAG, "Clearing DE1 address");
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, MODE_PRIVATE);
        prefs.edit().remove(KEY_DE1_ADDRESS).apply();
    }

    public static void setScaleAddress(Context context, String address, String scaleType) {
        DiagnosticLog.d("App", TAG, "Storing scale address: " + address + " type: " + scaleType);
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, MODE_PRIVATE);
        prefs.edit()
            .putString(KEY_SCALE_ADDRESS, address)
            .putString(KEY_SCALE_TYPE, scaleType)
            .apply();
    }

    public static void clearScaleAddress(Context context) {
        DiagnosticLog.d("App", TAG, "Clearing scale address");
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, MODE_PRIVATE);
        prefs.edit()
            .remove(KEY_SCALE_ADDRESS)
            .remove(KEY_SCALE_TYPE)
            .apply();
    }
}
