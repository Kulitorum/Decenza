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

    /// Suppress crash log writing for a known-noisy code path. The signal
    /// handler still re-raises the signal (so the OS terminates normally),
    /// it just skips writing crash.log + appending to debug.log. Intended
    /// for the Android APK install handover, where Qt's UNIX event
    /// dispatcher races against OS-reaped fds and produces a SIGSEGV in
    /// QSocketNotifier::setEnabled. Clear the flag on every terminal
    /// PackageInstaller status (success included — the success callback
    /// fires before the OS terminates us, and on slow ROMs that gap can
    /// be seconds long) so any real crash that fires after the dispatcher
    /// race window still gets reported.
    static void setSuppressCrashLog(bool suppress);

private:
    static void signalHandler(int signal);
    static void writeCrashLog(int signal, const char* signalName);
};

#endif // CRASHHANDLER_H
