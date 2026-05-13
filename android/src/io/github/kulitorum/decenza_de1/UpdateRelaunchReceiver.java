package io.github.kulitorum.decenza_de1;

import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.provider.Settings;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;

/**
 * Relaunches Decenza after a self-update.
 *
 * On ACTION_MY_PACKAGE_REPLACED, builds the launch intent for this package,
 * tags it with EXTRA_AUTO_RELAUNCH, and calls startActivity().
 *
 * Modern Android (10+) blocks startActivity() from a BroadcastReceiver unless
 * the process holds a BAL exemption. The exemption we rely on here is
 * SYSTEM_ALERT_WINDOW ("Display over other apps") which is declared in the
 * manifest and must be granted by the user. If permission is not granted,
 * startActivity() silently fails (BAL rejection) and the user re-opens the
 * app manually — same as before this code existed.
 *
 * Diagnostic flag file: writes a short line to AppDataLocation/auto_relaunch_fired.txt
 * on every receiver invocation so the next app launch can confirm "yes the
 * receiver did fire" even if BAL blocked the activity start. UpdateChecker
 * reads and clears this file on startup.
 *
 * Prior empirical work (commits 0db30f00 / 208655c8 / 5e19c785, 2026-04-19)
 * established that:
 *   - the plain MY_PACKAGE_REPLACED → startActivity() path is BAL-blocked on
 *     Samsung Android 16 (logcat result code=102);
 *   - PendingIntent with full creator/sender BAL opt-in is also blocked;
 *   - a notification fallback works mechanically but Samsung One UI suppresses
 *     its visibility to a badge count, defeating the UX goal.
 *
 * This implementation is the one path not exhausted by that work: declare
 * SYSTEM_ALERT_WINDOW so the receiver's process holds BAL exemption #13
 * directly (per the Android BAL exemption list), without trying to delegate
 * privilege through a PendingIntent across processes.
 *
 * Also provides {@link #launchSawPermissionSettings(Activity)} as a static
 * utility called via JNI from C++ to open Android Settings deeplinked to
 * this app's SAW page. This lives here (rather than on a separate helper
 * class) because it's part of the same auto-relaunch story.
 */
public class UpdateRelaunchReceiver extends BroadcastReceiver {
    private static final String TAG = "DecenzaAutoRelaunch";

    /**
     * Intent extra set by this receiver before calling startActivity().
     * The launched Activity reads this on creation to record that the most
     * recent launch came through the auto-relaunch path.
     */
    public static final String EXTRA_AUTO_RELAUNCH =
            "io.github.kulitorum.decenza_de1.AUTO_RELAUNCH_AFTER_UPDATE";

