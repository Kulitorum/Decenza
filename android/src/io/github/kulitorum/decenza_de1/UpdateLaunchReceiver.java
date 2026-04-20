package io.github.kulitorum.decenza_de1;

import android.app.ActivityOptions;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;

/**
 * Relaunches Decenza after a self-update.
 *
 * The legacy Intent(ACTION_VIEW) install flow used to provide this via the
 * system package-installer activity's "Open" button. PackageInstaller's session
 * API has no equivalent post-install UI, and the dynamic BroadcastReceiver we
 * register in ApkInstaller cannot handle STATUS_SUCCESS for a self-update
 * because Android kills the old process when it replaces the package on disk,
 * before the status broadcast is delivered.
 *
 * ACTION_MY_PACKAGE_REPLACED is delivered by the system to a manifest-
 * registered receiver running in the NEW process after a self-update, which
 * is exactly the right hook to bring the updated app back to the foreground.
 */
public class UpdateLaunchReceiver extends BroadcastReceiver {
    private static final String TAG = "DecenzaUpdateLaunch";

    @Override
    public void onReceive(Context context, Intent intent) {
        if (!Intent.ACTION_MY_PACKAGE_REPLACED.equals(intent.getAction())) {
            return;
        }
        StringBuilder result = new StringBuilder();
        Intent launch = context.getPackageManager()
                .getLaunchIntentForPackage(context.getPackageName());
        if (launch == null) {
            result.append("null launch intent");
        } else {
            launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);

            // Attempt 1: direct startActivity (known to BAL_BLOCK on Android 15+,
            // but we log the outcome for the record).
            try {
                context.startActivity(launch);
                result.append("direct:ok ");
                Log.i(TAG, "direct startActivity returned without throwing");
            } catch (Throwable t) {
                result.append("direct:ex=").append(t).append(" ");
                Log.w(TAG, "direct startActivity threw: " + t);
            }

            // Attempt 2: PendingIntent + explicit BAL opt-in from both creator
            // and sender. Android 15+ requires the PendingIntent creator to
            // opt in, and the sender's ActivityOptions bundle also needs the
            // BAL-allowed mode. The BAL log from our last test claimed this
            // would still be blocked (resultIfPiCreatorAllowsBal: BAL_BLOCK),
            // but let's confirm empirically.
            try {
                Bundle creatorOptions = null;
                if (Build.VERSION.SDK_INT >= 35) {  // VANILLA_ICE_CREAM (Android 15)
                    ActivityOptions ao = ActivityOptions.makeBasic();
                    ao.setPendingIntentCreatorBackgroundActivityStartMode(
                            ActivityOptions.MODE_BACKGROUND_ACTIVITY_START_ALLOWED);
                    creatorOptions = ao.toBundle();
                }
                int piFlags = PendingIntent.FLAG_UPDATE_CURRENT
                            | PendingIntent.FLAG_IMMUTABLE;
                PendingIntent pi;
                if (creatorOptions != null) {
                    pi = PendingIntent.getActivity(
                            context, 0, launch, piFlags, creatorOptions);
                } else {
                    pi = PendingIntent.getActivity(
                            context, 0, launch, piFlags);
                }
                Bundle sendOptions = null;
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
                    ActivityOptions ao = ActivityOptions.makeBasic();
                    ao.setPendingIntentBackgroundActivityStartMode(
                            ActivityOptions.MODE_BACKGROUND_ACTIVITY_START_ALLOWED);
                    sendOptions = ao.toBundle();
                }
                pi.send(context, 0, null, null, null, null, sendOptions);
                result.append("pi:sent");
                Log.i(TAG, "PendingIntent.send() returned without throwing");
            } catch (Throwable t) {
                result.append("pi:ex=").append(t);
                Log.w(TAG, "PendingIntent path threw: " + t);
            }
        }
        try {
            File flag = new File(context.getFilesDir(), "auto_launch_fired.txt");
            FileOutputStream out = new FileOutputStream(flag);
            try {
                String line = System.currentTimeMillis() + " " + result + "\n";
                out.write(line.getBytes());
            } finally {
                out.close();
            }
        } catch (IOException e) {
            Log.w(TAG, "failed to write auto_launch_fired.txt: " + e);
        }
    }
}
