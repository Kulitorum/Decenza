package io.github.kulitorum.decenza_de1;

import android.appwidget.AppWidgetManager;
import android.content.ComponentName;
import android.content.Context;
import android.content.SharedPreferences;
import android.util.Log;

/**
 * Bridge + shared constants for the machine-status Home Screen widget.
 *
 * The Qt app calls {@link #writeSnapshot(Context, String)} (via QJniObject)
 * whenever the machine-status snapshot changes. The snapshot JSON is stored in
 * SharedPreferences so {@code MachineStatusWidgetProvider} can read it from the
 * widget process. Mirrors src/widget/widgetsharedkeys.h — keep in sync.
 */
public final class MachineStatusWidget {
    private static final String TAG = "MachineStatusWidget";

    public static final String PREFS_NAME = "decenza_widget";
    public static final String PREFS_KEY = "machine_status_snapshot";

    private MachineStatusWidget() {}

    /** Persist the latest snapshot and ask any live widgets to redraw. */
    public static void writeSnapshot(Context context, String json) {
        if (context == null || json == null) {
            return;
        }
        try {
            SharedPreferences prefs =
                context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
            prefs.edit().putString(PREFS_KEY, json).apply();
            requestWidgetUpdate(context);
        } catch (Exception e) {
            // Pass the throwable so the type + stack survive (getMessage()
            // is null for e.g. NPE).
            Log.w(TAG, "Failed to write widget snapshot", e);
        }
    }

    /**
     * Notify the provider so it refreshes promptly while the app is connected
     * (instead of waiting for the OS update period). No-op if the widget has
     * not been added to a Home Screen yet.
     */
    static void requestWidgetUpdate(Context context) {
        try {
            AppWidgetManager mgr = AppWidgetManager.getInstance(context);
            ComponentName cn =
                new ComponentName(context, MachineStatusWidgetProvider.class);
            int[] ids = mgr.getAppWidgetIds(cn);
            if (ids != null && ids.length > 0) {
                MachineStatusWidgetProvider.renderAll(context, mgr, ids);
            }
        } catch (Exception e) {
            Log.w(TAG, "Widget update request failed", e);
        }
    }
}
