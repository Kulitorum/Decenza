#include "crashhandler.h"
#include "logpaths.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QDebug>

#include <csignal>
#include <cstdlib>
#include <cstring>

#if defined(Q_OS_MACOS) || defined(Q_OS_IOS) || defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
#include <pthread.h>
#endif

#ifdef Q_OS_ANDROID
#include <unwind.h>
#include <dlfcn.h>
#include <cxxabi.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>
#endif

#if (defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)) || defined(Q_OS_MACOS) || defined(Q_OS_IOS)
#include <execinfo.h>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

// Static storage for crash log path (set before signals might fire)
static char s_crashLogPath[512] = {0};
static char s_debugLogPath[512] = {0};
static char s_lastDebugMessage[4096] = {0};

#ifdef Q_OS_ANDROID
// "--pid=<N>" argument for logcat, precomputed in install() so the signal
// handler never has to format it.
static char s_logcatPidArg[32] = {0};
#endif

// Store recent debug messages for context
static QtMessageHandler s_previousHandler = nullptr;

static void crashMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    // Store the last few messages for crash context
    QByteArray localMsg = msg.toLocal8Bit();
    strncpy(s_lastDebugMessage, localMsg.constData(), sizeof(s_lastDebugMessage) - 1);
    s_lastDebugMessage[sizeof(s_lastDebugMessage) - 1] = '\0';

    // Call the previous handler
    if (s_previousHandler) {
        s_previousHandler(type, context, msg);
    }
}

#ifdef Q_OS_ANDROID
// Android backtrace using _Unwind_Backtrace
struct BacktraceState {
    void** current;
    void** end;
};

static _Unwind_Reason_Code unwindCallback(struct _Unwind_Context* context, void* arg)
{
    BacktraceState* state = static_cast<BacktraceState*>(arg);
    uintptr_t pc = _Unwind_GetIP(context);
    if (pc) {
        if (state->current == state->end) {
            return _URC_END_OF_STACK;
        }
        *state->current++ = reinterpret_cast<void*>(pc);
    }
    return _URC_NO_REASON;
}

static size_t captureBacktrace(void** buffer, size_t max)
{
    BacktraceState state = {buffer, buffer + max};
    _Unwind_Backtrace(unwindCallback, &state);
    return state.current - buffer;
}

static void writeBacktraceToFile(FILE* f)
{
    void* buffer[64];
    size_t count = captureBacktrace(buffer, 64);

    fprintf(f, "\nBacktrace (%zu frames):\n", count);
    for (size_t i = 0; i < count; ++i) {
        Dl_info info;
        if (dladdr(buffer[i], &info) && info.dli_sname) {
            // Try to demangle C++ names
            int status = 0;
            char* demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
            const char* name = (status == 0 && demangled) ? demangled : info.dli_sname;

            fprintf(f, "  #%zu: %p %s + %td (%s)\n",
                    i, buffer[i], name,
                    static_cast<char*>(buffer[i]) - static_cast<char*>(info.dli_saddr),
                    info.dli_fname ? info.dli_fname : "???");

            if (demangled) free(demangled);
        } else {
            fprintf(f, "  #%zu: %p\n", i, buffer[i]);
        }
    }
}

