#include "crashhandler.h"

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
// THE BUDGET IS THE DESIGN. The report is not read on the device — it is POSTed
// to api.decenza.coffee, which slices it before opening or commenting on a
// GitHub issue (Kulitorum/decenza-shotmap, backend/lambdas/crashReport.ts):
//
//     new issue body        crashLog.slice(0, 10000)
//     comment on existing   crashLog.slice(0, 5000)      <-- the usual path
//     debug log tail        debugLogTail.slice(0, 5000)
//
// All three cut from the END. A recurring crash dedupes onto its existing
// issue, so 5000 chars is the real budget, not 10000 — and in #1745 the header
// and 29-frame backtrace had already spent ~4100 of it. Everything below is
// sized against that, and any change here should be re-measured against those
// slice() calls rather than against what fits on a screen.
//
// #1745 is what this replaces: a blind `-t 200` unfiltered tail. ART's abort
// block runs header -> ref-table dump (the Summary naming the leaked class) ->
// "Runtime aborting..." -> a full per-thread stack dump of ~1000 lines. A tail
// of 200 lands inside the thread dump, so the report carried 41 lines of other
// threads' stacks and not one line of the diagnosis it exists to capture.
// Hence: filter to fatal priority, take the HEAD of the block, and drop the
// ~60-char "date pid tid F tag:" prefix that was costing half of every line.
static constexpr size_t kFatalCaptureBudget = 4000;

// Read the child's output through a pipe and write at most byteBudget of it to
// f. Head-anchored on purpose: `logcat -t N` gives the LAST N lines, which is
// the #1745 bug, so the cap has to be applied by us, from the start of the
// stream. Returns bytes written; 0 means the capture produced nothing.
static size_t captureLogcatToFile(FILE* f, bool fatalOnly, size_t byteBudget)
{
    int fds[2];
    if (pipe(fds) != 0) {
        fprintf(f, "  (pipe failed — no capture)\n");
        return 0;
    }

    pid_t child = fork();
    if (child == 0) {
        close(fds[0]);
        if (dup2(fds[1], STDOUT_FILENO) < 0 || dup2(fds[1], STDERR_FILENO) < 0)
            _exit(126);
        close(fds[1]);
        if (fatalOnly) {
            // "*:F" is every tag at fatal priority — which, in normal
            // operation, is ART's abort block and nothing else (the lines in
            // #1745 read " F rum.decenza_de:"). "-v raw" drops the timestamp,
            // pid, tid and tag prefix; the payload is what diagnoses the
            // crash, and the prefix was eating ~50% of the budget above.
            // "-t 20000" is a ceiling, not a window — we stop at byteBudget.
            execl("/system/bin/logcat", "logcat", "-d", "-v", "raw",
                  "-t", "20000", s_logcatPidArg, "*:F",
                  static_cast<char*>(nullptr));
        } else {
            execl("/system/bin/logcat", "logcat", "-d", "-t", "200",
                  s_logcatPidArg, static_cast<char*>(nullptr));
        }
        _exit(127);
    }
    if (child < 0) {
        close(fds[0]);
        close(fds[1]);
        fprintf(f, "  (fork failed — no capture)\n");
        return 0;
    }

    close(fds[1]);
    // Non-blocking so the drain below can share the bounded wait loop: a
    // blocking read on a wedged child would hang the crash handler.
    fcntl(fds[0], F_SETFL, O_NONBLOCK);

    size_t written = 0;
    bool budgetHit = false;
    bool eof = false;
    bool reaped = false;
    int childStatus = 0;

    // Bounded: logcat -d exits almost immediately, but never let a wedged child
    // hang the crash handler. ~3 s cap, then kill and move on.
    for (int i = 0; i < 60 && !eof && !budgetHit; ++i) {
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
            if (n == 0)
                eof = true;
            break;  // EOF, EAGAIN, or error — yield to the wait below
        }
        if (eof || budgetHit)
            break;

        pid_t r = waitpid(child, &childStatus, WNOHANG);
        if (r == child) {
            reaped = true;
            // The child is gone but the pipe may still hold buffered output;
            // one more drain pass runs on the next iteration before we exit on
            // EOF. Do not break here.
        } else if (r < 0) {
            // ECHILD (e.g. SIGCHLD set to SIG_IGN elsewhere in the process):
            // we can no longer observe the child, so stop waiting on it.
            reaped = true;
            childStatus = 0;
        }
        struct timespec ts = {0, 50 * 1000 * 1000};  // 50 ms
        nanosleep(&ts, nullptr);
    }

    close(fds[0]);
    if (!reaped) {
        // Either timed out or we stopped early at the budget. Kill, then reap
        // best-effort only — a blocking waitpid could hang the whole crash
        // handler on a child stuck in uninterruptible sleep, which on a
        // distressed device is exactly when this code runs.
        kill(child, SIGKILL);
        struct timespec ts = {0, 50 * 1000 * 1000};  // 50 ms
        nanosleep(&ts, nullptr);
        waitpid(child, &childStatus, WNOHANG);
    }

    if (written == 0) {
        if (WIFEXITED(childStatus) && WEXITSTATUS(childStatus) == 127)
            fprintf(f, "  (logcat exec failed — no capture)\n");
        else if (WIFEXITED(childStatus) && WEXITSTATUS(childStatus) == 126)
            fprintf(f, "  (dup2 failed in capture child — no capture)\n");
        else if (WIFSIGNALED(childStatus))
            fprintf(f, "  (logcat died on signal %d)\n", WTERMSIG(childStatus));
    } else if (budgetHit) {
        fprintf(f, "\n  (capture stopped at %zu bytes — the server slices the "
                   "report at 5000 chars on a duplicate issue, so anything past "
                   "here would not have survived submission)\n", byteBudget);
    } else {
        fprintf(f, "  (end of capture)\n");
    }
    return written;
}

