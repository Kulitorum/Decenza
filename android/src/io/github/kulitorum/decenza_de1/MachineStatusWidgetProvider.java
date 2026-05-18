package io.github.kulitorum.decenza_de1;

import android.app.PendingIntent;
import android.appwidget.AppWidgetManager;
import android.appwidget.AppWidgetProvider;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.widget.RemoteViews;

import org.json.JSONObject;

import java.time.OffsetDateTime;
import java.time.Duration;
import java.util.Locale;
import java.util.Map;

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

    // Below this widget height (dp) only phase + temp are shown (no room for
    // last-shot/staleness). Mirrors the iOS systemSmall compact rule.
    private static final int COMPACT_MAX_HEIGHT_DP = 120;

    // Authoritative short labels for raw MachineState phase strings. Mirrors
    // docs/CLAUDE_MD/WIDGET_SNAPSHOT.md and the iOS WidgetPhase.labels —
    // keep all three in sync.
    private static final Map<String, String> PHASE_LABELS = Map.ofEntries(
            Map.entry("Disconnected", "Disconnected"),
            Map.entry("Sleep", "Sleep"),
            Map.entry("Idle", "Idle"),
            Map.entry("Heating", "Heating"),
            Map.entry("Ready", "Ready"),
            Map.entry("EspressoPreheating", "Preheating"),
            Map.entry("Preinfusion", "Preinfusion"),
            Map.entry("Pouring", "Pouring"),
            Map.entry("Ending", "Finishing"),
            Map.entry("Steaming", "Steaming"),
            Map.entry("HotWater", "Hot Water"),
            Map.entry("Flushing", "Flushing"),
            Map.entry("Refill", "Refill"),
            Map.entry("Descaling", "Descaling"),
            Map.entry("Cleaning", "Cleaning"));

    @Override
    public void onUpdate(Context context, AppWidgetManager mgr, int[] ids) {
        renderAll(context, mgr, ids);
    }

    @Override
    public void onAppWidgetOptionsChanged(Context context, AppWidgetManager mgr,
                                          int id, Bundle newOptions) {
        mgr.updateAppWidget(id, buildViews(context, isCompact(newOptions)));
    }

    static void renderAll(Context context, AppWidgetManager mgr, int[] ids) {
        for (int id : ids) {
            boolean compact = isCompact(mgr.getAppWidgetOptions(id));
            mgr.updateAppWidget(id, buildViews(context, compact));
        }
    }

    private static boolean isCompact(Bundle options) {
        if (options == null) return false;
        int minH = options.getInt(
                AppWidgetManager.OPTION_APPWIDGET_MIN_HEIGHT, 0);
        return minH > 0 && minH < COMPACT_MAX_HEIGHT_DP;
    }

    private static RemoteViews buildViews(Context context, boolean compact) {
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

        boolean disconnected = true;
        if (json != null && !json.isEmpty()) {
            try {
                JSONObject o = new JSONObject(json);
                if (o.optBoolean("connected", false)) {
                    disconnected = false;
                    String phase = o.optString("phase", "");
                    double tempC = o.optDouble("temperatureC", Double.NaN);
                    double targetC = o.optDouble("targetTemperatureC", Double.NaN);
                    double steamC = o.optDouble("steamTemperatureC", Double.NaN);

                    v.setTextViewText(R.id.widget_phase, phaseLabel(phase));
                    v.setTextViewText(R.id.widget_temp,
                            tempLine(phase, tempC, targetC, steamC));
                    v.setTextViewText(R.id.widget_last_shot, lastShotLine(o));
                    v.setTextViewText(R.id.widget_status, stalenessLine(o));
                }
            } catch (Exception e) {
                Log.w(TAG, "Snapshot parse failed", e);
            }
        }

        if (disconnected) {
            v.setTextViewText(R.id.widget_phase, "Disconnected");
            v.setTextViewText(R.id.widget_temp, "");
            v.setTextViewText(R.id.widget_last_shot, "");
            v.setTextViewText(R.id.widget_status, "Tap to open");
        }

        // Compact (small) size: phase + temp only. Keep the status line only
        // when it's the disconnected "Tap to open" prompt.
        boolean showExtras = !compact;
        v.setViewVisibility(R.id.widget_last_shot,
                showExtras ? View.VISIBLE : View.GONE);
        v.setViewVisibility(R.id.widget_status,
                (showExtras || disconnected) ? View.VISIBLE : View.GONE);
        return v;
    }

    private static String phaseLabel(String phase) {
        if (phase == null || phase.isEmpty()) return "Decenza";
        String label = PHASE_LABELS.get(phase);
        return label != null ? label : phase;
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
            // Unparsable timestamp is a writer/schema bug — log it, and treat
            // as stale rather than silently claiming the data is live.
            Log.w(TAG, "capturedAt parse failed: " + captured, e);
            return "updated a while ago";
        }
        return "";
    }
}
