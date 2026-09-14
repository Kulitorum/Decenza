#include "core/diagnosticlogging.h"
#include "crashhandler.h"
#include "logpaths.h"
#include "mcp/mcplogfilter.h"
#include "version.h"

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
#include <atomic>

#if defined(Q_OS_MACOS) || defined(Q_OS_IOS) || defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
#include <pthread.h>
#endif

#ifdef Q_OS_ANDROID
#include <unwind.h>
#include <dlfcn.h>
#include <link.h>
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

#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
#include <cxxabi.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <sys/ucontext.h>
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
// "<version> build <code>", precomputed in install(). The report title carries
// only the version, and one version ships many builds.
static char s_buildLine[64] = {0};
// "<image> <UUID>" of the image holding this code, precomputed in install() on
// Apple platforms: the dSYM for frames in that image. Frames in other images are
// identified by the backtrace's own binary-images list.
static char s_imageUuid[128] = {0};

#ifdef Q_OS_ANDROID
// "--pid=<N>" argument for logcat, precomputed in install() so the signal
// handler never has to format it.
static char s_logcatPidArg[32] = {0};
#endif

// Store recent debug messages for context
static QtMessageHandler s_previousHandler = nullptr;

#if defined(Q_OS_ANDROID) || defined(Q_OS_MACOS) || defined(Q_OS_IOS)
// The directory before an image name (~150 characters per frame for an APK path)
// spends a budgeted report on nothing the name itself does not identify.
static const char* moduleBaseName(const char* path)
{
    if (!path || !*path)
        return "(main)";
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}
#endif

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

// Each frame as <module>+<offset>, the address llvm-symbolizer --obj=<module>
// resolves offline. dladdr names only exported symbols, and most of Qt's are
// hidden (#1937).
struct FrameLocation {
    const char* module = nullptr;
    uintptr_t offset = 0;
};

struct FrameLookup {
    void* const* pcs;
    size_t count;
    FrameLocation* out;
};

static int locateFrames(struct dl_phdr_info* info, size_t, void* data)
{
    auto* lookup = static_cast<FrameLookup*>(data);
    for (size_t i = 0; i < lookup->count; ++i) {
        if (lookup->out[i].module)
            continue;
        const uintptr_t pc = reinterpret_cast<uintptr_t>(lookup->pcs[i]);
        for (ElfW(Half) h = 0; h < info->dlpi_phnum; ++h) {
            const ElfW(Phdr)& ph = info->dlpi_phdr[h];
            if (ph.p_type != PT_LOAD)
                continue;
            const uintptr_t start = info->dlpi_addr + ph.p_vaddr;
            if (pc >= start && pc < start + ph.p_memsz) {
                lookup->out[i].module = info->dlpi_name;
                lookup->out[i].offset = pc - info->dlpi_addr;
                break;
            }
        }
    }
    return 0;
}

