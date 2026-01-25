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

#ifdef Q_OS_ANDROID
#include <unwind.h>
#include <dlfcn.h>
#include <cxxabi.h>
#endif

#if (defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)) || defined(Q_OS_MACOS)
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

#ifdef Q_OS_MACOS
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

    // Last debug message
    if (s_lastDebugMessage[0] != '\0') {
        fprintf(f, "\nLast debug message:\n  %s\n", s_lastDebugMessage);
    }

    // Write backtrace
#if defined(Q_OS_ANDROID) || defined(Q_OS_LINUX) || defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    writeBacktraceToFile(f);
#else
    fprintf(f, "\nBacktrace: not available on this platform\n");
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
#if defined(Q_OS_ANDROID) || defined(Q_OS_LINUX) || defined(Q_OS_WIN) || defined(Q_OS_MACOS)
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
    int startIndex = qMax(0, allLines.size() - lines);
    QStringList tailLines = allLines.mid(startIndex);

    return tailLines.join("\n");
}
