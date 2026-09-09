package io.github.kulitorum.decenza_de1;

import android.util.Log;

/** Runtime diagnostics enter Qt's registry/formatter once the app is loaded. */
public final class DiagnosticLog {
    private DiagnosticLog() {}

    private static native boolean writeNative(int priority, String owner, String emitter, String message);

    private static void write(int priority, String owner, String emitter, String message) {
        try {
            if (writeNative(priority, owner, emitter, message)) return;
        } catch (UnsatisfiedLinkError notLoaded) {
            // Broadcast receivers and the activity's pre-super.onCreate path can
            // run before the Qt library exists. Retry availability on the NEXT
            // event; caching this failure would lose the whole app session.
        }
        // Bootstrap/system-only output has no Qt elapsed clock or persistence.
        // Owners at call sites are checked against core/logtags.h by the gate.
        for (String line : String.valueOf(message).split("\\r\\n|\\r|\\n", -1))
            Log.println(priority, emitter, "[" + owner + "][" + emitter + "] " + line); // log-marker-exempt: Qt is not loaded in this process yet
    }

    public static void d(String owner, String tag, String message) { write(Log.DEBUG, owner, tag, message); }
    public static void i(String owner, String tag, String message) { write(Log.INFO, owner, tag, message); }
    public static void w(String owner, String tag, String message) { write(Log.WARN, owner, tag, message); }
    public static void e(String owner, String tag, String message) { write(Log.ERROR, owner, tag, message); }
    public static void w(String owner, String tag, String message, Throwable error) {
        w(owner, tag, message + "\n" + Log.getStackTraceString(error));
    }
    public static void e(String owner, String tag, String message, Throwable error) {
        e(owner, tag, message + "\n" + Log.getStackTraceString(error));
    }
}