static void writeBacktraceToFile(FILE* f)
{
    void* buffer[64];
    size_t count = captureBacktrace(buffer, 64);

    FrameLocation locations[64];
    FrameLookup lookup = {buffer, count, locations};
    dl_iterate_phdr(locateFrames, &lookup);

    // The frame after __kernel_rt_sigreturn is the faulting instruction; frames
    // after it are return addresses, whose call is the instruction before. In
    // #1937, #5 disassembles to the faulting stlxr and #6 to the insn after a bl.
    fprintf(f, "\nBacktrace (%zu frames, module+offset):\n", count); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
    for (size_t i = 0; i < count; ++i) {
        if (locations[i].module) {
            fprintf(f, "  #%zu: %s+0x%zx", i, moduleBaseName(locations[i].module), // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
                    static_cast<size_t>(locations[i].offset));
        } else {
            fprintf(f, "  #%zu: %p (not in a loaded module)", i, buffer[i]); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
        }

        Dl_info info;
        if (dladdr(buffer[i], &info) && info.dli_sname) {
            int status = 0;
            char* demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
            const char* name = (status == 0 && demangled) ? demangled : info.dli_sname;
            fprintf(f, " %s + %td", name, // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
                    static_cast<char*>(buffer[i]) - static_cast<char*>(info.dli_saddr));
            if (demangled) free(demangled);
        }
        fputc('\n', f);
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
// and 29-frame backtrace were ~4117 chars; Android frames have since dropped the
// APK path (~240 chars each in #1937, ~80 now), so that figure is an upper bound.
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
        fprintf(f, "  (end of capture)\n"); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
        break;
    case CaptureOutcome::BudgetHit:
        fprintf(f, "\n  (capture stopped at %zu bytes — see the budget note in " // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
                   "crashhandler.cpp; the rest would not have survived the "
                   "server's slice)\n", byteBudget);
        break;
    case CaptureOutcome::NoEntries:
        fprintf(f, "  (logcat ran and had nothing to report for this pid)\n"); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
        break;
    case CaptureOutcome::ExecFailed:
        fprintf(f, "  (logcat exec failed — no capture)\n"); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
        break;
    case CaptureOutcome::Dup2Failed:
        fprintf(f, "  (dup2 failed in capture child — no capture)\n"); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
        break;
    case CaptureOutcome::PipeFailed:
        fprintf(f, "  (pipe failed — no capture)\n"); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
        break;
    case CaptureOutcome::ForkFailed:
        fprintf(f, "  (fork failed — no capture)\n"); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
        break;
    case CaptureOutcome::SetupFailed:
        fprintf(f, "  (could not set the capture pipe non-blocking — skipped " // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
                   "rather than risk an unbounded read in the signal handler)\n");
        break;
    case CaptureOutcome::ReadFailed:
        fprintf(f, "\n  (capture failed after %zu bytes — output above is " // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
                   "incomplete)\n", bytes);
        break;
    case CaptureOutcome::TimedOut:
        fprintf(f, "\n  (capture killed after 3s with %zu bytes — output above " // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
                   "is truncated mid-stream, not complete)\n", bytes);
        break;
    case CaptureOutcome::ChildLost:
        fprintf(f, "\n  (lost track of the capture child after %zu bytes and " // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
                   "killed it — output above may be incomplete)\n", bytes);
        break;
    case CaptureOutcome::Unknown:
        fprintf(f, "  (capture produced %zu bytes and ended unexplained; child " // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
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
    fprintf(f, "\nART abort message (logcat, fatal priority only):\n"); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
    size_t bytes = 0;
    int rawStatus = 0;
    const CaptureOutcome outcome = captureLogcatToFile(
        f, /*fatalOnly=*/true, kFatalCaptureBudget, &bytes, &rawStatus);
    writeCaptureMarker(f, outcome, bytes, kFatalCaptureBudget, rawStatus);
    if (outcome == CaptureOutcome::NoEntries)
        fprintf(f, "  (so this was not an ART abort, or logd rotated its " // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
                   "entries out)\n");
    return outcome;
}

static void appendLogcatTailToFile(FILE* f, size_t byteBudget)
{
    fprintf(f, "\nSystem log tail (logcat):\n"); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
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

    fprintf(f, "\nBacktrace (%d frames):\n", count); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
    for (int i = 0; i < count; ++i) {
        fprintf(f, "  #%d: %s\n", i, symbols[i] ? symbols[i] : "???"); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
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

    fprintf(f, "\nBacktrace (%d frames):\n", frames); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging

    SYMBOL_INFO* symbol = (SYMBOL_INFO*)calloc(sizeof(SYMBOL_INFO) + 256, 1);
    symbol->MaxNameLen = 255;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

    for (USHORT i = 0; i < frames; ++i) {
        SymFromAddr(process, (DWORD64)buffer[i], nullptr, symbol);
        fprintf(f, "  #%d: 0x%p %s\n", i, buffer[i], symbol->Name); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
    }

    free(symbol);
    SymCleanup(process);
}
#endif

#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
struct LoadedImage {
    const mach_header_64* header = nullptr;
    uintptr_t slide = 0;
    const char* path = nullptr;
};

// The first load command of header that accept() takes, or nullptr.
template <typename Accept>
static const load_command* findLoadCommand(const mach_header_64* header, Accept accept)
{
    auto* cmd = reinterpret_cast<const load_command*>(header + 1);
    for (uint32_t c = 0; c < header->ncmds; ++c) {
        if (accept(cmd))
            return cmd;
        cmd = reinterpret_cast<const load_command*>(reinterpret_cast<const char*>(cmd) + cmd->cmdsize);
    }
    return nullptr;
}

// The loaded image with a mapped segment containing address. Header, slide and
// name are read at one index, so an image loaded or unloaded meanwhile cannot mix
// two images. No-access segments are skipped: an executable's __PAGEZERO maps
// nothing but spans 4 GB from its slide, and would claim those addresses.
static bool imageContaining(uintptr_t address, LoadedImage* image)
{
    const uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; ++i) {
        const auto* header = reinterpret_cast<const mach_header_64*>(_dyld_get_image_header(i));
        if (!header || header->magic != MH_MAGIC_64)
            continue;
        const uintptr_t slide = static_cast<uintptr_t>(_dyld_get_image_vmaddr_slide(i));
        const auto mapsAddress = [address, slide](const load_command* cmd) {
            if (cmd->cmd != LC_SEGMENT_64)
                return false;
            const auto* segment = reinterpret_cast<const segment_command_64*>(cmd);
            const uintptr_t start = static_cast<uintptr_t>(segment->vmaddr) + slide;
            return segment->initprot != 0 && address >= start && address - start < segment->vmsize;
        };
        if (findLoadCommand(header, mapsAddress)) {
            *image = {header, slide, _dyld_get_image_name(i)};
            return true;
        }
    }
    return false;
}

static const char* imageName(const LoadedImage& image)
{
    return image.path && *image.path ? moduleBaseName(image.path) : "(unnamed image)";
}

static void formatImageUuid(const mach_header_64* header, char* out, size_t size)
{
    const load_command* cmd = findLoadCommand(header, [](const load_command* c) { return c->cmd == LC_UUID; });
    if (!cmd) {
        snprintf(out, size, "(no UUID)");
        return;
    }
    const uint8_t* u = reinterpret_cast<const uuid_command*>(cmd)->uuid;
    snprintf(out, size, "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
             u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7],
             u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
}

static int describeInImage(char* out, size_t size, uintptr_t address, const LoadedImage& image)
{
    return snprintf(out, size, "%s 0x%lx", imageName(image), static_cast<unsigned long>(address - image.slide));
}

int CrashHandler::describeCodeAddress(void* pc, char* out, size_t size)
{
    LoadedImage image;
    if (!imageContaining(reinterpret_cast<uintptr_t>(pc), &image))
        return snprintf(out, size, "%p (not in a loaded image)", pc);
    return describeInImage(out, size, reinterpret_cast<uintptr_t>(pc), image);
}

// The image holding this code, whose dSYM symbolicates its frames.
static const mach_header_64* s_ownImage = nullptr;

static void recordOwnImage()
{
    LoadedImage image;
    if (!imageContaining(reinterpret_cast<uintptr_t>(&recordOwnImage), &image)) {
        snprintf(s_imageUuid, sizeof(s_imageUuid), "(this binary's image was not found)");
        return;
    }
    s_ownImage = image.header;
    char uuid[40];
    formatImageUuid(image.header, uuid, sizeof(uuid));
    snprintf(s_imageUuid, sizeof(s_imageUuid), "%s %s", imageName(image), uuid);
}

static void writeBacktraceToFile(FILE* f)
{
    void* buffer[64];
    const int count = backtrace(buffer, 64);

    // dladdr returns the closest symbol below an address with no size bound, so in a
    // stripped image the name can belong to another function (#1777:
    // "...QBluetoothPermission...metaObjectFunction + 1076920"). None is printed for
    // this binary, whose dSYM has the real one; elsewhere only within 64 KB, as "near".
    constexpr ptrdiff_t kNearSymbolRange = 0x10000;

    // Each distinct image is listed after the frames with its UUID. A shared-cache
    // system library has no file of its own: its UUID says which build of it an
    // unslid address belongs to.
    LoadedImage images[64];
    int imageCount = 0;

    fprintf(f, "\nBacktrace (%d return addresses, each the instruction after its call; image and unslid address):\n", count); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
    for (int i = 0; i < count; ++i) {
        const uintptr_t address = reinterpret_cast<uintptr_t>(buffer[i]);
        LoadedImage image;
        if (!imageContaining(address, &image)) {
            fprintf(f, "  #%d: %p (not in a loaded image)\n", i, buffer[i]); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
            continue;
        }
        char where[256];
        describeInImage(where, sizeof(where), address, image);
        fprintf(f, "  #%d: %s", i, where); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging

        int known = 0;
        while (known < imageCount && images[known].header != image.header)
            ++known;
        if (known == imageCount)
            images[imageCount++] = image;

        Dl_info info;
        if (image.header != s_ownImage && dladdr(buffer[i], &info) && info.dli_sname) {
            const ptrdiff_t offset = static_cast<char*>(buffer[i]) - static_cast<char*>(info.dli_saddr);
            if (offset >= 0 && offset < kNearSymbolRange) {
                int status = 0;
                char* demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
                fprintf(f, " near %s + %td", (status == 0 && demangled) ? demangled : info.dli_sname, offset); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
                free(demangled);
            }
        }
        fputc('\n', f);
    }

    fprintf(f, "\nBinary images (name, UUID, unslid load address):\n"); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
    for (int k = 0; k < imageCount; ++k) {
        char uuid[40];
        formatImageUuid(images[k].header, uuid, sizeof(uuid));
        fprintf(f, "  %s %s 0x%lx\n", imageName(images[k]), uuid, // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
                static_cast<unsigned long>(reinterpret_cast<uintptr_t>(images[k].header) - images[k].slide));
    }
}
#endif

void CrashHandler::writeCrashLog(int signal, const char* signalName, void* faultPc)
{
    // Open crash log file (using raw C file I/O - safer in signal handler)
    FILE* f = fopen(s_crashLogPath, "w");
    if (!f) return;

#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    char faultWhere[256] = "(unknown)";
    if (faultPc)
        describeCodeAddress(faultPc, faultWhere, sizeof(faultWhere));
#else
    Q_UNUSED(faultPc);
#endif

    // Write crash header
    fprintf(f, "%s\n", kReportStart); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
    fprintf(f, "Signal: %d (%s)\n", signal, signalName); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
    if (s_buildLine[0] != '\0')
        fprintf(f, "Build: %s\n", s_buildLine); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
    if (s_imageUuid[0] != '\0')
        fprintf(f, "Image: %s\n", s_imageUuid); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    fprintf(f, "Fault pc: %s\n", faultWhere); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
#endif

    // Get current time (basic, signal-safe-ish)
    time_t now = time(nullptr);
    fprintf(f, "Time: %s", ctime(&now));  // ctime adds newline // log-marker-exempt: crash/abort report writer cannot reenter Qt logging

    // Thread info — critical for diagnosing render thread crashes
    char threadName[64] = {0};
#if defined(Q_OS_MACOS) || defined(Q_OS_IOS) || defined(Q_OS_LINUX)
    pthread_getname_np(pthread_self(), threadName, sizeof(threadName));
    fprintf(f, "Thread: %p name=\"%s\"\n", // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
            (void*)pthread_self(), threadName[0] ? threadName : "(unnamed)");
#elif defined(Q_OS_WIN)
    fprintf(f, "Thread: %lu\n", (unsigned long)GetCurrentThreadId()); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
#endif

    // Last debug message, CAPPED. s_lastDebugMessage is char[4096] and holds
    // whatever the last qDebug was — this app logs profile JSON and HTTP bodies,
    // so an unbounded copy can spend most of the 5000-char head slice before the
    // ART capture below has written a byte, and the report ends up looking
    // exactly like #1745 again. The first line is the part that has ever been
    // diagnostic (in #1745 it named the screensaver transition count).
    if (s_lastDebugMessage[0] != '\0') {
        constexpr int kLastMessageMax = 512;
        fprintf(f, "\nLast debug message:\n  %.*s%s\n", // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
                kLastMessageMax, s_lastDebugMessage,
                s_lastDebugMessage[kLastMessageMax] != '\0' ? " …(truncated)" : "");
    }

    // On disk before the ART capture and the backtrace, which walk images and
    // demangle on a possibly-corrupt heap: a fault there would leave crash.log empty.
    fflush(f);

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
    fprintf(f, "\nBacktrace: not available on this platform\n"); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
#endif

#ifdef Q_OS_ANDROID
    // Only when ART ran and had nothing to say. Skipped when ART DID explain
    // itself (the budget is spent, and the unfiltered tail there is the thread
    // dump #1745 wasted its whole report on) and equally when the capture
    // FAILED — the fallback runs the same binary the same way, so it would fail
    // identically and stack a second, contradictory marker under the first.
    //
    // Smaller budget than the fatal pass: every byte here is up to 3s of
    // signal-handler time on a machine that may be mid-shot.
    if (artOutcome == CaptureOutcome::NoEntries)
        appendLogcatTailToFile(f, 1500);
#endif

    fprintf(f, "\n%s\n", kReportEnd); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
    fflush(f);
    fclose(f);

    // Also append to debug.log for persistence
    if (s_debugLogPath[0] != '\0') {
        FILE* debugLog = fopen(s_debugLogPath, "a");
        if (debugLog) {
            fprintf(debugLog, "\n\n%s\n", kReportStart); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
            fprintf(debugLog, "Signal: %d (%s)\n", signal, signalName); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
            if (s_buildLine[0] != '\0')
                fprintf(debugLog, "Build: %s\n", s_buildLine); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
            if (s_imageUuid[0] != '\0')
                fprintf(debugLog, "Image: %s\n", s_imageUuid); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
            fprintf(debugLog, "Fault pc: %s\n", faultWhere); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
#endif
            fprintf(debugLog, "Time: %s", ctime(&now)); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
            if (s_lastDebugMessage[0] != '\0') {
                fprintf(debugLog, "\nLast debug message:\n  %s\n", s_lastDebugMessage); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
            }
            // Flushed before the backtrace, which runs dl_iterate_phdr, dladdr and
            // a malloc'ing demangler on a possibly-corrupt heap: if it faults, the
            // start marker that getDebugLogTail() anchors on is already on disk.
            fflush(debugLog);
#if defined(Q_OS_ANDROID) || defined(Q_OS_LINUX) || defined(Q_OS_WIN) || defined(Q_OS_MACOS) || defined(Q_OS_IOS)
            writeBacktraceToFile(debugLog);
#endif
            fprintf(debugLog, "\n%s\n", kReportEnd); // log-marker-exempt: crash/abort report writer cannot reenter Qt logging
            fflush(debugLog);
            fclose(debugLog);
        }
    }
}

#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
void CrashHandler::signalActionHandler(int signal, siginfo_t*, void* context)
{
    void* pc = nullptr;
    const auto* uc = static_cast<ucontext_t*>(context);
    if (uc && uc->uc_mcontext) {
#if defined(__arm64__)
        pc = reinterpret_cast<void*>(__darwin_arm_thread_state64_get_pc(uc->uc_mcontext->__ss));
#elif defined(__x86_64__)
        pc = reinterpret_cast<void*>(uc->uc_mcontext->__ss.__rip);
#endif
    }
    handleSignal(signal, pc);
}
#else
void CrashHandler::signalHandler(int signal)
{
    handleSignal(signal, nullptr);
}
#endif

void CrashHandler::handleSignal(int signal, void* faultPc)
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
    writeCrashLog(signal, signalName, faultPc);

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

    snprintf(s_buildLine, sizeof(s_buildLine), "%s build %d", VERSION_STRING, versionCode());
#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    recordOwnImage();
#endif

#ifdef Q_OS_ANDROID
    snprintf(s_logcatPidArg, sizeof(s_logcatPidArg), "--pid=%d", getpid());
#endif

    DIAG_DEBUG(APP, "CrashHandler") << "Installing signal handlers, crash log path:" << logPath;

    // Install message handler to capture last debug message
    s_previousHandler = qInstallMessageHandler(crashMessageHandler);

    // Install signal handlers
#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    struct sigaction action {};
    action.sa_sigaction = signalActionHandler;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    for (const int s : {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL})
        sigaction(s, &action, nullptr);
#else
    std::signal(SIGSEGV, signalHandler);
    std::signal(SIGABRT, signalHandler);
#ifdef SIGBUS
    std::signal(SIGBUS, signalHandler);
#endif
    std::signal(SIGFPE, signalHandler);
    std::signal(SIGILL, signalHandler);
#endif
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

CrashHandler::PreviousCrash CrashHandler::previousCrash()
{
    const QString path = crashLogPath();
    if (!QFile::exists(path))
        return PreviousCrash::None;

    // A crash after main() returned is C++ runtime cleanup: not actionable, so
    // the report is deleted rather than shown.
    QFile file(path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString content = QString::fromUtf8(file.readAll());
        file.close();
        if (content.contains("main() returned"))
            return QFile::remove(path) ? PreviousCrash::DiscardedOnExit : PreviousCrash::DiscardFailed;
    }
    return PreviousCrash::Pending;
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

QString CrashHandler::getDebugLogTail(qsizetype charBudget)
{
    QString debugPath = QString::fromUtf8(s_debugLogPath);
    if (debugPath.isEmpty()) {
        // Fallback if install() wasn't called yet. Must resolve the same way
        // install() does — see the note there on the two-debug.log bug.
        debugPath = DecenzaPaths::logsDirectory() + "/debug.log";
    }

    QFile file(debugPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("(debug.log could not be read: %1)").arg(file.errorString());

    // Read all lines, dropping any crash-report block.
    //
    // This tail is submitted as its own field to answer a question the crash log
    // cannot: what the app was doing before it died. Crash-report text in here
    // answers nothing, because the same text is already submitted as crashLog.
    //
    // Two writers put report text in this file:
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
    qsizetype crashAnchor = -1;  // where the last writeCrashLog() block began, in allLines
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
            // Only writeCrashLog()'s own copy puts the marker at the start of a
            // line: WebDebugLogger::handleMessage() prefixes every physical line
            // of main.cpp's re-log.
            if (line.startsWith(kBlockStart))
                crashAnchor = allLines.size();
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

    // The crashed run ends where its crash-time block begins. The last session is
    // not enough: a launch that showed the report and closed before the user
    // answered leaves crash.log in place and adds a session that did not crash.
    // Anything restored from an unclosed block lies past the anchor and is cut.
    QString notes;
    qsizetype end = allLines.size();
    if (crashAnchor >= 0) {
        end = crashAnchor;
    } else {
        notes += QStringLiteral("(This crash's block is not in debug.log; showing the last run "
                                "logged, which may not be the one that crashed.)\n");
    }

    // The marker stays: its wall-clock start dates the elapsed-time prefixes.
    const QString& sessionMarker = McpLogFilter::sessionStartMarker();
    qsizetype start = -1;
    for (qsizetype i = end - 1; i >= 0; --i) {
        if (allLines[i].startsWith(sessionMarker)) {
            start = i;
            break;
        }
    }
    if (start < 0) {
        start = 0;
        notes += QStringLiteral("(This run's session start is not in debug.log, so its start "
                                "time is unknown.)\n");
    }

    QString narrative = selectCrashNarrative(allLines.mid(start, end - start),
                                             charBudget - notes.size());
    if (narrative.isEmpty())
        narrative = QStringLiteral("(The crashed run wrote no log lines.)");
    return (notes + narrative).left(charBudget);
}

QString CrashHandler::selectCrashNarrative(const QStringList& lines, qsizetype charBudget)
{
    using McpLogFilter::LineMatch;

    // A plain tail spends a 5000-char field on whatever came last; #1937's
    // repeated one warning six times.
    constexpr qsizetype kTailEntries = 20;
    constexpr qsizetype kMaxLineChars = 300;

    QStringList nonBlank;
    nonBlank.reserve(lines.size());
    for (const QString& line : lines) {
        if (!line.trimmed().isEmpty())
            nonBlank.append(line);
    }
    const QList<LineMatch> entries = McpLogFilter::dedupeConsecutive(
        McpLogFilter::filterLines(nonBlank, 0, QString(), false, QString()));
    const qsizetype n = entries.size();
    if (n == 0)
        return QString();

    QStringList rendered;
    QList<int> ranks;
    rendered.reserve(n);
    ranks.reserve(n);
    // WARN and above end with " {category=… source=file:line}" (WebDebugLogger's
    // diagnosticContext()). A cut keeps that suffix: it is what locates the line.
    const auto clip = [](const QString& text) {
        if (text.size() <= kMaxLineChars)
            return text;
        const qsizetype context = text.endsWith(QLatin1Char('}'))
            ? text.lastIndexOf(QStringLiteral(" {")) : -1;
        const QString suffix = context > 0 ? text.mid(context) : QString();
        const qsizetype keep = qMax(qsizetype(80), kMaxLineChars - suffix.size());
        return text.left(qMin(context > 0 ? context : text.size(), keep))
            + QStringLiteral(" …") + suffix;
    };
    for (const LineMatch& e : entries) {
        QString text = clip(e.text);
        if (e.count > 1)
            text += QStringLiteral(" (x%1)").arg(e.count);
        rendered.append(text);
        ranks.append(McpLogFilter::levelRank(McpLogFilter::lineLevel(e.text)));
    }

    // Priority: the session marker, the last lines, then each level from FATAL
    // down to DEBUG, newest first within a level.
    QList<qsizetype> order;
    for (qsizetype i = 0; i < n; ++i) {
        if (entries[i].text.startsWith(McpLogFilter::sessionStartMarker()))
            order.append(i);
    }
    for (qsizetype i = n - 1; i >= qMax(qsizetype(0), n - kTailEntries); --i)
        order.append(i);
    const qsizetype tailEnd = order.size();
    for (int rank = McpLogFilter::levelRank(QStringLiteral("FATAL")); rank >= 0; --rank) {
        for (qsizetype i = n - 1; i >= 0; --i) {
            if (ranks[i] == rank)
                order.append(i);
        }
    }

    const QString header = QStringLiteral(
        "(Selected from %1 lines of the crashed run: the last lines, then errors, "
        "warnings, info and debug, newest first. Gaps are marked.)\n").arg(nonBlank.size());
    const qsizetype budget = charBudget - header.size();

    QList<bool> chosen(n, false);
    QList<qsizetype> picks;          // priority order, so trimming drops the least wanted
    QList<qsizetype> earlier(n, 0);  // older identical lines folded into a pick
    QHash<QString, qsizetype> pickByText;
    qsizetype used = 0;
    for (qsizetype k = 0; k < order.size(); ++k) {
        const qsizetype i = order[k];
        if (chosen[i])
            continue;
        const QString key = McpLogFilter::stripTimestampPrefix(entries[i].text);
        // A repeated line is one fact; past the tail, its older copies are a
        // count on the newest rather than more of the budget.
        if (k >= tailEnd) {
            const auto it = pickByText.constFind(key);
            if (it != pickByText.cend()) {
                earlier[*it] += entries[i].count;
                continue;
            }
        }
        const qsizetype cost = rendered[i].size() + 1;
        if (used + cost > budget)
            continue;
        chosen[i] = true;
        picks.append(i);
        used += cost;
        if (!pickByText.contains(key))
            pickByText.insert(key, i);
    }

    // Gap markers and "(+N earlier)" are not in the estimate above, so assemble
    // for real and give back the least wanted picks until it fits.
    const auto assemble = [&]() {
        QString out;
        if (picks.size() < n)
            out = header;
        qsizetype nextLine = 0;
        for (qsizetype i = 0; i < n; ++i) {
            if (!chosen[i])
                continue;
            if (entries[i].line > nextLine)
                out += QStringLiteral("  … %1 lines omitted …\n").arg(entries[i].line - nextLine);
            out += rendered[i];
            if (earlier[i] > 0)
                out += QStringLiteral(" (+%1 earlier)").arg(earlier[i]);
            out += QLatin1Char('\n');
            nextLine = entries[i].lastLine + 1;
        }
        if (nextLine < nonBlank.size())
            out += QStringLiteral("  … %1 lines omitted …\n").arg(nonBlank.size() - nextLine);
        out.chop(1);
        return out;
    };

    QString out = assemble();
    while (out.size() > charBudget && !picks.isEmpty()) {
        chosen[picks.takeLast()] = false;
        out = assemble();
    }
    // Only a budget smaller than the header and one gap marker gets here.
    return out.left(charBudget);
}
