package io.github.kulitorum.decenza_de1;

import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;

import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

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

        maybeRequestNotificationPermission();
    }

    // Android 13+ (API 33+) requires runtime POST_NOTIFICATIONS permission
    // for any notification to be shown. We use notifications from
    // UpdateLaunchReceiver to give the user a "Decenza updated — tap to open"
    // affordance after a self-update, matching the legacy Intent(ACTION_VIEW)
    // install flow's "Open" button. Ask once on first launch; if declined,
    // the app still works — the user just relaunches manually after an update.
    private void maybeRequestNotificationPermission() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) return;
        String perm = android.Manifest.permission.POST_NOTIFICATIONS;
        if (ContextCompat.checkSelfPermission(this, perm)
                == PackageManager.PERMISSION_GRANTED) {
            return;
        }
        try {
            ActivityCompat.requestPermissions(this, new String[] { perm }, 4242);
        } catch (Throwable t) {
            Log.w(TAG, "requestPermissions POST_NOTIFICATIONS failed: " + t);
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

    private static boolean isDeadSystemException(Throwable t) {
        while (t != null) {
            if (t.getClass().getName().contains("DeadSystem")) {
                return true;
            }
            t = t.getCause();
        }
        return false;
    }

    private void installJavaCrashHandler() {
        defaultHandler = Thread.getDefaultUncaughtExceptionHandler();

        Thread.setDefaultUncaughtExceptionHandler((thread, throwable) -> {
            // DeadSystemException means Android's Bluetooth system service died
            // (deep sleep, OEM power management, system_server crash).
            // Create a flag file so the C++ side can detect it and trigger BLE
            // recovery (disconnect + reconnect). This fires on any thread — we
            // don't filter by thread name since DeadSystemException only occurs
            // when Android system services die, which always affects BLE.
            // Don't crash the app — the rest of the app is fine, only BLE is dead.
            if (isDeadSystemException(throwable)) {
                Log.w(TAG, "DeadSystemException on thread " + thread.getName()
                        + " — Bluetooth system died, signaling BLE recovery");
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