// ART's own account of why it aborted. Written BEFORE our backtrace, which is
// deliberate and is the other half of the #1745 fix: when ART aborts, our
// backtrace names the JNI call that happened to hit the ceiling (a battery poll
// in #1408/#1572/#1745) and ART's dump names the actual leak, so on a budget
// that cuts from the end, ART's dump is what has to go first. Returns false
// when there were no fatal entries, which is the normal case for SIGSEGV and
// friends — the caller then falls back to the unfiltered tail, after the
// backtrace, where it costs the backtrace nothing.
static bool appendArtAbortMessageToFile(FILE* f)
{
    fprintf(f, "\nART abort message (logcat, fatal priority only):\n");
    if (captureLogcatToFile(f, /*fatalOnly=*/true, kFatalCaptureBudget) > 0)
        return true;
    fprintf(f, "  (no fatal-priority entries for this pid — not an ART abort, "
               "or logd rotated them out)\n");
    return false;
}

static void appendLogcatTailToFile(FILE* f)
{
    fprintf(f, "\nSystem log tail (logcat):\n");
    if (captureLogcatToFile(f, /*fatalOnly=*/false, kFatalCaptureBudget) == 0)
        fprintf(f, "  (logcat wrote nothing — entries for this pid may have "
                   "rotated out of logd)\n");
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
    fprintf(f, "=== CRASH REPORT ===\n");
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

    // Last debug message
    if (s_lastDebugMessage[0] != '\0') {
        fprintf(f, "\nLast debug message:\n  %s\n", s_lastDebugMessage);
    }

#ifdef Q_OS_ANDROID
    // Ahead of the backtrace: see appendArtAbortMessageToFile(). Only into
    // crash.log (which becomes the report's "Crash Log" section) — not into the
    // debug.log copy below, whose tail is submitted separately.
    const bool artAborted = appendArtAbortMessageToFile(f);
#endif

    // Write backtrace
#if defined(Q_OS_ANDROID) || defined(Q_OS_LINUX) || defined(Q_OS_WIN) || defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    writeBacktraceToFile(f);
#else
    fprintf(f, "\nBacktrace: not available on this platform\n");
#endif

#ifdef Q_OS_ANDROID
    // Only when ART did not explain itself. An ART abort has already spent the
    // budget on the fatal block, and the unfiltered tail there is the thread
    // dump that #1745 wasted its whole report on.
    if (!artAborted)
        appendLogcatTailToFile(f);
#endif

    fprintf(f, "\n=== END CRASH REPORT ===\n");
    fflush(f);
    fclose(f);

    // Also append to debug.log for persistence
    if (s_debugLogPath[0] != '\0') {
        FILE* debugLog = fopen(s_debugLogPath, "a");
        if (debugLog) {
            fprintf(debugLog, "\n\n=== CRASH REPORT ===\n");
            fprintf(debugLog, "Signal: %d (%s)\n", signal, signalName);
            fprintf(debugLog, "Time: %s", ctime(&now));
            if (s_lastDebugMessage[0] != '\0') {
                fprintf(debugLog, "\nLast debug message:\n  %s\n", s_lastDebugMessage);
            }
#if defined(Q_OS_ANDROID) || defined(Q_OS_LINUX) || defined(Q_OS_WIN) || defined(Q_OS_MACOS) || defined(Q_OS_IOS)
            writeBacktraceToFile(debugLog);
#endif
            fprintf(debugLog, "\n=== END CRASH REPORT ===\n");
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

    // Also set up debug.log path for persistent crash logging
    QString debugPath = dataPath + "/debug.log";
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
        // Fallback if install() wasn't called yet
        QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        debugPath = dataPath + "/debug.log";
    }

    QFile file(debugPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }

    // Read all lines, dropping any crash-report block.
    //
    // This tail is submitted as its own field (debugLogTail, sliced to 5000
    // chars server-side) to answer a question the crash log cannot: what the
    // app was doing before it died. Crash-report text in here answers nothing,
    // because the same text is already being submitted as crashLog.
    //
    // Two writers put it there, and both land at the very end of debug.log —
    // exactly where this tail reads:
    //   - writeCrashLog() appends the whole report for persistence, at crash
    //     time, so it is the last thing in the file;
    //   - main.cpp re-logs the previous run's crash log at startup (a single
    //     qWarning record, so only its first line carries a log prefix).
    // In #1745 the result was a 5000-char debug tail containing one stale crash
    // report and not a single line of app narrative.
    static const QLatin1String kBlockStart("=== CRASH REPORT ===");
    static const QLatin1String kBlockEnd("=== END CRASH REPORT ===");

    QStringList allLines;
    bool insideCrashBlock = false;
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        if (line.contains(kBlockEnd)) {
            insideCrashBlock = false;
            continue;
        }
        if (line.contains(kBlockStart)) {
            insideCrashBlock = true;
            continue;
        }
        if (!insideCrashBlock)
            allLines.append(line);
    }
    file.close();

    // Get last N lines
    qsizetype startIndex = qMax(qsizetype(0), allLines.size() - lines);
    QStringList tailLines = allLines.mid(startIndex);

    return tailLines.join("\n");
}
