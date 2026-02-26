package io.github.kulitorum.decenza_de1;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.IBinder;
import android.util.Log;

import androidx.core.app.NotificationCompat;
import androidx.core.app.ServiceCompat;

/**
 * Foreground service that keeps the app alive while connected to the DE1 via BLE.
 * Samsung and other aggressive OEMs kill background apps even with active BLE connections.
 * A foreground service with a persistent notification tells the OS this process is doing
 * user-visible work and should not be killed.
 *
 * Lifecycle is driven by BLE connection state in de1device.cpp:
 * - Started when BLE service discovery completes (DE1 connected)
 * - Stopped when BLE disconnects (user-initiated or connection lost)
 */
public class BleConnectionService extends Service {
    private static final String TAG = "BleConnectionService";
    private static final String CHANNEL_ID = "ble_connection";
    private static final int NOTIFICATION_ID = 1;

    @Override
    public void onCreate() {
        super.onCreate();
        createNotificationChannel();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        Notification notification = buildNotification();

        int foregroundServiceType = 0;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            // API 34+: must specify foreground service type
            foregroundServiceType = ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE;
        }

        ServiceCompat.startForeground(this, NOTIFICATION_ID, notification, foregroundServiceType);
        Log.d(TAG, "Foreground service started");

        // Don't restart if killed — C++ will restart on BLE reconnect
        return START_NOT_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onDestroy() {
        Log.d(TAG, "Foreground service stopped");
        super.onDestroy();
    }

    private void createNotificationChannel() {
        NotificationChannel channel = new NotificationChannel(
            CHANNEL_ID,
            "BLE Connection",
            NotificationManager.IMPORTANCE_LOW  // Silent — no sound or vibration
        );
        channel.setDescription("Shows while connected to the DE1 espresso machine");

        NotificationManager manager = getSystemService(NotificationManager.class);
        if (manager != null) {
            manager.createNotificationChannel(channel);
        }
    }

    private Notification buildNotification() {
        NotificationCompat.Builder builder = new NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("Connected to DE1")
            .setContentText("Maintaining BLE connection")
            .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .setOngoing(true);

        // Tap notification to open the app
        Intent launchIntent = getPackageManager().getLaunchIntentForPackage(getPackageName());
        if (launchIntent != null) {
            PendingIntent pendingIntent = PendingIntent.getActivity(
                this, 0, launchIntent,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE
            );
            builder.setContentIntent(pendingIntent);
        }

        return builder.build();
    }

    // Called from C++ via JNI
    public static void start(Context context) {
        Log.d(TAG, "Starting BLE connection service");
        Intent intent = new Intent(context, BleConnectionService.class);
        try {
            context.startForegroundService(intent);
        } catch (Exception e) {
            // Android 12+ throws ForegroundServiceStartNotAllowedException
            // if app is not in a foreground state. Safe to ignore — the service
            // is a keep-alive optimization, not a functional requirement.
            Log.w(TAG, "Could not start foreground service: " + e.getMessage());
        }
    }

    // Called from C++ via JNI
    public static void stop(Context context) {
        Log.d(TAG, "Stopping BLE connection service");
        Intent intent = new Intent(context, BleConnectionService.class);
        context.stopService(intent);
    }
}
