package io.github.kulitorum.decenza_de1;

import android.app.Activity;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageInstaller;
import android.content.pm.PackageManager;
import android.os.Build;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Streams an APK into a PackageInstaller session and commits it. Replaces the
 * legacy ACTION_VIEW intent, which was unreliable on Android 16 / Samsung —
 * the confirmation dialog was being dismissed by a lifecycle flicker, forcing
 * the user to tap "Install" a second time.
 *
 * On API 31+ we request USER_ACTION_NOT_REQUIRED. Silent updates additionally
 * require the UPDATE_PACKAGES_WITHOUT_USER_ACTION privileged permission (not
 * currently declared), so the system always fires STATUS_PENDING_USER_ACTION;
 * we forward its EXTRA_INTENT to startActivity, which is the
 * officially-sanctioned way to surface the confirmation sheet and is not
 * subject to the flicker race. If EXTRA_INTENT is absent (unexpected), the
 * install is aborted and INTERNAL_STATUS_NO_CONFIRM_INTENT is reported.
 *
 * Terminal statuses (SUCCESS / FAILURE*) and internal worker-thread errors are
 * reported back to Qt via {@link #nativeOnInstallStatus}.
 */
public class ApkInstaller {
    private static final String TAG = "ApkInstaller";
    private static final String ACTION_INSTALL_STATUS =
            "io.github.kulitorum.decenza_de1.APK_INSTALL_STATUS";

    // Sentinel codes for errors that happen before PackageInstaller produces a
    // STATUS_*. Values are chosen outside the PackageInstaller.STATUS_* range
    // (STATUS_PENDING_USER_ACTION = -1, STATUS_SUCCESS..STATUS_FAILURE_TIMEOUT = 0..8).
    private static final int INTERNAL_STATUS_CREATE_FAILED   = -100;
    private static final int INTERNAL_STATUS_WRITE_FAILED    = -101;
    private static final int INTERNAL_STATUS_NO_CONFIRM_INTENT = -102;

    // Guards against concurrent sessions. Both the UpdateChecker and ShotServer
    // paths share a single BroadcastReceiver whose callbacks route to one global
    // nativeOnInstallStatus — a second session would corrupt m_installInFlight.
    private static final AtomicBoolean sInstallInFlight = new AtomicBoolean(false);

    /**
     * Implemented in C++. Forwards install status (Android PackageInstaller
     * STATUS_* or INTERNAL_STATUS_*) to {@code UpdateChecker} on the Qt main
     * thread. Registered via {@code QJniEnvironment::registerNativeMethods}
     * when the first {@code UpdateChecker} is constructed.
     */
    static native void nativeOnInstallStatus(int status, String message);

    /**
     * Validates the APK, registers the status receiver, then streams the APK
     * bytes into a PackageInstaller session on a background thread. Returns
     * quickly so the Qt UI thread stays responsive during the (potentially
     * multi-second) session write for large APKs.
     *
     * Returns true on successful dispatch of the worker thread. All subsequent
     * failures (create / write / commit / PackageInstaller terminal statuses)
     * are reported via {@link #nativeOnInstallStatus}.
     */
    public static boolean install(Activity activity, String apkPath) {
        if (activity == null || apkPath == null) {
            Log.e(TAG, "install: null activity or path");
            return false;
        }

        final File apk = new File(apkPath);
        final long apkLen = apk.length();
        if (!apk.exists() || apkLen <= 0) {
            Log.e(TAG, "install: APK missing or empty: " + apkPath);
            return false;
        }

        if (!sInstallInFlight.compareAndSet(false, true)) {
            Log.w(TAG, "install: session already in flight, ignoring duplicate request");
            return false;
        }

        final Context appContext = activity.getApplicationContext();

        // Register before dispatching the worker so STATUS_PENDING_USER_ACTION
        // is never delivered to a non-existent receiver.
        registerStatusReceiver(appContext);

        Thread worker = new Thread(new Runnable() {
            @Override
            public void run() {
                runSessionInstall(appContext, apk, apkLen);
            }
        }, "ApkInstallerWorker");
        worker.start();
        return true;
    }

    private static void runSessionInstall(Context appContext, File apk, long apkLen) {
        PackageInstaller installer =
                appContext.getPackageManager().getPackageInstaller();

        PackageInstaller.SessionParams params = new PackageInstaller.SessionParams(
                PackageInstaller.SessionParams.MODE_FULL_INSTALL);
        params.setAppPackageName(appContext.getPackageName());
        params.setSize(apkLen);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            params.setInstallReason(PackageManager.INSTALL_REASON_USER);
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            params.setRequireUserAction(
                    PackageInstaller.SessionParams.USER_ACTION_NOT_REQUIRED);
        }

        int sessionId;
        try {
            sessionId = installer.createSession(params);
        } catch (IOException e) {
            Log.e(TAG, "install: createSession failed: " + e);
            sInstallInFlight.set(false);
            reportStatus(INTERNAL_STATUS_CREATE_FAILED, e.toString());
            return;
        }

        PackageInstaller.Session session = null;
        try {
            session = installer.openSession(sessionId);

            try (InputStream in = new FileInputStream(apk);
                 OutputStream out = session.openWrite("base.apk", 0, apkLen)) {
                byte[] buf = new byte[1 << 16];
                int n;
                while ((n = in.read(buf)) > 0) {
                    out.write(buf, 0, n);
                }
                session.fsync(out);
            }

            Intent statusIntent = new Intent(ACTION_INSTALL_STATUS)
                    .setPackage(appContext.getPackageName());
            int piFlags = PendingIntent.FLAG_UPDATE_CURRENT;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                piFlags |= PendingIntent.FLAG_MUTABLE;
            }
            PendingIntent pi = PendingIntent.getBroadcast(
                    appContext, sessionId, statusIntent, piFlags);

            session.commit(pi.getIntentSender());
            Log.i(TAG, "install: session " + sessionId + " committed (" + apkLen + " bytes)");
        } catch (Exception e) {
            Log.e(TAG, "install: write/commit failed: " + e);
            if (session != null) {
                try { session.abandon(); } catch (Exception e2) {
                    Log.w(TAG, "session.abandon() failed: " + e2);
                }
            }
            sInstallInFlight.set(false);
            reportStatus(INTERNAL_STATUS_WRITE_FAILED, e.toString());
        } finally {
            if (session != null) {
                session.close();
            }
        }
    }

    private static void reportStatus(int status, String message) {
        try {
            nativeOnInstallStatus(status, message);
        } catch (UnsatisfiedLinkError e) {
            // Native side not yet registered (unit tests, or install()
            // called before UpdateChecker construction). Logged status is
            // the fallback signal.
            Log.w(TAG, "reportStatus: native not registered, status=" + status);
        }
    }

    private static volatile boolean sReceiverRegistered = false;

    private static synchronized void registerStatusReceiver(Context appContext) {
        if (sReceiverRegistered) return;
        IntentFilter filter = new IntentFilter(ACTION_INSTALL_STATUS);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            appContext.registerReceiver(
                    sStatusReceiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            appContext.registerReceiver(sStatusReceiver, filter);
        }
        sReceiverRegistered = true;
    }

    private static final BroadcastReceiver sStatusReceiver = new BroadcastReceiver() {
        @Override
        @SuppressWarnings("deprecation")  // getParcelableExtra(String) on pre-33; typed overload used on 33+
        public void onReceive(Context context, Intent intent) {
            int status = intent.getIntExtra(
                    PackageInstaller.EXTRA_STATUS,
                    PackageInstaller.STATUS_FAILURE);
            String msg = intent.getStringExtra(
                    PackageInstaller.EXTRA_STATUS_MESSAGE);

            if (status == PackageInstaller.STATUS_PENDING_USER_ACTION) {
                Intent confirm;
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                    confirm = intent.getParcelableExtra(Intent.EXTRA_INTENT, Intent.class);
                } else {
                    confirm = intent.getParcelableExtra(Intent.EXTRA_INTENT);
                }
                if (confirm == null) {
                    Log.w(TAG, "install: STATUS_PENDING_USER_ACTION with no EXTRA_INTENT");
                    sInstallInFlight.set(false);
                    reportStatus(INTERNAL_STATUS_NO_CONFIRM_INTENT,
                            "STATUS_PENDING_USER_ACTION delivered with no EXTRA_INTENT");
                    return;
                }
                confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                try {
                    context.getApplicationContext().startActivity(confirm);
                    Log.i(TAG, "install: user confirmation dialog launched");
                } catch (Exception e) {
                    Log.e(TAG, "install: startActivity for confirm failed: " + e);
                    sInstallInFlight.set(false);
                    reportStatus(INTERNAL_STATUS_NO_CONFIRM_INTENT, "failed to launch install dialog: " + e);
                }
                return;
            }

            Log.i(TAG, "install: status=" + status + " msg=" + msg);
            sInstallInFlight.set(false);
            reportStatus(status, msg);
        }
    };
}
