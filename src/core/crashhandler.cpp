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

// Append the tail of this process's logcat to the crash log. The point is the
// abort message: when ART kills us (JNI errors like global reference table
// overflow, CheckJNI failures), it logs the FATAL reason — and for ref-table
// overflow, a dump of the table's dominant classes — to logd *before* raising
// SIGABRT. Our own qDebug log never sees that text, so without this section a
// SIGABRT-from-ART report shows where we were, but not why ART aborted (#1408).
//
// An app may always read its own logs (logd filters by UID, no READ_LOGS
// needed). fork() in a signal handler is not strictly async-signal-safe, but
// between fork and exec the child calls only dup2/execl/_exit (all AS-safe;
// the fd is captured before the fork) and the rest of this crash handler
// already relies on far less safe machinery (stdio, demangling).
//
// Every outcome writes a distinct marker line: this section exists to explain
// crashes, so an empty section must be attributable (exec denied vs. logd
// rotated this pid out vs. capture killed) rather than read as "no entries".
static void appendLogcatTailToFile(FILE* f)
{
    fprintf(f, "\nSystem log tail (logcat, includes ART abort message if any):\n");
    // Flush stdio before the child writes to the shared file description so
    // output doesn't interleave out of order.
    fflush(f);
    const int outFd = fileno(f);
    const off_t startOffset = lseek(outFd, 0, SEEK_CUR);

    pid_t child = fork();
    if (child == 0) {
        if (dup2(outFd, STDOUT_FILENO) < 0 || dup2(outFd, STDERR_FILENO) < 0)
            _exit(126);
        execl("/system/bin/logcat", "logcat", "-d", "-t", "200",
              s_logcatPidArg, static_cast<char*>(nullptr));
        _exit(127);
    }
    if (child < 0) {
        fprintf(f, "  (fork failed, no logcat capture)\n");
        return;
    }

    // Bounded wait: logcat -d exits almost immediately, but never let a wedged
    // child hang the crash handler. ~3 s cap, then kill and move on.
    for (int i = 0; i < 60; ++i) {
        int status = 0;
        pid_t r = waitpid(child, &status, WNOHANG);
        if (r == child) {
            if (WIFEXITED(status) && WEXITSTATUS(status) == 127)
                fprintf(f, "  (logcat exec failed — no capture)\n");
            else if (WIFEXITED(status) && WEXITSTATUS(status) == 126)
                fprintf(f, "  (dup2 failed in capture child — no capture)\n");
            else if (WIFSIGNALED(status))
                fprintf(f, "  (logcat died on signal %d)\n", WTERMSIG(status));
            else if (lseek(outFd, 0, SEEK_CUR) == startOffset)
                fprintf(f, "  (logcat ran but wrote nothing — entries for this pid may have rotated out of logd)\n");
            else
                fprintf(f, "  (end of logcat tail)\n");
            return;
        }
        if (r < 0) {
            // ECHILD (e.g. SIGCHLD set to SIG_IGN elsewhere in the process):
            // we can no longer observe the child, so make sure it isn't still
            // writing into this file while we finish the report.
            kill(child, SIGKILL);
            fprintf(f, "  (waitpid failed, errno=%d — log tail above may be incomplete)\n", errno);
            return;
        }
        struct timespec ts = {0, 50 * 1000 * 1000};  // 50 ms
        nanosleep(&ts, nullptr);
    }
    // Timed out. Kill and reap best-effort only — a blocking waitpid here could
    // hang the whole crash handler on a child stuck in uninterruptible sleep,
    // which on a distressed device is exactly when this code runs.
    kill(child, SIGKILL);
    struct timespec ts = {0, 50 * 1000 * 1000};  // 50 ms
    nanosleep(&ts, nullptr);
    waitpid(child, nullptr, WNOHANG);
    fprintf(f, "  (logcat killed after 3s — output above may be truncated or missing)\n");
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

    // Write backtrace
#if defined(Q_OS_ANDROID) || defined(Q_OS_LINUX) || defined(Q_OS_WIN) || defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    writeBacktraceToFile(f);
#else
    fprintf(f, "\nBacktrace: not available on this platform\n");
#endif

#ifdef Q_OS_ANDROID
    // Only into crash.log (which becomes the report's "Crash Log" section) —
    // not into the debug.log copy below, whose tail is submitted separately.
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

    // Read all lines and get the last N
    QStringList allLines;
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        allLines.append(stream.readLine());
    }
    file.close();

    // Get last N lines
    qsizetype startIndex = qMax(qsizetype(0), allLines.size() - lines);
    QStringList tailLines = allLines.mid(startIndex);

    return tailLines.join("\n");
}