// Capture this process's logcat into the crash log. The point is the abort
// message: when ART kills us (JNI errors like global reference table overflow,
// CheckJNI failures), it logs the FATAL reason — and for ref-table overflow, a
// dump of the table's dominant classes — to logd *before* raising SIGABRT. Our
// own qDebug log never sees that text, so without this section a
// SIGABRT-from-ART report shows where we were, but not why ART aborted (#1408).
//
// An app may always read its own logs (logd filters by UID, no READ_LOGS
// needed). fork() in a signal handler is not strictly async-signal-safe, but
// between fork and exec the child calls only close/dup2/execl/_exit (all
// AS-safe) and the rest of this crash handler already relies on far less safe
// machinery (stdio, demangling).
//
// Every outcome writes a distinct marker line: this section exists to explain
// crashes, so an empty section must be attributable (exec denied vs. logd
// rotated this pid out vs. capture killed) rather than read as "no entries".
//
// THE BUDGET IS THE DESIGN. What bounds the report is not what fits on the
// device (CrashReportDialog.qml renders the whole thing) — it is that the report
// is POSTed to api.decenza.coffee, which slices it before opening or commenting
// on a GitHub issue. Verified 2026-08-02 against Kulitorum/decenza-shotmap,
// backend/lambdas/crashReport.ts:
//
//     new issue body        crashLog.slice(0, 10000)      + debugLogTail.slice(0, 5000)
//     comment on existing   crashLog.slice(0,  5000)      and NO debugLogTail at all
//                                                          (addCommentToIssue is
//                                                           never passed the field)
//
// Both cut from the END. Which path a report takes is decided by
// findSimilarIssue(), whose search is filtered `is:open` — so a crash dedupes
// onto its predecessor only while that predecessor is still OPEN. #1745 is the
// fourth instance of this exact crash and still opened a NEW issue, because
// #1408 and #1572 had been closed as duplicates: it got the 10000-char budget,
// and its crash log arrived cut at exactly 10001 chars.
//
// So: 10000 normally, 5000 whenever someone has left the prior issue open. The
// sizing below has to survive the 5000 case, and anything that grows what
// precedes the capture has to be re-measured against both. In #1745 the header
// and 29-frame backtrace alone were ~4117 chars.
//
// #1745 is what this replaces: a blind `-t 200` unfiltered tail, which returned
// 41 lines of OTHER THREADS' stacks and not one line of diagnosis. ART emits its
// abort block at fatal priority — the report's lines read " F rum.decenza_de:" —
// but so is the per-thread stack dump that follows, so filtering alone would not
// have fixed it. Taking the HEAD of the filtered stream is what cuts before the
// thread dump.
//
// INFERRED, not sourced: that the ref-table dump (the Summary naming the leaked
// class) precedes the thread dump. What #1745 does establish is that the abort
// is raised from inside the log record — its frames #5-#6 are
// `LogMessage::~LogMessage` -> `Runtime::Abort` — so the message text is written
// to logd before any thread dump exists. Where ART places the ref-table dump
// within that is not visible in any report we hold; the first post-fix capture
// is what confirms or refutes it. Do not repeat the ordering as established.
static constexpr size_t kFatalCaptureBudget = 4000;

// Why the capture ended. Returned instead of a bare byte count because the
// caller cannot otherwise tell "logcat had nothing to say" from "logcat never
// ran", and it was printing a confident "not an ART abort" over both.
enum class CaptureOutcome {
    Content,      // bytes captured, stream ended on its own
    BudgetHit,    // bytes captured, we stopped at byteBudget
    NoEntries,    // child ran and exited cleanly with nothing to say
    ExecFailed,
    Dup2Failed,
    PipeFailed,
    ForkFailed,
    SetupFailed,  // fcntl could not make the read non-blocking
    ReadFailed,
    TimedOut,     // deadline expired with the child still streaming
    ChildLost,    // waitpid failed (ECHILD) — we killed it, output may be short
    Unknown,      // nothing matched — print the raw status rather than guess
};


