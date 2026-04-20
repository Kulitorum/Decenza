package io.github.kulitorum.decenza_de1;

import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.util.Log;

import androidx.core.app.NotificationCompat;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;

/**
 * Posts a "Decenza updated — tap to open" notification after a self-update,
 * restoring the UX of the legacy Intent(ACTION_VIEW) install flow, where the
 * system package-installer activity provided an "Open" button.
 *
 * We previously tried to start the main activity directly from this receiver.
 * Target SDK 36 / Android 15+ BAL (Background Activity Launch) rules block
 * that, even with PendingIntent + full creator/sender opt-in — the creator
 * opt-in is rejected because this receiver's process is already in RECEIVER
 * state with no visible window when we run, so we have no BAL privilege to
 * grant. A tap on a notification, by contrast, is a foreground-initiated
 * start, so its contentIntent PendingIntent is allowed to launch the activity.
 */
public class UpdateLaunchReceiver extends BroadcastReceiver {
    private static final String TAG = "DecenzaUpdateLaunch";
    private static final String CHANNEL_ID = "decenza_update";
    private static final int NOTIFICATION_ID = 1001;

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
            Log.w(TAG, "no launch intent for package " + context.getPackageName());
        } else {
            launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            try {
                postUpdatedNotification(context, launch);
                result.append("notif:posted");
                Log.i(TAG, "posted update-complete notification");
            } catch (Throwable t) {
                result.append("notif:ex=").append(t);
                Log.w(TAG, "failed to post update-complete notification: " + t);
            }
        }
        // Diagnostic: flag file + log on next startup lets us confirm the
        // receiver ran. Harmless if permission prompts block the notification;
        // the flag still tells us whether this code executed.
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

    private static void postUpdatedNotification(Context context, Intent launch) {
        NotificationManager nm =
                (NotificationManager) context.getSystemService(Context.NOTIFICATION_SERVICE);
        if (nm == null) return;

        // Android 8+ requires a channel. Creating the channel repeatedly is
        // idempotent, so we ensure it exists here rather than at app startup
        // (where we may never have a chance to run for a fresh install's first
        // self-update).
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(
                    CHANNEL_ID,
                    "Updates",
                    NotificationManager.IMPORTANCE_DEFAULT);
            channel.setDescription("Notifies you when an in-app update has installed.");
            nm.createNotificationChannel(channel);
        }

        PendingIntent contentIntent = PendingIntent.getActivity(
                context, 0, launch,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);

        NotificationCompat.Builder builder =
                new NotificationCompat.Builder(context, CHANNEL_ID)
                        .setSmallIcon(android.R.drawable.stat_sys_download_done)
                        .setContentTitle("Decenza updated")
                        .setContentText("Tap to open.")
                        .setPriority(NotificationCompat.PRIORITY_DEFAULT)
                        .setCategory(NotificationCompat.CATEGORY_STATUS)
                        .setContentIntent(contentIntent)
                        .setAutoCancel(true);

        nm.notify(NOTIFICATION_ID, builder.build());
    }
}
