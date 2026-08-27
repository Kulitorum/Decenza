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

    // onResume/onPause/onStop go through dispatchToQt for the same reason as
    // onNewIntent below: each one calls QtNative.setApplicationState(), which
    // calls the `updateApplicationState` native unguarded (QtNative.java:266,
    // Qt 6.11.2). That is the crash in issues #1239 (v1.7.5), #1511 and #1512
    // (both v1.8.0) — same UnsatisfiedLinkError, different callback, and all
    // three predate the Qt 6.11.2 upgrade.
    @Override
    protected void onResume() {
        dispatchToQt("onResume", () -> super.onResume());
        Log.d(TAG, "=== LIFECYCLE: onResume (app now in foreground) ===");
    }

    @Override
    protected void onPause() {
        Log.d(TAG, "=== LIFECYCLE: onPause (app losing focus) ===");
        dispatchToQt("onPause", () -> super.onPause());
    }

    @Override
    protected void onStop() {
        Log.d(TAG, "=== LIFECYCLE: onStop (app no longer visible) ===");
        dispatchToQt("onStop", () -> super.onStop());
    }

    // NOT wrapped: QtActivityBase.onDestroy() ends in System.exit() on the
    // normal path, and swallowing a failure part-way through that teardown
    // would leave a half-torn-down process alive rather than saving one.
    @Override
    protected void onDestroy() {
        Log.d(TAG, "=== LIFECYCLE: onDestroy (app being destroyed) ===");
        super.onDestroy();
    }

    // Guard for the Qt lifecycle callbacks that jump straight into JNI.
    //
    // QtActivityBase.onNewIntent(), onActivityResult() and
    // onRequestPermissionsResult() call a `public static native` method on
    // QtNative with no guard (qtbase/src/android/jar/src/org/qtproject/qt/
    // android/QtActivityBase.java:369-385, Qt 6.11.2), and onResume/onPause/
    // onStop reach one the same way through QtNative.setApplicationState()
    // (QtNative.java:266). Those natives are registered by the QPA plugin's
    // JNI_OnLoad (qtbase/src/plugins/platforms/android/androidjnimain.cpp:
    // 751-761, :907-926), so before Qt's libraries are loaded the call throws
    // UnsatisfiedLinkError:
    //
    //   No implementation found for void org.qtproject.qt.android.QtNative
    //   .onNewIntent(android.content.Intent)
    //
    // It reaches the main thread's uncaught handler and kills the process
    // (issue #1869, crash on a Teclast P30T right after a DE1 firmware
    // update, three seconds before the app relaunched).
    //
    // Qt guards roughly eight sibling callbacks in the same class against
    // exactly this — onCreate, onResume, dispatchKeyEvent,
    // dispatchGenericMotionEvent, onKeyDown, onKeyUp and friends all test
    // QtNative.getStateDetails().isStarted first (QtActivityBase.java:113,
    // :194, :268, :278, :288, :298), and the platform plugin maintains that
    // flag for the purpose, clearing it once main() returns
    // (androidjnimain.cpp:489). These three callbacks were simply left out.
    //
    // Qt knows this window exists and handles it in exactly one place:
    // QtNative.setActivity() wraps its native call in a catch for this error,
    // commented "this happens ... before Qt native libraries have been
    // loaded" (QtNative.java:71-79). Every other entry point was left bare.
    //
    // What we cannot show is what kept the libraries from being loaded in the
    // crashing process — a load that failed outright is one candidate (we have
    // taken "dlopen failed: library lib_arm64-v8a.so not found" in the field,
    // issues #246/#247/#254), a race against onCreate's load is another, and
    // the sources rule out neither. Do not write either up as settled. The
    // guard does not depend on the answer.
    //
    // We are especially exposed because the activity is android:launchMode=
    // "singleTop" AND carries a USB_DEVICE_ATTACHED intent-filter, so a
    // launcher tap or a USB re-enumeration (the DE1 power-cycle after a
    // firmware flash re-enumerates the tablet's port) both route here instead
    // of starting a fresh activity.
    //
    // Losing the callback is harmless: nothing in Decenza registers a Qt new-
    // intent listener, and a result that arrives with no Qt to receive it had
    // no consumer either. Losing the process is not. Swallow and log.
    private void dispatchToQt(String callback, Runnable body) {
        try {
            body.run();
        } catch (UnsatisfiedLinkError e) {
            Log.w(TAG, "Qt native " + callback + " unavailable (Qt not running) - ignoring: "
                    + e.getMessage());
        }
    }

    @Override
    protected void onNewIntent(Intent intent) {
        dispatchToQt("onNewIntent", () -> super.onNewIntent(intent));
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        dispatchToQt("onActivityResult", () -> super.onActivityResult(requestCode, resultCode, data));
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        dispatchToQt("onRequestPermissionsResult",
                () -> super.onRequestPermissionsResult(requestCode, permissions, grantResults));
    }

    // Catches three closely-related dead-binder cases:
    //   1. DeadObjectException — the common case: a remote Bluetooth binder
    //      we were writing to died (BT toggled off, OEM power policy, driver
    //      hiccup). Covered by `instanceof DeadObjectException`. (Issue #1227)
    //   2. DeadSystemException — system_server died. Subclass of
    //      DeadObjectException so the same `instanceof` covers it.
    //   3. DeadSystemRuntimeException — the API 31+ unchecked variant of
    //      case 2. **Despite the name, it does NOT extend DeadObjectException**
    //      (it extends RuntimeException directly), and is typically thrown
    //      without a DeadObjectException in its cause chain, so the
    //      `instanceof` check misses it. We fall back to a class-name
    //      substring match to cover it. (Was caught by the original
    //      `contains("DeadSystem")` filter in PR #544 / issue #538.)
    // Qt's QtBluetoothLE.executeWriteJob doesn't wrap the
    // BluetoothGatt.writeCharacteristic binder call in try/catch (verified
    // through Qt 6.11.1), so the RuntimeException it raises lands here.
    // Issues #189 (v1.5.0), #538 (v1.4.x DeadSystemRuntimeException),
    // #1227 (v1.7.3 DeadObjectException).
    private static boolean isDeadObjectException(Throwable t) {
        while (t != null) {
            if (t instanceof DeadObjectException
                    || t.getClass().getName().contains("DeadSystem")) {
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