// Read the child's output through a pipe and write at most byteBudget of it to
// f. Head-anchored on purpose: `logcat -t N` gives the LAST N lines, which is
// the #1745 bug, so the cap has to be applied by us, from the start of the
// stream. `bytesOut` receives what was written (may be non-zero even on a
// failure outcome — a stream can fail part-way).
static CaptureOutcome captureLogcatToFile(FILE* f, bool fatalOnly,
                                          size_t byteBudget, size_t* bytesOut,
                                          int* rawStatusOut)
{
    *bytesOut = 0;
    *rawStatusOut = 0;

    int fds[2];
    if (pipe(fds) != 0)
        return CaptureOutcome::PipeFailed;

    pid_t child = fork();
    if (child == 0) {
        close(fds[0]);
        // stdout ONLY. logcat's own stderr must NOT reach the content pipe:
        // the unfiltered fallback runs only on CaptureOutcome::NoEntries, and
        // that outcome requires the stream to have produced nothing. A device
        // that rejects these arguments would put logcat's complaint in the
        // pipe, which counts as captured bytes, yields Content instead of
        // NoEntries, and skips the fallback — the one path that rescues a
        // non-ART crash, disabled by the failure of the path it rescues. The
        // exit status is what attributes such a failure instead.
        if (dup2(fds[1], STDOUT_FILENO) < 0)
            _exit(126);
        const int devNull = open("/dev/null", O_WRONLY);
        if (devNull >= 0) {
            (void)dup2(devNull, STDERR_FILENO);  // best effort; noise, not correctness
            close(devNull);
        }
        close(fds[1]);
        if (fatalOnly) {
            // "*:F" is every tag at fatal priority. "-v raw" drops the 49-char
            // "date pid tid F tag: " prefix, which was 34% of the bytes
            // captured in #1745 (2009 of 5824). Note ART's own "runtime.cc:NNN]"
            // tag is message payload and survives -v raw.
            //
            // No "-t": it is a TAIL, and on the common non-ART crash the fatal
            // filter matches nothing, so a wide "-t" only makes logcat walk the
            // pid's whole retained history to produce zero bytes. --pid= plus
            // the fatal filter is the bound that matters; byteBudget is the cap.
            execl("/system/bin/logcat", "logcat", "-d", "-v", "raw",
                  s_logcatPidArg, "*:F", static_cast<char*>(nullptr));
        } else {
            execl("/system/bin/logcat", "logcat", "-d", "-t", "200",
                  s_logcatPidArg, static_cast<char*>(nullptr));
        }
        _exit(127);
    }
    if (child < 0) {
        close(fds[0]);
        close(fds[1]);
        return CaptureOutcome::ForkFailed;
    }

    close(fds[1]);
    // Non-blocking is what makes the deadline below real: a blocking read on a
    // wedged child would hang the crash handler with no bound at all. So if it
    // cannot be established, abandon the capture rather than run the loop
    // without the property it depends on.
    if (fcntl(fds[0], F_SETFL, O_NONBLOCK) != 0) {
        close(fds[0]);
        kill(child, SIGKILL);
        waitpid(child, nullptr, WNOHANG);
        return CaptureOutcome::SetupFailed;
    }

    size_t written = 0;
    bool budgetHit = false;
    bool eof = false;
    bool readFailed = false;
    bool reaped = false;
    int childStatus = 0;
    int iterations = 0;

    // Bounded: logcat -d exits almost immediately, but never let a wedged child
    // hang the crash handler. ~3 s cap, then kill and move on.
    for (; iterations < 60 && !eof && !budgetHit && !readFailed; ++iterations) {
        for (;;) {
            char buf[1024];
            ssize_t n = read(fds[0], buf, sizeof(buf));
            if (n > 0) {
                const size_t remaining = byteBudget - written;
                const size_t take = (static_cast<size_t>(n) < remaining)
                                        ? static_cast<size_t>(n) : remaining;
                if (take > 0) {
                    fwrite(buf, 1, take, f);
                    written += take;
                }
                if (written >= byteBudget) {
                    budgetHit = true;
                    break;
                }
                continue;
            }
            if (n == 0) {
                eof = true;
                break;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                break;  // nothing ready yet — yield to the wait below
            // A hard error (EIO on a failing device, EBADF). Left folded into
            // the EAGAIN case it would never set eof, so the loop would burn
            // the full 3 s re-reading a dead fd and then report success.
            readFailed = true;
            break;
        }

        // Reap BEFORE the exit test. On the normal path logcat exits, the pipe
        // closes, read() returns 0 and we leave immediately — so a waitpid
        // placed after that test never ran on the path that always happens, and
        // every successful capture went on to SIGKILL an already-dead child and
        // sleep 50 ms for it.
        pid_t r = waitpid(child, &childStatus, WNOHANG);
        if (r == child) {
            reaped = true;
        } else if (r < 0) {
            // ECHILD (e.g. SIGCHLD set to SIG_IGN elsewhere in the process): we
            // can no longer observe the child, but we are still obliged to stop
            // it — it holds the write end of a pipe we are about to close.
            kill(child, SIGKILL);
            close(fds[0]);
            *bytesOut = written;
            return CaptureOutcome::ChildLost;
        }
        if (eof || budgetHit || readFailed)
            break;

        struct timespec ts = {0, 50 * 1000 * 1000};  // 50 ms
        nanosleep(&ts, nullptr);
    }

    const bool timedOut = !eof && !budgetHit && !readFailed;

    close(fds[0]);
    if (!reaped) {
        // Kill, then reap best-effort only — a blocking waitpid could hang the
        // whole crash handler on a child stuck in uninterruptible sleep, which
        // on a distressed device is exactly when this code runs.
        kill(child, SIGKILL);
        struct timespec ts = {0, 50 * 1000 * 1000};  // 50 ms
        nanosleep(&ts, nullptr);
        waitpid(child, &childStatus, WNOHANG);
    }

    // A short write makes `written` a lie, and `written` decides both the
    // marker and whether the fallback runs. The code this replaced compared
    // lseek() offsets and so actually observed the file; this restores that.
    if (ferror(f))
        readFailed = true;

    *bytesOut = written;
    *rawStatusOut = childStatus;
    if (readFailed)
        return CaptureOutcome::ReadFailed;
    if (timedOut)
        return CaptureOutcome::TimedOut;
    if (budgetHit)
        return CaptureOutcome::BudgetHit;
    if (written > 0)
        return CaptureOutcome::Content;
    if (WIFEXITED(childStatus) && WEXITSTATUS(childStatus) == 127)
        return CaptureOutcome::ExecFailed;
    if (WIFEXITED(childStatus) && WEXITSTATUS(childStatus) == 126)
        return CaptureOutcome::Dup2Failed;
    if (WIFEXITED(childStatus) && WEXITSTATUS(childStatus) == 0)
        return CaptureOutcome::NoEntries;
    return CaptureOutcome::Unknown;
}

// One marker per outcome, and exactly one. This section exists to explain
// crashes, so an empty or short section must be attributable — and must never
// be MISattributed, which is worse: a truncated capture that prints "end of
// capture" tells the reader ART's dump genuinely ended there.
static void writeCaptureMarker(FILE* f, CaptureOutcome outcome, size_t bytes,
                               size_t byteBudget, int rawStatus)
{
    switch (outcome) {
    case CaptureOutcome::Content:
        fprintf(f, "  (end of capture)\n");
        break;
    case CaptureOutcome::BudgetHit:
        fprintf(f, "\n  (capture stopped at %zu bytes — see the budget note in "
                   "crashhandler.cpp; the rest would not have survived the "
                   "server's slice)\n", byteBudget);
        break;
    case CaptureOutcome::NoEntries:
        fprintf(f, "  (logcat ran and had nothing to report for this pid)\n");
        break;
    case CaptureOutcome::ExecFailed:
        fprintf(f, "  (logcat exec failed — no capture)\n");
        break;
    case CaptureOutcome::Dup2Failed:
        fprintf(f, "  (dup2 failed in capture child — no capture)\n");
        break;
    case CaptureOutcome::PipeFailed:
        fprintf(f, "  (pipe failed — no capture)\n");
        break;
    case CaptureOutcome::ForkFailed:
        fprintf(f, "  (fork failed — no capture)\n");
        break;
    case CaptureOutcome::SetupFailed:
        fprintf(f, "  (could not set the capture pipe non-blocking — skipped "
                   "rather than risk an unbounded read in the signal handler)\n");
        break;
    case CaptureOutcome::ReadFailed:
        fprintf(f, "\n  (capture failed after %zu bytes — output above is "
                   "incomplete)\n", bytes);
        break;
    case CaptureOutcome::TimedOut:
        fprintf(f, "\n  (capture killed after 3s with %zu bytes — output above "
                   "is truncated mid-stream, not complete)\n", bytes);
        break;
    case CaptureOutcome::ChildLost:
        fprintf(f, "\n  (lost track of the capture child after %zu bytes and "
                   "killed it — output above may be incomplete)\n", bytes);
        break;
    case CaptureOutcome::Unknown:
        fprintf(f, "  (capture produced %zu bytes and ended unexplained; child "
                   "status=0x%x)\n", bytes, static_cast<unsigned>(rawStatus));
        break;
    }
}

// ART's own account of why it aborted. Written BEFORE our backtrace, which is
// deliberate and is the other half of the #1745 fix: when ART aborts, our
// backtrace names the JNI call that happened to hit the ceiling (a battery poll
// in #1408/#1572/#1745) and ART's dump names the actual leak, so on a budget
// that cuts from the end, ART's dump is what has to go first.
//
// Returns the outcome so the caller can tell "ART said nothing" (fall back to
// the unfiltered tail) from "the capture itself failed" (the fallback would
// fail identically, and a second theory printed under it would contradict the
// first).
static CaptureOutcome appendArtAbortMessageToFile(FILE* f)
{
    fprintf(f, "\nART abort message (logcat, fatal priority only):\n");
    size_t bytes = 0;
    int rawStatus = 0;
    const CaptureOutcome outcome = captureLogcatToFile(
        f, /*fatalOnly=*/true, kFatalCaptureBudget, &bytes, &rawStatus);
    writeCaptureMarker(f, outcome, bytes, kFatalCaptureBudget, rawStatus);
    if (outcome == CaptureOutcome::NoEntries)
        fprintf(f, "  (so this was not an ART abort, or logd rotated its "
                   "entries out)\n");
    return outcome;
}

static void appendLogcatTailToFile(FILE* f, size_t byteBudget)
{
    fprintf(f, "\nSystem log tail (logcat):\n");
    size_t bytes = 0;
    int rawStatus = 0;
    const CaptureOutcome outcome =
        captureLogcatToFile(f, /*fatalOnly=*/false, byteBudget, &bytes, &rawStatus);
    writeCaptureMarker(f, outcome, bytes, byteBudget, rawStatus);
}
#endif

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
static void writeBacktraceToFile(FILE* f)
{
    void* buffer[64];
    int count = backtrace(buffer, 64);
    char** symbols = backtrace_symbols(buffer, count);

    fprintf(f, "\nBacktrace (%d frames):\n", count);
    for (int i = 0; i < count; ++i) {
        fprintf(f, "  #%d: %s\n", i, symbols[i] ? symbols[i] : "???");
    }

    if (symbols) free(symbols);
}
#endif

#ifdef Q_OS_WIN
static void writeBacktraceToFile(FILE* f)
{
    void* buffer[64];
    USHORT frames = CaptureStackBackTrace(0, 64, buffer, nullptr);

    HANDLE process = GetCurrentProcess();
    SymInitialize(process, nullptr, TRUE);

    fprintf(f, "\nBacktrace (%d frames):\n", frames);

    SYMBOL_INFO* symbol = (SYMBOL_INFO*)calloc(sizeof(SYMBOL_INFO) + 256, 1);
    symbol->MaxNameLen = 255;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

    for (USHORT i = 0; i < frames; ++i) {
        SymFromAddr(process, (DWORD64)buffer[i], nullptr, symbol);
        fprintf(f, "  #%d: 0x%p %s\n", i, buffer[i], symbol->Name);
    }

    free(symbol);
    SymCleanup(process);
}
#endif

#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
static void writeBacktraceToFile(FILE* f)
{
    void* buffer[64];
    int count = backtrace(buffer, 64);
    char** symbols = backtrace_symbols(buffer, count);

    fprintf(f, "\nBacktrace (%d frames):\n", count);
    for (int i = 0; i < count; ++i) {
        fprintf(f, "  #%d: %s\n", i, symbols[i] ? symbols[i] : "???");
    }

    if (symbols) free(symbols);
}
#endif

void CrashHandler::writeCrashLog(int signal, const char* signalName)
{
    // Open crash log file (using raw C file I/O - safer in signal handler)
    FILE* f = fopen(s_crashLogPath, "w");
    if (!f) return;

    // Write crash header
    fprintf(f, "%s\n", kReportStart);
    fprintf(f, "Signal: %d (%s)\n", signal, signalName);

    // Get current time (basic, signal-safe-ish)
    time_t now = time(nullptr);
    fprintf(f, "Time: %s", ctime(&now));  // ctime adds newline

    // Thread info — critical for diagnosing render thread crashes
    char threadName[64] = {0};
#if defined(Q_OS_MACOS) || defined(Q_OS_IOS) || defined(Q_OS_LINUX)
    pthread_getname_np(pthread_self(), threadName, sizeof(threadName));
    fprintf(f, "Thread: %p name=\"%s\"\n",
            (void*)pthread_self(), threadName[0] ? threadName : "(unnamed)");
#elif defined(Q_OS_WIN)
    fprintf(f, "Thread: %lu\n", (unsigned long)GetCurrentThreadId());
#endif

    // Last debug message, CAPPED. s_lastDebugMessage is char[4096] and holds
    // whatever the last qDebug was — this app logs profile JSON and HTTP bodies,
    // so an unbounded copy can spend most of the 5000-char head slice before the
    // ART capture below has written a byte, and the report ends up looking
    // exactly like #1745 again. The first line is the part that has ever been
    // diagnostic (in #1745 it named the screensaver transition count).
    if (s_lastDebugMessage[0] != '\0') {
        constexpr int kLastMessageMax = 512;
        fprintf(f, "\nLast debug message:\n  %.*s%s\n",
                kLastMessageMax, s_lastDebugMessage,
                s_lastDebugMessage[kLastMessageMax] != '\0' ? " …(truncated)" : "");
    }

#ifdef Q_OS_ANDROID
    // Ahead of the backtrace: see appendArtAbortMessageToFile(). Only into
    // crash.log (which becomes the report's "Crash Log" section) — not into the
    // debug.log copy below, whose tail is submitted separately.
    const CaptureOutcome artOutcome = appendArtAbortMessageToFile(f);
#endif

    // Write backtrace
#if defined(Q_OS_ANDROID) || defined(Q_OS_LINUX) || defined(Q_OS_WIN) || defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    writeBacktraceToFile(f);
#else
    fprintf(f, "\nBacktrace: not available on this platform\n");
#endif

#ifdef Q_OS_ANDROID
    // Only when ART ran and had nothing to say. Skipped when ART DID explain
    // itself (the budget is spent, and the unfiltered tail there is the thread
    // dump #1745 wasted its whole report on) and equally when the capture
    // FAILED — the fallback runs the same binary the same way, so it would fail
    // identically and stack a second, contradictory marker under the first.
    //
    // Smaller budget than the fatal pass: on this path the backtrace above has
    // already spent ~4100 chars, so on a 5000-char comment only a few hundred
    // survive submission anyway, and every byte here is also up to 3s of
    // signal-handler time on a machine that may be mid-shot.
    if (artOutcome == CaptureOutcome::NoEntries)
        appendLogcatTailToFile(f, 1500);
#endif

    fprintf(f, "\n%s\n", kReportEnd);
    fflush(f);
    fclose(f);

    // Also append to debug.log for persistence
    if (s_debugLogPath[0] != '\0') {
        FILE* debugLog = fopen(s_debugLogPath, "a");
        if (debugLog) {
            fprintf(debugLog, "\n\n%s\n", kReportStart);
            fprintf(debugLog, "Signal: %d (%s)\n", signal, signalName);
            fprintf(debugLog, "Time: %s", ctime(&now));
            if (s_lastDebugMessage[0] != '\0') {
                fprintf(debugLog, "\nLast debug message:\n  %s\n", s_lastDebugMessage);
            }
#if defined(Q_OS_ANDROID) || defined(Q_OS_LINUX) || defined(Q_OS_WIN) || defined(Q_OS_MACOS) || defined(Q_OS_IOS)
            writeBacktraceToFile(debugLog);
#endif
            // Flushed BEFORE the end marker as well as after: writeBacktrace
            // runs dladdr and __cxa_demangle (which mallocs) from a signal
            // handler on a possibly-corrupt heap, so a second fault in there
            // would leave a start marker with no end — and getDebugLogTail()
            // treats that as "everything after is report text". It recovers now
            // (see the EOF handling there), but losing less is better.
            fprintf(debugLog, "\n%s\n", kReportEnd);
            fflush(debugLog);
            fclose(debugLog);
        }
    }
}

void CrashHandler::signalHandler(int signal)
{
    const char* signalName = "UNKNOWN";
    switch (signal) {
        case SIGSEGV: signalName = "SIGSEGV (Segmentation fault)"; break;
        case SIGABRT: signalName = "SIGABRT (Abort)"; break;
#ifdef SIGBUS
        case SIGBUS:  signalName = "SIGBUS (Bus error)"; break;
#endif
        case SIGFPE:  signalName = "SIGFPE (Floating point exception)"; break;
        case SIGILL:  signalName = "SIGILL (Illegal instruction)"; break;
        default: break;
    }

    // Write crash log
    writeCrashLog(signal, signalName);

    // Re-raise signal to get default behavior (core dump, etc.)
    std::signal(signal, SIG_DFL);
    std::raise(signal);
}

void CrashHandler::install()
{
    // Set up crash log path early (before any signals might fire)
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataPath);

    QString logPath = dataPath + "/crash.log";
    QByteArray pathBytes = logPath.toUtf8();
    strncpy(s_crashLogPath, pathBytes.constData(), sizeof(s_crashLogPath) - 1);

    // Also set up debug.log path for persistent crash logging.
    //
    // DecenzaPaths::logsDirectory(), not dataPath — they differ on Android, and
    // getting it wrong made this file a SECOND debug.log that only this class
    // ever wrote to. WebDebugLogger puts every line of app narrative in
    // logsDirectory()/debug.log (external storage, so a user can retrieve it);
    // this used to append crash reports to AppDataLocation/debug.log, whose
    // entire content was therefore crash reports. That is why the debug tail
    // submitted with #1745 was one stale report and no narrative — not a
    // missing filter, the wrong file. logpaths.h:22-26 exists to stop exactly
    // this second copy quietly falling back to AppDataLocation.
    QString debugPath = DecenzaPaths::logsDirectory() + "/debug.log";
    QByteArray debugPathBytes = debugPath.toUtf8();
    strncpy(s_debugLogPath, debugPathBytes.constData(), sizeof(s_debugLogPath) - 1);

#ifdef Q_OS_ANDROID
    snprintf(s_logcatPidArg, sizeof(s_logcatPidArg), "--pid=%d", getpid());
#endif

    qDebug() << "CrashHandler: Installing signal handlers, crash log path:" << logPath;

    // Install message handler to capture last debug message
    s_previousHandler = qInstallMessageHandler(crashMessageHandler);

    // Install signal handlers
    std::signal(SIGSEGV, signalHandler);
    std::signal(SIGABRT, signalHandler);
#ifdef SIGBUS
    std::signal(SIGBUS, signalHandler);
#endif
    std::signal(SIGFPE, signalHandler);
    std::signal(SIGILL, signalHandler);
}

