package io.github.kulitorum.decenza_de1;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
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
        // Diagnostic: drop a flag file so we can confirm from the Qt log on the
        // next app start whether this receiver ran at all, independently of
        // whether startActivity() below actually brings the UI up.
        String launchResult = "pending";
        try {
            Intent launch = context.getPackageManager()
                    .getLaunchIntentForPackage(context.getPackageName());
            if (launch != null) {
                launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                context.startActivity(launch);
                launchResult = "startActivity ok";
                Log.i(TAG, "launched updated app after ACTION_MY_PACKAGE_REPLACED");
            } else {
                launchResult = "null launch intent";
                Log.w(TAG, "no launch intent for package " + context.getPackageName());
            }
        } catch (Throwable t) {
            launchResult = "exception: " + t;
            Log.w(TAG, "failed to launch updated app: " + t);
        }
        try {
            File flag = new File(context.getFilesDir(), "auto_launch_fired.txt");
            FileOutputStream out = new FileOutputStream(flag);
            try {
                String line = System.currentTimeMillis() + " " + launchResult + "\n";
                out.write(line.getBytes());
            } finally {
                out.close();
            }
        } catch (IOException e) {
            Log.w(TAG, "failed to write auto_launch_fired.txt: " + e);
        }
    }
}
