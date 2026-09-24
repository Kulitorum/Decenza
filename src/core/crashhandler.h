#ifndef CRASHHANDLER_H
#define CRASHHANDLER_H

#include <QByteArray>
#include <QString>
#include <QStringList>

#if defined(Q_OS_MACOS) || defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
#include <signal.h>
#endif

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

    /// Section headings writeCrashLog() prints and insertTombstoneSection()
    /// anchors on. Each section is preceded by a blank line.
    static constexpr const char* kArtCaptureHeading = "ART abort message (logcat, fatal priority only):";
    static constexpr const char* kBacktraceHeading  = "Backtrace (";
    static constexpr const char* kLogcatTailHeading = "System log tail (logcat):";

    /// Install signal handlers. Call once at startup.
    static void install();

    /// Re-read the crash header's "Device:" line, which install() reads before
    /// the app object exists. Call once it does.
    static void refreshDeviceLine();

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
    /// first. Lines with no level tag survive only among the last entries, and so
    /// do a Java stack trace's frames after its first, and empty messages. Output
    /// is in log order with omitted stretches marked.
    static QString selectCrashNarrative(const QStringList& lines, qsizetype charBudget);

    /// What the tombstone summary may cost of the 5000-char comment slice
    /// (table in crashhandler.cpp).
    static constexpr qsizetype kTombstoneSummaryBudget = 3500;

    /// On Android: crashLog with the previous run's tombstone summarised into it,
    /// ahead of this handler's own capture, read through ApplicationExitInfo and
    /// matched by writeCrashLog()'s "Pid:" line. Below Android 12, or when there
    /// is no tombstone, a one-line note says why. Unchanged on other platforms.
    static QString withAndroidTombstone(const QString& crashLog);

    struct TombstoneSummary {
        QString text;
        // Every frame of the crashing thread is in text, and the unwinder left
        // no note against it.
        bool hasWholeCrashingThread = false;
        // What the tombstone records, -1 when it does not: compared with the
        // report's "Tid:" and "Signal:" lines to tell whether both are one crash.
        qint64 tid = -1;
        int signal = -1;
    };

    /// A debuggerd tombstone (tombstone.proto) as report text within charBudget:
    /// signal, abort message, causes, GWP-ASan allocation and free stacks, the
    /// crashing thread, then other threads' first app frame. Malformed input
    /// yields a note, never a crash.
    static TombstoneSummary summarizeTombstone(const QByteArray& proto, qsizetype charBudget);

    /// What the server keeps of a new issue's crash log (table in crashhandler.cpp).
    static constexpr qsizetype kCrashLogBudget = 10000;

    /// Puts section into crashLog before this handler's ART capture (or its
    /// backtrace), so the server's head-anchored slice keeps it.
    static QString insertTombstoneSection(const QString& crashLog, const QString& section);

    /// insertTombstoneSection() for a summary. A tombstone whose thread or signal
    /// differs from crashLog's is labelled a second fault. Past kCrashLogBudget,
    /// and only when the summary holds the whole crashing thread of this same
    /// crash, this handler's own backtrace is dropped rather than leaving the
    /// server's cut to decide what is lost.
    static QString insertTombstoneSummary(const QString& crashLog, const TombstoneSummary& summary);

#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    /// "<image> 0x<address>" for a code address, the address unslid (runtime minus
    /// the image's dyld slide): what `atos -o <dSYM>` looks up with no -l or -s, "at
    /// their default locations" (man atos). For a shared-cache system library it is
    /// the unslid cache address. Returns snprintf's result.
    static int describeCodeAddress(void* pc, char* out, size_t size);
#endif

private:
#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    // SA_SIGINFO, for the interrupted pc: backtrace() inside a handler starts from
    // saved return addresses and never includes the faulting frame.
    static void signalActionHandler(int signal, siginfo_t* info, void* context);
#elif defined(Q_OS_ANDROID)
    // SA_SIGINFO, so the original siginfo and ucontext can be handed on to the
    // handler this one displaced (debuggerd's), which needs both to write a
    // tombstone of the crash rather than of this handler.
    static void chainingSignalHandler(int signal, siginfo_t* info, void* context);
#else
    static void signalHandler(int signal);
#endif
    static void handleSignal(int signal, void* faultPc);
    static void writeCrashLog(int signal, const char* signalName, void* faultPc);
};

#endif // CRASHHANDLER_H