void CrashHandler::uninstall()
{
    // Restore default signal handlers to prevent spurious crash reports during cleanup
    // Crashes after main() returns are typically runtime cleanup issues we can't fix
    std::signal(SIGSEGV, SIG_DFL);
    std::signal(SIGABRT, SIG_DFL);
#ifdef SIGBUS
    std::signal(SIGBUS, SIG_DFL);
#endif
    std::signal(SIGFPE, SIG_DFL);
    std::signal(SIGILL, SIG_DFL);

    // Restore previous message handler
    if (s_previousHandler) {
        qInstallMessageHandler(s_previousHandler);
        s_previousHandler = nullptr;
    }
}

void CrashHandler::logOpenFileDescriptors(const QString& tag)
{
#ifdef Q_OS_ANDROID
    QDir fdDir("/proc/self/fd");
    if (!fdDir.exists()) {
        qDebug() << "[fd dump:" << tag << "] /proc/self/fd not accessible";
        return;
    }
    // /proc/self/fd entries are symlinks; the default QDir filter excludes
    // symlinks-to-non-existent. Pass an explicit filter that keeps everything
    // except `.` / `..`.
    const auto entries = fdDir.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    qDebug().noquote() << "[fd dump:" << tag << "]" << entries.size() << "open fds:";
    for (const QString& entry : entries) {
        const QFileInfo fi("/proc/self/fd/" + entry);
        qDebug().noquote().nospace() << "  fd=" << entry << " -> " << fi.symLinkTarget();
    }
#else
    Q_UNUSED(tag);
#endif
}

