package io.github.kulitorum.decenza_de1;

import android.app.PendingIntent;
import android.appwidget.AppWidgetManager;
import android.appwidget.AppWidgetProvider;
import android.content.Context;
import android.content.Intent;
import android.util.Log;
import android.widget.RemoteViews;

import org.json.JSONObject;

import java.time.OffsetDateTime;
import java.time.Duration;
import java.util.Locale;

/**
 * Home Screen widget showing DE1 machine phase, temperature-vs-target, and the
 * last-shot summary. Renders purely from the snapshot the Qt app writes to
 * SharedPreferences (see MachineStatusWidget / src/widget/widgetsharedkeys.h).
 * Read-only: tapping anywhere launches the app, no machine control.
 */
public class MachineStatusWidgetProvider extends AppWidgetProvider {
    private static final String TAG = "MachineStatusWidget";

    // Snapshot older than this (while still "connected") is annotated stale.
    private static final long FRESH_WINDOW_MIN = 3;

    @Override
    public void onUpdate(Context context, AppWidgetManager mgr, int[] ids) {
        renderAll(context, mgr, ids);
    }

    static void renderAll(Context context, AppWidgetManager mgr, int[] ids) {
        for (int id : ids) {
            RemoteViews views = buildViews(context);
            mgr.updateAppWidget(id, views);
        }
    }

    private static RemoteViews buildViews(Context context) {
        RemoteViews v = new RemoteViews(context.getPackageName(),
                R.layout.machine_status_widget);

        // Tap anywhere → launch the app (no control actions).
        Intent launch = new Intent(context, DecenzaActivity.class);
        launch.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK
                | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        PendingIntent pi = PendingIntent.getActivity(context, 0, launch,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        v.setOnClickPendingIntent(R.id.widget_root, pi);

        String json = context
                .getSharedPreferences(MachineStatusWidget.PREFS_NAME,
                        Context.MODE_PRIVATE)
                .getString(MachineStatusWidget.PREFS_KEY, null);

        if (json == null || json.isEmpty()) {
            applyDisconnected(v);
            return v;
        }

        try {
            JSONObject o = new JSONObject(json);
            boolean connected = o.optBoolean("connected", false);
            if (!connected) {
                applyDisconnected(v);
                return v;
            }

            String phase = o.optString("phase", "");
            double tempC = o.optDouble("temperatureC", Double.NaN);
            double targetC = o.optDouble("targetTemperatureC", Double.NaN);
            double steamC = o.optDouble("steamTemperatureC", Double.NaN);

            v.setTextViewText(R.id.widget_phase, phaseLabel(phase));
            v.setTextViewText(R.id.widget_temp,
                    tempLine(phase, tempC, targetC, steamC));
            v.setTextViewText(R.id.widget_last_shot, lastShotLine(o));
            v.setTextViewText(R.id.widget_status, stalenessLine(o));
        } catch (Exception e) {
            Log.w(TAG, "Snapshot parse failed: " + e.getMessage());
            applyDisconnected(v);
        }
        return v;
    }

    private static void applyDisconnected(RemoteViews v) {
        v.setTextViewText(R.id.widget_phase, "Disconnected");
        v.setTextViewText(R.id.widget_temp, "");
        v.setTextViewText(R.id.widget_last_shot, "");
        v.setTextViewText(R.id.widget_status, "Tap to open");
    }

    private static String phaseLabel(String phase) {
        if (phase == null || phase.isEmpty()) return "Decenza";
        // Insert spaces in CamelCase phase names (e.g. HotWater -> Hot Water).
        return phase.replaceAll("(?<=[a-z])(?=[A-Z])", " ");
    }

    private static String tempLine(String phase, double tempC,
                                   double targetC, double steamC) {
        if ("Steaming".equals(phase) && !Double.isNaN(steamC)) {
            return Math.round(steamC) + " °C steam";
        }
        if ("Heating".equals(phase)
                && !Double.isNaN(tempC) && !Double.isNaN(targetC)) {
            return "Heating " + Math.round(tempC) + " → "
                    + Math.round(targetC) + " °C";
        }
        if (Double.isNaN(tempC)) return "";
        if ("Ready".equals(phase)) {
            return "Ready · " + Math.round(tempC) + " °C";
        }
        return Math.round(tempC) + " °C";
    }

    private static String lastShotLine(JSONObject o) {
        JSONObject shot = o.optJSONObject("lastShot");
        if (shot == null) return "";
        double yield = shot.optDouble("yieldG", Double.NaN);
        double dur = shot.optDouble("durationSec", Double.NaN);
        if (Double.isNaN(yield) || Double.isNaN(dur)) return "";
        String line = String.format(Locale.US, "Last: %.1fg · %ds",
                yield, Math.round(dur));
        String badge = shot.optString("qualityBadge", "");
        if (!badge.isEmpty()) line += " · " + badge;
        return line;
    }

    private static String stalenessLine(JSONObject o) {
        String captured = o.optString("capturedAt", "");
        if (captured.isEmpty()) return "";
        try {
            OffsetDateTime t = OffsetDateTime.parse(captured);
            long min = Duration.between(t, OffsetDateTime.now()).toMinutes();
            if (min >= FRESH_WINDOW_MIN) {
                return "updated " + min + " min ago";
            }
        } catch (Exception e) {
            // Unparsable timestamp — treat as stale rather than claim live.
            return "updated a while ago";
        }
        return "";
    }
}
