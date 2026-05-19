package io.github.kulitorum.decenza_de1;

import android.content.Intent;
import android.os.Bundle;
import android.os.DeadObjectException;
import android.util.Log;

import org.qtproject.qt.android.bindings.QtActivity;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

public class DecenzaActivity extends QtActivity {

    private static final String TAG = "DecenzaActivity";
    private Thread.UncaughtExceptionHandler defaultHandler;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        // Install Java crash handler FIRST, before any Qt initialization
        installJavaCrashHandler();

        super.onCreate(savedInstanceState);
        StorageHelper.init(this);
        Log.d(TAG, "=== LIFECYCLE: onCreate ===");

        // Start the shutdown service so onTaskRemoved() will be called
        // when the app is swiped away from recent tasks
        try {
            Intent serviceIntent = new Intent(this, DeviceShutdownService.class);
            startService(serviceIntent);
        } catch (IllegalStateException e) {
            // Android may block startService() if app is considered "in background"
            // This can happen during certain wake scenarios - safe to ignore
            android.util.Log.w("DecenzaActivity", "Could not start shutdown service: " + e.getMessage());
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        Log.d(TAG, "=== LIFECYCLE: onResume (app now in foreground) ===");
    }

    @Override
    protected void onPause() {
        Log.d(TAG, "=== LIFECYCLE: onPause (app losing focus) ===");
        super.onPause();
    }

    @Override
    protected void onStop() {
        Log.d(TAG, "=== LIFECYCLE: onStop (app no longer visible) ===");
        super.onStop();
    }

    @Override
    protected void onDestroy() {
        Log.d(TAG, "=== LIFECYCLE: onDestroy (app being destroyed) ===");
        super.onDestroy();
    }

    // Catches both DeadObjectException (the common case: the Bluetooth GATT
    // binder/process died — toggled off, OEM power policy, driver hiccup) and
    // its subclass DeadSystemException (system_server died — deep sleep, OEM
    // power management). Qt's QtBluetoothLE.executeWriteJob doesn't wrap the
    // BluetoothGatt.writeCharacteristic binder call in try/catch (verified
    // through Qt 6.11.1), so the RuntimeException it raises lands here.
    // Issues #189 (v1.5.0) and #1227 (v1.7.3).
    private static boolean isDeadObjectException(Throwable t) {
        while (t != null) {
            if (t instanceof DeadObjectException) {
                return true;
            }
            t = t.getCause();
        }
        return false;
    }

    private void installJavaCrashHandler() {
        defaultHandler = Thread.getDefaultUncaughtExceptionHandler();

        Thread.setDefaultUncaughtExceptionHandler((thread, throwable) -> {
            // A DeadObjectException means the remote Bluetooth binder we
            // were writing to died (toggled off, OEM power-managed away,
            // GATT proxy unbound) — or, in the DeadSystemException subclass
            // case, system_server itself died. In all of these the BLE
            // handler thread is gone but the UI thread and the rest of the
            // app are fine, so we drop a flag file for the C++ side to
            // observe and trigger reconnect (see main.cpp ble recovery
            // timer), and explicitly do NOT call defaultHandler — the
            // process must stay alive.
            if (isDeadObjectException(throwable)) {
                Log.w(TAG, "DeadObjectException on thread " + thread.getName()
                        + " — BLE binder died, signaling BLE recovery");
                try {
                    File flagFile = new File(getFilesDir(), "ble_dead_system");
                    flagFile.createNewFile();
                    Log.w(TAG, "Wrote BLE recovery flag: " + flagFile.getAbsolutePath());
                } catch (Exception e) {
                    Log.e(TAG, "Failed to write BLE recovery flag: " + e.getMessage());
                }
                // Don't call defaultHandler — keep the app alive.
                // The BLE handler thread is dead but the UI thread and app are fine.
                return;
            }

            try {
                // Get crash log path (same as C++ crash handler)
                File filesDir = getFilesDir();
                File crashLog = new File(filesDir, "crash.log");

                // Get stack trace as string
                StringWriter sw = new StringWriter();
                PrintWriter pw = new PrintWriter(sw);
                throwable.printStackTrace(pw);
                String stackTrace = sw.toString();

                // Build crash report
                StringBuilder report = new StringBuilder();
                SimpleDateFormat sdf = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US);
                report.append("=== JAVA CRASH REPORT ===\n");
                report.append("Time: ").append(sdf.format(new Date())).append("\n");
                report.append("Thread: ").append(thread.getName()).append("\n");
                report.append("Exception: ").append(throwable.getClass().getName()).append("\n");
                report.append("Message: ").append(throwable.getMessage()).append("\n");
                report.append("\n=== STACK TRACE ===\n");
                report.append(stackTrace);
                report.append("\n=== DEVICE INFO ===\n");
                report.append("Android: ").append(android.os.Build.VERSION.RELEASE)
                      .append(" (API ").append(android.os.Build.VERSION.SDK_INT).append(")\n");
                report.append("Device: ").append(android.os.Build.MANUFACTURER)
                      .append(" ").append(android.os.Build.MODEL).append("\n");

                // Write to file
                FileWriter fw = new FileWriter(crashLog);
                fw.write(report.toString());
                fw.close();

                Log.e(TAG, "Java crash logged to: " + crashLog.getAbsolutePath());
                Log.e(TAG, report.toString());

            } catch (Exception e) {
                Log.e(TAG, "Failed to write crash log: " + e.getMessage());
            }

            // Call default handler to show system crash dialog / terminate
            if (defaultHandler != null) {
                defaultHandler.uncaughtException(thread, throwable);
            }
        });

        Log.d(TAG, "Java crash handler installed");
    }
}
