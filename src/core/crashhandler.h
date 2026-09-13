#ifndef CRASHHANDLER_H
#define CRASHHANDLER_H

#include <QString>
#include <QStringList>

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

    /// api.decenza.coffee keeps the first 5000 UTF-16 units of debug_log_tail when
    /// it opens an issue (table in crashhandler.cpp). Anything past that is lost
    /// from the end, which is the part nearest the crash.
    static constexpr qsizetype kDebugLogTailBudget = 4900;

    /// The crashed run's story from debug.log, within charBudget: see
    /// selectCrashNarrative(). Must be called before WebDebugLogger::install(),
    /// which starts the new run's session in the same file.
    static QString getDebugLogTail(qsizetype charBudget = kDebugLogTailBudget);

    /// Picks from one run's lines what fits charBudget: the last lines, then
    /// errors, warnings, info and debug, each newest first. Output is in log
    /// order with omitted stretches marked.
    static QString selectCrashNarrative(const QStringList& lines, qsizetype charBudget);

private:
    static void signalHandler(int signal);
    static void writeCrashLog(int signal, const char* signalName);
};

#endif // CRASHHANDLER_H
