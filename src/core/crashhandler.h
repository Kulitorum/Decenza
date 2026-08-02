#ifndef CRASHHANDLER_H
#define CRASHHANDLER_H

#include <QString>

/**
 * @brief Installs signal handlers to catch crashes and log debug info before dying.
 *
 * Catches: SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL
 * Logs to: <app_data>/crash.log
 *
 * Call CrashHandler::install() early in main() before QApplication.
 */
class CrashHandler
{
public:
    /// The markers bracketing a crash report, wherever one is written.
    ///
    /// One definition because there are six producers (crash.log's own pair, the
    /// debug.log copy's pair, and main.cpp's two standalone re-log markers) and
    /// ONE consumer — getDebugLogTail(), which strips these blocks out of the
    /// tail it submits. Hand-copied, a writer could be respelled alone and the
    /// stripper would silently stop matching: the crash-report duplication of
    /// #1745 returns in full and no test notices, because a fixture spells the
    /// marker itself rather than asking a writer for it.
    static constexpr const char* kReportStart = "=== CRASH REPORT ===";
    static constexpr const char* kReportEnd   = "=== END CRASH REPORT ===";

    /// Install signal handlers. Call once at startup.
    static void install();

    /// Uninstall signal handlers. Call before app exit to prevent spurious crash reports.
    static void uninstall();

    /// Get the path to the crash log file
    static QString crashLogPath();

    /// Check if there's a crash log from a previous run
    static bool hasCrashLog();

    /// Read and clear the crash log (call after showing to user)
    static QString readAndClearCrashLog();

    /// Read the crash log without clearing it
    static QString readCrashLog();

    /// Get the last N lines of debug.log for context
    static QString getDebugLogTail(int lines = 50);

    /// Walk /proc/self/fd and qDebug each fd's symlink target. Diagnostic
    /// only — used to attribute the fd that gets reaped during the Android
    /// APK install handover (#865) so the next crash log tells us which
    /// service or socket family was responsible. No-op on non-Android.
    /// `tag` is logged with each line so multiple calls in the same run
    /// (e.g. before vs. after teardown) can be distinguished.
    static void logOpenFileDescriptors(const QString& tag);

private:
    static void signalHandler(int signal);
    static void writeCrashLog(int signal, const char* signalName);
};

#endif // CRASHHANDLER_H
