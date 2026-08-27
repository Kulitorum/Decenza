package io.github.kulitorum.decenza_de1;

import android.content.ComponentName;
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

        retargetAliasLaunch();

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

    // Launcher Mode (Settings, `app/launcherMode`) enables the LauncherAlias
    // activity-alias so Decenza can be the tablet's home screen. Qt 6.11
    // added a branch to QtActivityBase.onCreate that skips loading the Qt
    // libraries outright when the activity was launched through an alias:
    //
    //   if (isLaunchedAsAlias()) {
    //       Log.d(TAG, "Starting an alias-activity, skipping loading of the Qt libraries.");
    //   } else { ...loadQtLibraries()... }
    //
    // and isLaunchedAsAlias() is a plain name comparison of the intent's
    // component against this class (QtActivityBase.java:169-178). So a HOME
    // launch through LauncherAlias produces an activity with no Qt behind it,
    // and the first lifecycle callback into QtNative dies with
    // UnsatisfiedLinkError. That is issues #1239, #1511, #1512 and #1869.
    //
    // The branch is new: absent in v6.8.0 and v6.10.0, present in v6.11.1
    // (checked against code.qt.io). Decenza moved to 6.11.1 shortly before the
    // first of those reports, and Launcher Mode had been working since the
    // alias got its android.app.lib_name metadata (PR #287).
    //
    // Point the intent at the real activity so the comparison matches and Qt
    // loads normally. QtLoader reads android.app.lib_name from
    // getComponentName() rather than from the intent (QtLoader.java:95-96,
    // :525), which setIntent() does not affect — and DecenzaActivity carries
    // that metadata too, so library resolution is unchanged either way.
    private void retargetAliasLaunch() {
        Intent intent = getIntent();
        if (intent == null || intent.getComponent() == null)
            return;
        if (intent.getComponent().getClassName().equals(getClass().getName()))
            return;

        Log.i(TAG, "Launched via alias " + intent.getComponent().getClassName()
                + " - retargeting to " + getClass().getName() + " so Qt loads its libraries");
        intent.setComponent(new ComponentName(this, getClass()));
        setIntent(intent);
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

    // Qt's onNewIntent/onActivityResult/onRequestPermissionsResult call a
    // native on QtNative with no guard (QtActivityBase.java:369-385, Qt
    // 6.11.2), and those natives are registered by the Android QPA plugin's
    // JNI_OnLoad (androidjnimain.cpp:752-762, :907-926). Reached with Qt's
    // libraries not loaded, the call throws UnsatisfiedLinkError, which lands
    // on the main thread's uncaught handler and kills the process.
    //
    // Qt knows this window exists and handles it in exactly one place:
    // QtNative.setActivity() wraps its native call in a catch for this error,
    // commented "this happens ... before Qt native libraries have been
    // loaded" (QtNative.java:71-79). Every other entry point is bare.
    //
    // Scope note. The known way to reach that state in this app was the
    // alias launch retargeted in onCreate() above, and that is fixed at
    // source rather than caught here. This guard covers only the three
    // callbacks where dropping the call costs nothing: no Qt new-intent
    // listener is registered anywhere in Decenza, and a result delivered
    // with no Qt to receive it had no consumer either.
    //
    // The lifecycle callbacks are deliberately NOT wrapped, though they can
    // throw the same error (onResume/onPause/onStop reach a bare native
    // through QtNative.setApplicationState(), QtNative.java:274 — that is the
    // crash in #1239, #1511 and #1512, all three in onResume). Swallowing
    // there would keep an activity alive that has no Qt behind it: no
    // startNativeApplication(), so no content view, so a blank window that
    // cannot recover — and in Launcher Mode Decenza IS the home screen, so
    // there is nowhere to escape to while the DE1 runs unattended. A process
    // that dies and is restarted by Android is the better failure. Same
    // reasoning for onDestroy, whose QtNative.terminateQtNativeApplication()
    // precedes the System.exit(0) that ends it (QtActivityBase.java:210-223):
    // catching there would skip the exit and strand the process.
    private void dispatchToQt(String callback, Runnable body) {
        try {
            body.run();
        } catch (UnsatisfiedLinkError e) {
            // "(Qt not loaded)" is an inference from the exception type, not a
            // confirmed diagnosis — this catch cannot tell which native was
            // missing or why.
            Log.w(TAG, "Qt native " + callback + " unavailable (Qt not loaded) - ignoring: "
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
