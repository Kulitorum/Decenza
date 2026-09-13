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

    /// What the previous run left in crash.log. Returned rather than logged:
    /// main() asks before WebDebugLogger is installed, so a line logged here
    /// would never reach debug.log.
    enum class PreviousCrash { None, Pending, DiscardedOnExit, DiscardFailed };
    static PreviousCrash previousCrash();

    /// Read and clear the crash log (call after showing to user)
    static QString readAndClearCrashLog();

    /// Read the crash log without clearing it
    static QString readCrashLog();

    /// api.decenza.coffee keeps the first 5000 UTF-16 units of debug_log_tail when
    /// it opens an issue, and sends none when it comments on an open one (table in
    /// crashhandler.cpp). 100 under is margin, not a measurement.
    static constexpr qsizetype kDebugLogTailBudget = 4900;

    /// The crashed run from debug.log, within charBudget: the session holding
    /// writeCrashLog()'s own block, ended where that block starts, then
    /// selectCrashNarrative(). A missing block or session start is stated in a
    /// leading note rather than guessed around. Call before
    /// WebDebugLogger::install(), which starts the new run's session in this file.
    static QString getDebugLogTail(qsizetype charBudget = kDebugLogTailBudget);

    /// Picks from one run's lines what fits charBudget: session markers, the last
    /// 20 entries (consecutive repeats merged), then FATAL down to DEBUG, newest
    /// first. Lines with no level tag survive only among the last entries. Output
    /// is in log order with omitted stretches marked.
    static QString selectCrashNarrative(const QStringList& lines, qsizetype charBudget);

#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    /// "<image> 0x<address>" for a code address, the address being where it lies in
    /// the image's own file (runtime minus ASLR slide): what `atos -o <dSYM>` looks
    /// up with no -l or -s, "at their default locations" (man atos). Returns
    /// snprintf's result.
    static int describeCodeAddress(void* pc, char* out, size_t size);
#endif

private:
    static void signalHandler(int signal);
    static void writeCrashLog(int signal, const char* signalName);
};

#endif // CRASHHANDLER_H