QString CrashHandler::crashLogPath()
{
    return QString::fromUtf8(s_crashLogPath);
}

bool CrashHandler::hasCrashLog()
{
    QString path = crashLogPath();
    if (!QFile::exists(path)) {
        return false;
    }

    // Check if this is a crash-on-exit (not actionable, don't bother user)
    // These happen during C++ runtime cleanup after main() returns normally
    QFile file(path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString content = QString::fromUtf8(file.readAll());
        file.close();

        // If the last debug message shows main() returned successfully,
        // this is a cleanup crash we can't fix - delete and ignore it
        if (content.contains("main() returned")) {
            qDebug() << "CrashHandler: Ignoring crash-on-exit (main() returned normally)";
            QFile::remove(path);
            return false;
        }
    }

    return true;
}

QString CrashHandler::readAndClearCrashLog()
{
    QString content = readCrashLog();

    // Remove the crash log after reading
    QFile::remove(crashLogPath());

    return content;
}

QString CrashHandler::readCrashLog()
{
    QString path = crashLogPath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }

    QString content = QString::fromUtf8(file.readAll());
    file.close();

    return content;
}

QString CrashHandler::getDebugLogTail(int lines)
{
    QString debugPath = QString::fromUtf8(s_debugLogPath);
    if (debugPath.isEmpty()) {
        // Fallback if install() wasn't called yet. Must resolve the same way
        // install() does — see the note there on the two-debug.log bug.
        debugPath = DecenzaPaths::logsDirectory() + "/debug.log";
    }

    QFile file(debugPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }

    // Read all lines, dropping any crash-report block.
    //
    // This tail is submitted as its own field to answer a question the crash log
    // cannot: what the app was doing before it died. Crash-report text in here
    // answers nothing, because the same text is already submitted as crashLog.
    // (Budget note: the server slices debugLogTail to 5000 chars when OPENING an
    // issue and drops the field entirely when commenting on an existing one —
    // see the table in this file's capture section.)
    //
    // Three writers put report text in this file:
    //   - writeCrashLog() appends the whole report at crash time;
    //   - main.cpp re-logs the previous run's crash log at startup, as THREE
    //     qWarning records: "=== PREVIOUS CRASH DETECTED ===", then the report
    //     body as one noquote record, then a standalone end marker. So the body's
    //     interior lines carry no log prefix while the markers around it do —
    //     which is why the match below is not anchored to the start of a line.
    // main.cpp's trailing standalone end marker is LOAD-BEARING, not redundant:
    // it closes a crash.log whose own end marker never made it to disk.
    const QLatin1String kBlockStart(kReportStart);
    const QLatin1String kBlockEnd(kReportEnd);

    QStringList allLines;
    QStringList blockLines;   // held while inside a block, restored if it never closes
    bool insideCrashBlock = false;
    bool sawAnyStart = false;
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        if (line.contains(kBlockEnd)) {
            // An end marker with no start has two causes, and they need opposite
            // treatment, so they are told apart by POSITION:
            //
            //  - A trim. WebDebugLogger::trimLogFile() keeps the TAIL of the
            //    file, so it can cut a block's opening line away and leave the
            //    body and closer behind. Everything read so far is that body and
            //    must go. This can only ever appear before the first start
            //    marker, because a trim cuts the head and nothing else.
            //  - main.cpp's standalone closer, which follows the report body's
            //    OWN end marker (see above). By then a start has been seen, the
            //    block is already closed, and the narrative before it is real.
            if (!insideCrashBlock && !sawAnyStart)
                allLines.clear();
            insideCrashBlock = false;
            blockLines.clear();
            continue;
        }
        if (line.contains(kBlockStart)) {
            insideCrashBlock = true;
            sawAnyStart = true;
            blockLines.clear();
            continue;
        }
        if (insideCrashBlock)
            blockLines.append(line);
        else
            allLines.append(line);
    }
    file.close();

    // A start marker that never closed. The producer is writeCrashLog()'s
    // debug.log append, which can die mid-block (it calls dladdr and a
    // malloc'ing demangler from a signal handler, on the heap that may have
    // caused the crash). Latching to EOF would drop every line after it — for
    // this run and every future one, since debug.log is append-mode and
    // persists — and the empty QString that produced is indistinguishable from
    // "could not open the file". A degraded tail beats a silent empty one.
    if (insideCrashBlock)
        allLines += blockLines;

    // Get last N lines
    qsizetype startIndex = qMax(qsizetype(0), allLines.size() - lines);
    QStringList tailLines = allLines.mid(startIndex);

    return tailLines.join("\n");
}
