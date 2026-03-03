package io.github.kulitorum.decenza_de1;

import android.bluetooth.BluetoothGatt;
import android.util.Log;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.Map;

/**
 * Android-only BLE and GC helper utilities not exposed by Qt.
 *
 * Qt 6 does not expose BluetoothGatt.requestConnectionPriority() or JVM
 * heap utilization tuning through its public API. This class uses reflection
 * on Qt's internal QtBluetoothLE class (for GATT access) and on
 * dalvik.system.VMRuntime (for heap tuning).
 *
 * All reflection is best-effort: if field/method names change between Qt or
 * Android versions the calls silently no-op and log a warning.
 */
public class BleHelper {
    private static final String TAG = "DecenzaBleHelper";

    // Default ART heap utilization target (~0.75). Set higher during shots to
    // defer GC; reset to default when returning to idle.
    private static final float HEAP_UTIL_DEFAULT  = 0.75f;
    private static final float HEAP_UTIL_DEFERRED = 0.95f;

    // -------------------------------------------------------------------------
    // Shot / flowing-operation GC management
    // -------------------------------------------------------------------------

    /**
     * Call when a flowing operation is about to start (espresso preheating,
     * hot water, flush, etc.).
     *
     * 1. Raises the ART heap utilization target to 0.95, which tells the GC
     *    to wait until the heap is 95% full before collecting. This defers
     *    stop-the-world GC pauses during the operation.
     * 2. Runs System.gc() on a background thread so the heap is clean going
     *    into the operation and the next automatic GC is pushed further away.
     */
    public static void onFlowingStarted() {
        setHeapUtilization(HEAP_UTIL_DEFERRED);
        new Thread(() -> {
            System.gc();
            System.runFinalization();
            System.gc();
        }, "DecenzaPreShotGC").start();
        Log.d(TAG, "onFlowingStarted: heap utilization deferred to " + HEAP_UTIL_DEFERRED + ", pre-shot GC scheduled");
    }

    /**
     * Call when a flowing operation ends and the machine returns to idle.
     *
     * 1. Resets the ART heap utilization target to the default (~0.75) so
     *    normal GC behaviour resumes.
     * 2. Runs System.gc() on a background thread to clean up BLE callback
     *    objects accumulated during the operation.
     */
    public static void onFlowingEnded() {
        setHeapUtilization(HEAP_UTIL_DEFAULT);
        new Thread(() -> {
            System.gc();
            System.runFinalization();
        }, "DecenzaPostShotGC").start();
        Log.d(TAG, "onFlowingEnded: heap utilization reset to default, post-shot GC scheduled");
    }

    /**
     * Sets the ART heap utilization target via VMRuntime reflection.
     * This is a hidden API; gracefully no-ops if unavailable.
     */
    private static void setHeapUtilization(float utilization) {
        try {
            Class<?> vmRuntimeClass = Class.forName("dalvik.system.VMRuntime");
            java.lang.reflect.Method getRuntime = vmRuntimeClass.getDeclaredMethod("getRuntime");
            Object vmRuntime = getRuntime.invoke(null);
            java.lang.reflect.Method setUtil = vmRuntimeClass.getDeclaredMethod(
                "setTargetHeapUtilization", float.class);
            setUtil.invoke(vmRuntime, utilization);
        } catch (Exception e) {
            // Hidden API restricted on this Android version — gracefully ignore.
            Log.d(TAG, "setHeapUtilization: unavailable (" + e.getMessage() + ")");
        }
    }

    // -------------------------------------------------------------------------
    // CONNECTION_PRIORITY_HIGH
    // -------------------------------------------------------------------------

    /**
     * Request CONNECTION_PRIORITY_HIGH for the GATT connection to the given
     * MAC address. Call this after QLowEnergyController emits connected().
     *
     * CONNECTION_PRIORITY_HIGH reduces the BLE connection interval from the
     * default ~30-50 ms to 7.5-15 ms, which reduces how long Android GC
     * pauses delay BLE notification delivery and BLE write commands.
     *
     * @param macAddress The Bluetooth MAC address (e.g. "AA:BB:CC:DD:EE:FF")
     * @return true if the request was submitted, false if it could not be made
     */
    public static boolean requestHighConnectionPriority(String macAddress) {
        try {
            // Qt 6 stores all LE controller instances in a static HashMap on
            // org.qtproject.qt.android.bluetooth.QtBluetoothLE.
            Class<?> qtBleClass = Class.forName(
                "org.qtproject.qt.android.bluetooth.QtBluetoothLE");

            Field leMapField = qtBleClass.getDeclaredField("leMap");
            leMapField.setAccessible(true);
            Map<?, ?> leMap = (Map<?, ?>) leMapField.get(null);

            if (leMap == null || leMap.isEmpty()) {
                Log.w(TAG, "requestHighConnectionPriority: Qt leMap is null or empty");
                return false;
            }

            Field gattField = qtBleClass.getDeclaredField("mBluetoothGatt");
            gattField.setAccessible(true);

            for (Object instance : leMap.values()) {
                BluetoothGatt gatt = (BluetoothGatt) gattField.get(instance);
                if (gatt == null) continue;
                if (macAddress.equalsIgnoreCase(gatt.getDevice().getAddress())) {
                    gatt.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH);
                    Log.i(TAG, "Requested CONNECTION_PRIORITY_HIGH for " + macAddress);
                    return true;
                }
            }

            Log.w(TAG, "requestHighConnectionPriority: no GATT found for " + macAddress);
        } catch (ClassNotFoundException e) {
            Log.w(TAG, "requestHighConnectionPriority: Qt internal class not found: " + e.getMessage());
        } catch (NoSuchFieldException e) {
            Log.w(TAG, "requestHighConnectionPriority: Qt internal field not found (Qt version mismatch?): " + e.getMessage());
        } catch (Exception e) {
            Log.w(TAG, "requestHighConnectionPriority: unexpected error: " + e.getMessage());
        }
        return false;
    }
}
