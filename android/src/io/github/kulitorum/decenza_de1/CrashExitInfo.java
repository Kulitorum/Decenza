package io.github.kulitorum.decenza_de1;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.os.Build;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.util.Date;
import java.util.List;

/** The system's record of how a previous process of this app died. */
public final class CrashExitInfo {
    private CrashExitInfo() {}

    // Tombstones run to hundreds of KB; this bounds only a pathological one.
    private static final int MAX_BYTES = 4 * 1024 * 1024;

    // Why the last nativeCrashTombstone() returned null, for the crash report.
    private static String sLastStatus = "";

    /** debuggerd's tombstone (tombstone.proto) for process pid, or null. */
    public static byte[] nativeCrashTombstone(Context context, int pid) {
        sLastStatus = "";
        // getTraceInputStream() carries a tombstone for REASON_CRASH_NATIVE from API 31.
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) {
            sLastStatus = "Android " + Build.VERSION.RELEASE
                    + " does not give apps their tombstones; that starts at Android 12";
            return null;
        }
        try {
            ActivityManager am = context.getSystemService(ActivityManager.class);
            List<ApplicationExitInfo> exits =
                    am.getHistoricalProcessExitReasons(context.getPackageName(), pid, 1);
            if (exits.isEmpty()) {
                sLastStatus = "the system kept no exit record for pid " + pid;
                return null;
            }
            ApplicationExitInfo exit = exits.get(0);
            if (exit.getReason() != ApplicationExitInfo.REASON_CRASH_NATIVE) {
                // What was recorded, not a verdict: a crash that never reached
                // debuggerd is recorded as REASON_SIGNALED, status = the signal.
                // The time is for the report's "Time:" line: the lookup is by pid
                // alone, and records outlive the process.
                String description = exit.getDescription();
                sLastStatus = "the system recorded " + reasonName(exit.getReason())
                        + " with status " + exit.getStatus()
                        + (description != null ? " (" + description + ")" : "")
                        + " for pid " + pid + " at " + new Date(exit.getTimestamp())
                        + ", so no tombstone is attached to its exit record";
                return null;
            }
            try (InputStream in = exit.getTraceInputStream()) {
                if (in == null) {
                    sLastStatus = "the native crash of pid " + pid + " has no tombstone attached";
                    return null;
                }
                ByteArrayOutputStream out = new ByteArrayOutputStream();
                byte[] buf = new byte[16384];
                int n;
                while ((n = in.read(buf)) > 0) {
                    if (out.size() + n > MAX_BYTES) {
                        sLastStatus = "the tombstone is larger than " + MAX_BYTES + " bytes";
                        return null;
                    }
                    out.write(buf, 0, n);
                }
                if (out.size() == 0) {
                    sLastStatus = "the tombstone attached to pid " + pid + " is empty";
                    return null;
                }
                return out.toByteArray();
            }
        } catch (Throwable t) {
            sLastStatus = "reading the exit record failed: " + t;
            return null;
        }
    }

    public static String lastStatus() {
        return sLastStatus;
    }

    private static String reasonName(int reason) {
        switch (reason) {
            case ApplicationExitInfo.REASON_SIGNALED: return "death by signal";
            case ApplicationExitInfo.REASON_CRASH: return "a Java crash";
            case ApplicationExitInfo.REASON_ANR: return "an ANR";
            case ApplicationExitInfo.REASON_LOW_MEMORY: return "a low-memory kill";
            case ApplicationExitInfo.REASON_EXIT_SELF: return "its own exit";
            case ApplicationExitInfo.REASON_USER_REQUESTED: return "a user request";
            case ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE: return "an excessive-resource kill";
            case ApplicationExitInfo.REASON_OTHER: return "an unspecified system kill";
            default: return "exit reason " + reason;
        }
    }
}