    /**
     * Diagnostic flag-file name (lives in {@code Context.getFilesDir()}).
     * Written on every receiver invocation; read and removed on the next
     * Qt main startup by {@code UpdateChecker::readAutoRelaunchDiagnostic()}.
     */
    public static final String FLAG_FILENAME = "auto_relaunch_fired.txt";

    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent == null || !Intent.ACTION_MY_PACKAGE_REPLACED.equals(intent.getAction())) {
            return;
        }

        boolean canDrawOverlays = checkCanDrawOverlays(context);
        String resultSummary;

        try {
            Intent launch = context.getPackageManager()
                    .getLaunchIntentForPackage(context.getPackageName());
            if (launch == null) {
                Log.w(TAG, "no launch intent for " + context.getPackageName());
                resultSummary = "no_launch_intent";
            } else {
                launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK
                              | Intent.FLAG_ACTIVITY_CLEAR_TOP);
                launch.putExtra(EXTRA_AUTO_RELAUNCH, true);

                Log.i(TAG, "receiver fired; canDrawOverlays=" + canDrawOverlays
                        + "; attempting startActivity()");
                context.startActivity(launch);
                Log.i(TAG, "startActivity() returned without throwing"
                        + " (this does NOT confirm the activity actually launched —"
                        + " BAL may still drop the start silently)");
                resultSummary = "started:saw=" + canDrawOverlays;
            }
        } catch (Throwable t) {
            Log.w(TAG, "startActivity() threw: " + t);
            resultSummary = "exception:" + t.getClass().getSimpleName();
        }

        writeFlagFile(context, resultSummary, canDrawOverlays);
    }

    /**
     * Wrapper around {@link Settings#canDrawOverlays(Context)} that compiles
     * cleanly on minSdk 28 (always available) and tolerates surprises in OEM
     * implementations by returning false rather than propagating an exception.
     */
    private static boolean checkCanDrawOverlays(Context context) {
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                return Settings.canDrawOverlays(context);
            }
        } catch (Throwable t) {
            Log.w(TAG, "canDrawOverlays threw: " + t);
        }
        return false;
    }

    /**
     * Persists a one-line diagnostic so the next app launch can determine
     * whether this receiver ran and what it tried, even if BAL blocked the
     * activity launch and we have no foreground UI to log from in real-time.
     *
     * Format: {@code <epoch-millis> result=<summary> saw=<true|false>}
     */
    private static void writeFlagFile(Context context, String resultSummary,
                                      boolean canDrawOverlays) {
        try {
            File flag = new File(context.getFilesDir(), FLAG_FILENAME);
            try (FileOutputStream out = new FileOutputStream(flag)) {
                String line = System.currentTimeMillis()
                        + " result=" + resultSummary
                        + " saw=" + canDrawOverlays
                        + "\n";
                out.write(line.getBytes());
            }
        } catch (IOException e) {
            Log.w(TAG, "failed to write " + FLAG_FILENAME + ": " + e);
        }
    }

    /**
     * Opens Android Settings → "Display over other apps" (Samsung One UI:
     * "Appear on top") deeplinked to the Decenza package. There is no API
     * to grant SYSTEM_ALERT_WINDOW from the app itself — the user must
     * toggle it in Android Settings. We just route them there. Reachable
     * from {@code UpdateChecker::requestAutoRelaunchPermission()} via JNI;
     * presently triggered by the "Open Settings" button in the one-time
     * post-update prompt, but the method is general.
     *
     * Implemented on the Java side rather than reaching into Java from C++
     * because the prior C++ approach (Uri.fromParts via JNI variadic call
     * with three jstring args) produced a URI with empty SSP — root cause
     * appears to be a Qt 6.10 variadic-JNI marshalling issue with multiple
     * jstring args (symptom-confirmed: empty SSP; no minimal repro of the
     * marshalling bug itself). Doing everything in Java means we only have
     * to JNI-call this single static method with one jobject argument,
     * which Qt handles reliably.
     *
     * Threading: invoked from the Qt main thread (not the Android UI
     * thread). Activity-instance accessors used here ({@code getPackageName},
     * {@code startActivity}) are documented as thread-safe; the call site
     * matches the pattern already used by {@code ApkInstaller} elsewhere
     * in this package.
     *
     * Behaviour: if {@code activity} is null, a warning is logged and no
     * Intent is started. Any exception thrown by Settings (e.g. on an
     * OEM ROM that has stripped MANAGE_OVERLAY_PERMISSION) is caught and
     * logged; the caller has no way to detect the failure other than
     * observing that the user never grants the permission.
     */
    public static void launchSawPermissionSettings(Activity activity) {
        if (activity == null) {
            Log.w(TAG, "launchSawPermissionSettings: null activity");
            return;
        }
        try {
            // Called from an Activity context — no FLAG_ACTIVITY_NEW_TASK
            // needed. Forcing NEW_TASK has been observed to break the
            // back-navigation return path on some OEMs (Settings ends up
            // in a separate task and Back goes to home instead of Decenza).
            Uri uri = Uri.fromParts("package", activity.getPackageName(), null);
            Intent intent = new Intent(
                    Settings.ACTION_MANAGE_OVERLAY_PERMISSION, uri);
            activity.startActivity(intent);
            Log.i(TAG, "launchSawPermissionSettings: started for "
                    + activity.getPackageName());
        } catch (Throwable t) {
            Log.w(TAG, "launchSawPermissionSettings failed: " + t);
        }
    }
}
