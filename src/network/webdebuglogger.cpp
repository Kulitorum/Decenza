#include "webdebuglogger.h"

#include "mcp/mcplogfilter.h"

#include <QDebug>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>

namespace {
const QString kSessionMarker = QStringLiteral("========== SESSION START:");
// Written by trimLogFile() at the head of what survives a trim. Named because
// it is produced at one site and READ at another (the headless-fragment scan in
// sessionIndex(), which must not mistake it for log content) — and a banner
// those two spelled differently would resurrect the phantom session it exists
// to keep out.
const QString kTrimBanner = QStringLiteral("... [log trimmed] ...");
}

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

WebDebugLogger* WebDebugLogger::s_instance = nullptr;
QtMessageHandler WebDebugLogger::s_previousHandler = nullptr;

WebDebugLogger* WebDebugLogger::instance()
{
    return s_instance;
}

void WebDebugLogger::install()
{
    if (!s_instance) {
        s_instance = new WebDebugLogger();
        s_previousHandler = qInstallMessageHandler(messageHandler);
    }
}

WebDebugLogger* WebDebugLogger::create(QQmlEngine*, QJSEngine*)
{
    // Calls install() rather than assuming main() already did. install() is
    // idempotent (the `if (!s_instance)` above) and needs nothing from the QML
    // engine, so there is no real case where this should ever return null — and
    // every caller in QML was written as if it could anyway (the #1661 truthy-
    // singleton trap: a registered type with no instance is TRUTHY, so
    // `WebDebugLogger &&  WebDebugLogger.foo` still reaches the member read).
    // Calling install() here removes the null case instead of asking every
    // future view to keep guessing whether it needs a guard.
    install();
    return s_instance;
}

WebDebugLogger::WebDebugLogger(QObject* parent)
    : QObject(parent)
    , m_startTime(QDateTime::currentDateTime())
{

    // Set up log file path — use external storage on Android so logs survive APK updates.
    // Falls back to internal app data if external storage is unavailable.
    QString dataDir;
#ifdef Q_OS_ANDROID
    QJniObject javaPath = QJniObject::callStaticObjectMethod(
        "io/github/kulitorum/decenza_de1/StorageHelper",
        "getLogsPath",
        "()Ljava/lang/String;");
    if (javaPath.isValid()) {
        dataDir = javaPath.toString();
    }
#endif
    if (dataDir.isEmpty()) {
        dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    QDir().mkpath(dataDir);
    m_logFilePath = dataDir + "/debug.log";

    // Write session start marker
    QFile file(m_logFilePath);
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << "\n========== SESSION START: " << m_startTime.toString(Qt::ISODate) << " ==========\n";
    } else {
        m_writeFailureWarned = true;
        qWarning() << "WebDebugLogger: cannot write session marker to" << m_logFilePath
                   << "-" << file.errorString() << "- this session's log will not persist.";
    }
}

#ifdef DECENZA_TESTING
WebDebugLogger::WebDebugLogger(const QString& testLogFilePath, QObject* parent)
    : QObject(parent)
    , m_startTime(QDateTime::currentDateTime())
{
    m_logFilePath = testLogFilePath;
}

void WebDebugLogger::installForTesting(WebDebugLogger* instance)
{
    s_instance = instance;
}
#endif

void WebDebugLogger::messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    // Forward to previous handler (console output) - wall clock comes from qSetMessagePattern
    if (s_previousHandler) {
        s_previousHandler(type, context, msg);
    }

    // Capture to our buffer (without prefix - internal use)
    if (s_instance) {
        s_instance->handleMessage(type, msg);
    }
}

void WebDebugLogger::handleMessage(QtMsgType type, const QString& message)
{
    QString category;
    switch (type) {
    case QtDebugMsg:    category = "DEBUG"; break;
    case QtInfoMsg:     category = "INFO"; break;
    case QtWarningMsg:  category = "WARN"; break;
    case QtCriticalMsg: category = "ERROR"; break;
    case QtFatalMsg:    category = "FATAL"; break;
    }

    // WALL CLOCK, not seconds-since-start.
    //
    // Both are derivable from the other given the session marker, so this is not
    // about information — it is about which direction costs the reader
    // arithmetic. Every other record in the app (shots, bags, notes) carries an
    // ISO timestamp, so the question actually asked of a log is "what happened
    // during the 09:04 shot". With an elapsed field that means finding the
    // session marker, parsing its ISO start, and adding an offset to every line
    // being considered — arithmetic an assistant reading the log does badly and
    // reports confidently, and the gap is invisible because every line already
    // LOOKS timestamped. With a wall clock the join is a string comparison.
    //
    // The reverse question ("how long after startup") is now the one needing the
    // session marker, and it is asked far less often.
    //
    // Time of day only: the date is on the session marker, and repeating it 21,000
    // times would cost more of the 2 MB cap than it informs. The single bracket is
    // deliberate — lineLevel() and stripTimestampPrefix() in mcp/mcplogfilter.h
    // both match "[^]]*" inside one bracket, so this stays parseable by everything
    // that already reads the old format, including logs already on disk.
    QString line = QString("[%1] %2 %3")
        .arg(QDateTime::currentDateTime().time().toString(QStringLiteral("HH:mm:ss.zzz")))
        .arg(category, -5)
        .arg(message);

    QMutexLocker locker(&m_mutex);
    m_lines.append(line);

    // Trim to max size (ring buffer)
    while (m_lines.size() > m_maxLines) {
        m_lines.removeFirst();
    }

    // Write to file with m_mutex released — holding it across the write would
    // block every other logging thread on disk I/O, and the emit below must not
    // run under it at all (see the re-entrancy note). writeToFile() is not
    // unsynchronised as a result: it takes m_fileMutex, which orders the file
    // operations without ordering the buffer.
    locker.unlock();
    writeToFile(line);

    // Notify observers LAST: after the mutex is released and after the line is on
    // disk, so a slot that reads either sees a consistent state.
    //
    // Releasing the lock first is not a tidiness choice. This object's own signal
    // reaches slots that can log — a view appending a line, a helper reporting what
    // it did — and every such log re-enters handleMessage() through the global
    // message handler. Emitting under m_mutex would therefore have the same thread
    // take a non-recursive QMutex it already holds: a self-deadlock, on whatever
    // arbitrary thread happened to log, with a stack that names the innocent slot.
    //
    // The re-entrancy is still a problem after the unlock, just a different one:
    // slot logs -> handleMessage -> emit -> slot logs -> unbounded recursion until
    // the stack dies. So the guard is per-THREAD (the handler is called from the
    // database and network threads as well as the main one, and a shared flag would
    // let one thread's logging silently suppress another's signal) and it suppresses
    // only the EMIT. The line itself is still buffered and still written to disk,
    // because a line produced by a logging slot is a real line and losing it would
    // be a silent hole exactly where someone was trying to explain something.
    //
    // Documented in the signal's comment as a rule too, but the rule is not the
    // mechanism: "do not log in this slot" is unenforceable, invisible when broken,
    // and the failure is a hang or a crash rather than a wrong value.
    //
    // What this does NOT cover, stated because a guard invites the assumption that
    // it does: a receiver living on a DIFFERENT thread whose slot logs. That emit is
    // queued, so it returns before the slot runs and the flag is already clear by
    // then; the slot's own log re-enters on its thread and queues another delivery,
    // which grows the receiver's event queue without bound instead of the stack.
    // Nothing in the app is such a receiver today — the views and this object are
    // both main-thread — and a thread-local flag cannot see across threads to fix
    // it. If a worker-thread observer is ever added, it needs its own answer.
    static thread_local bool emitting = false;
    if (emitting) {
        return;
    }
    // Cleared on every exit path. A slot that throws would otherwise leave this
    // thread permanently unable to emit, i.e. a view that silently stops updating
    // with nothing to indicate why.
    struct Guard {
        bool& flag;
        ~Guard() { flag = false; }
    } guard{emitting};
    emitting = true;
    emit lineAppended(type, line);
}

void WebDebugLogger::writeToFile(const QString& line)
{
    // Held across the append AND the trim below — see m_fileMutex's declaration.
    QMutexLocker fileLocker(&m_fileMutex);

    QFile file(m_logFilePath);
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << line << "\n";
        file.close();

        // Check if we need to trim
        if (file.size() > MAX_LOG_FILE_SIZE) {
            trimLogFile();
        }
    } else if (!m_writeFailureWarned) {
        // Once only: every subsequent line hits the same open() and would
        // otherwise repeat this at the very moment logging is already broken.
        // Unmarked and to stderr deliberately — it cannot go through a
        // registered-subsystem helper (this IS the persistence layer those
        // helpers write through), and it cannot reach the connections views
        // either, since they read this same file.
        m_writeFailureWarned = true;
        qWarning() << "WebDebugLogger: cannot write" << m_logFilePath
                   << "-" << file.errorString()
                   << "- all subsequent log lines will be lost from the persisted "
                      "file, the connections views, Share, and debug_get_log.";
    }
}

void WebDebugLogger::trimLogFile()
{
    // Recursive: writeToFile() already holds this when it calls us, and a direct
    // caller does not.
    QMutexLocker fileLocker(&m_fileMutex);

    QFile file(m_logFilePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // Latched, for the same reason as the write path below: writeToFile()
        // re-checks the size after EVERY line, so a trim that cannot read the
        // file is retried on every subsequent line, forever, while the file grows
        // past its 2 MB cap without bound. This was a bare `return` — the failure
        // with the largest user-visible consequence in this class said nothing at
        // all, and the symptom (a huge debug.log whose recent past is unreachable)
        // points nowhere near the cause.
        if (!m_trimFailureWarned) {
            m_trimFailureWarned = true;
            qWarning() << "WebDebugLogger: cannot open" << m_logFilePath
                       << "to read for trimming -" << file.errorString()
                       << "- the log will keep growing past its" << MAX_LOG_FILE_SIZE
                       << "byte cap. This warning is not repeated.";
        }
        return;
    }

    // Read all content
    QByteArray content = file.readAll();
    file.close();

    // Keep last ~80% of max size to avoid frequent trimming
    qint64 keepSize = MAX_LOG_FILE_SIZE * 80 / 100;
    if (content.size() <= keepSize) {
        return;
    }

    // Find a newline near the trim point to keep lines intact
    qint64 trimPoint = content.size() - keepSize;
    qint64 newlinePos = content.indexOf('\n', trimPoint);
    if (newlinePos == -1) {
        newlinePos = trimPoint;
    }

    // Write the trimmed content behind a banner, and NOTHING else.
    //
    // This used to re-emit a session marker here, "so it survives the trim". It
    // did the opposite. The only start time this function holds is m_startTime —
    // the CURRENTLY RUNNING session's — while the content it was introducing
    // belongs to whatever older session survived the cut. sessionIndex()
    // treats every SESSION START line as a boundary, so the forgery became a real
    // session in every enumeration, carrying a timestamp from a different day
    // than its own lines.
    //
    // Observed in a real log: five sessions enumerated as 07-29 18:17, 07-28
    // 10:23, 07-29 08:21, 07-29 18:17, 07-30 08:20 — not chronological, and the
    // duplicated timestamp is the run that performed the trim. Every session=N
    // address was wrong, and old lines read as having been written just now,
    // which is the exact hazard session scoping exists to prevent.
    //
    // The concern the old comment named is real: a trim CAN remove the running
    // session's own marker, if that session alone exceeds keepSize. The remedy is
    // in sessionIndex(), which reports a headless leading fragment as a
    // session with an UNKNOWN start rather than borrowing one — an absent
    // timestamp is recoverable, a wrong one is not.
    // Both writes are checked, because by the time they run the truncate has
    // ALREADY happened and the original file is unrecoverable. A short write here
    // — a full disk, a full quota on Android external storage — amputates the log
    // mid-line, and QFile::write is not [[nodiscard]], so -Werror=unused-result
    // never said a word. CLAUDE.md's note about unannotated APIs is this case
    // exactly: check the result yourself, because the compiler will not.
    if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        const QByteArray banner = (kTrimBanner + "\n").toUtf8();
        const QByteArray tail = content.mid(newlinePos + 1);
        const qint64 wroteBanner = file.write(banner);
        const qint64 wroteTail = file.write(tail);
        if (!file.flush() || wroteBanner != banner.size() || wroteTail != tail.size()) {
            qWarning() << "WebDebugLogger: log trim wrote" << wroteTail << "of"
                       << tail.size() << "bytes to" << m_logFilePath << "-"
                       << file.errorString()
                       << "- the persisted log is now truncated and the lost lines are "
                          "unrecoverable.";
        }
    } else {
        // Warn once, not per line. writeToFile() re-checks the size after EVERY
        // line, so a trim that cannot open the file is retried forever while the
        // file grows past its cap without bound — a multi-hundred-megabyte
        // debug.log and a debug_get_log that never reaches the recent past, with
        // nothing anywhere saying why. A transient share violation on Windows or a
        // remounted SD card on Android is enough to start it.
        if (!m_trimFailureWarned) {
            m_trimFailureWarned = true;
            qWarning() << "WebDebugLogger: cannot open" << m_logFilePath
                       << "to trim -" << file.errorString()
                       << "- the log will keep growing past its size cap. This warning "
                          "is not repeated.";
        }
    }
}

QString WebDebugLogger::getPersistedLog() const
{
    QFile file(m_logFilePath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString::fromUtf8(file.readAll());
    }
    return QString();
}

QStringList WebDebugLogger::getPersistedLogChunk(qsizetype offset, qsizetype limit, qsizetype* totalLines) const
{
    QFile file(m_logFilePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // Distinct from "log is genuinely empty" — surfaced so a caller isn't left
        // reading an empty result as though nothing has ever been logged. QFileInfo's
        // stat-based size()/mtime() (used by sessionIndex()'s cache key) can succeed
        // even when the file can't be opened, so this warning is the only signal that
        // the persisted log is currently unreadable.
        qWarning() << "WebDebugLogger: failed to open persisted log for reading:" << m_logFilePath
                   << file.errorString();
        if (totalLines) *totalLines = 0;
        return {};
    }

    QTextStream stream(&file);
    QStringList result;
    qsizetype lineNum = 0;

    while (!stream.atEnd()) {
        QString line = stream.readLine();
        if (lineNum >= offset && result.size() < limit) {
            result.append(line);
        }
        lineNum++;
        // Stop once the page is full — but only when nobody asked for the total,
        // because that is a count of the WHOLE file and needs every line. Without
        // this the loop read to EOF even for a satisfied 400-line request, so the
        // connections views paid a full 2 MB scan on top of sessionIndex()'s.
        if (!totalLines && result.size() >= limit)
            break;
    }

    if (totalLines) *totalLines = lineNum;
    return result;
}

QList<WebDebugLogger::SessionBoundary> WebDebugLogger::sessionIndex(qsizetype* totalLines) const
{
    QMutexLocker locker(&m_sessionIndexMutex);

    const QFileInfo info(m_logFilePath);
    const qint64 size = info.size();
    const QDateTime mtime = info.lastModified();

    if (m_cachedFileSize != size || m_cachedFileMTime != mtime) {
#ifdef DECENZA_TESTING
        ++s_testSessionIndexRebuildCount;
#endif
        qsizetype scannedTotal = 0;
        // Same 100,000-line cap the tool's filtered/tailed reads use — a
        // generous superset of what MAX_LOG_FILE_SIZE (2MB) can ever hold.
        const QStringList allLines = getPersistedLogChunk(0, 100000, &scannedTotal);

        QList<SessionBoundary> sessions;

        // A trim cuts from the FRONT, so it can leave lines whose own SESSION
        // START marker is gone. Those lines are a session — they are just one
        // whose start time was destroyed, and the file no longer holds the
        // information needed to recover it.
        //
        // Report them as a boundary at line 0 with an EMPTY timestamp. Two
        // properties follow, and both matter:
        //
        //   - The fragment is addressable. Without a boundary it belonged to no
        //     session at all, so its lines were unreachable through session=N and
        //     invisible in the line accounting.
        //   - Its start time is absent rather than borrowed. trimLogFile() used to
        //     supply one by writing a marker stamped with the running session's
        //     start; that is why this branch exists rather than that one. An
        //     absent timestamp tells a reader it is unknown. A borrowed one tells
        //     them yesterday's disconnect happened this morning.
        //
        // Only ever the FIRST boundary: any later session was written whole and
        // carries its own marker.
        //
        // "Content before the first marker" is NOT the same as "line 0 is not a
        // marker", and it is not "line 0 is not blank" either. TWO lines can
        // legitimately precede the first marker without any orphaned content
        // existing, and counting either one invents a session:
        //
        //   - A BLANK line, on a healthy fresh log. The marker is written with a
        //     leading newline (see the constructor), so line 0 is empty and the
        //     marker sits on line 1.
        //   - The TRIM BANNER, on any trimmed log. trimLogFile() writes it
        //     unconditionally at the head of what survives.
        //
        // The banner case is not hypothetical and was shipped: on a real 21,553
        // line log the trim landed just before a marker, so the surviving head was
        // banner-then-marker with no orphaned lines at all, and this scan reported
        // a session whose entire content was the banner. It read as a genuine
        // one-line session in every enumeration.
        //
        // Skip both, and require something that is neither before concluding a
        // fragment is real.
        qsizetype firstMarkerLine = -1;
        for (qsizetype i = 0; i < allLines.size(); ++i) {
            if (allLines[i].contains(kSessionMarker)) {
                firstMarkerLine = i;
                break;
            }
        }
        const qsizetype fragmentEnd =
            (firstMarkerLine >= 0) ? firstMarkerLine : allLines.size();
        bool hasHeadlessFragment = false;
        for (qsizetype i = 0; i < fragmentEnd; ++i) {
            const QString trimmed = allLines[i].trimmed();
            if (!trimmed.isEmpty() && trimmed != kTrimBanner) {
                hasHeadlessFragment = true;
                break;
            }
        }
        if (hasHeadlessFragment)
            sessions.append({0, QString(), 0});

        for (qsizetype i = 0; i < allLines.size(); ++i) {
            if (allLines[i].contains(kSessionMarker)) {
                QString ts;
                const qsizetype tsStart = allLines[i].indexOf(kSessionMarker) + kSessionMarker.size();
                const qsizetype tsEnd = allLines[i].indexOf(QStringLiteral("=========="), tsStart);
                if (tsEnd > tsStart)
                    ts = allLines[i].mid(tsStart, tsEnd - tsStart).trimmed();
                sessions.append({i, ts, 0});
            }
        }
        for (qsizetype i = 0; i < sessions.size(); ++i) {
            const qsizetype nextStart = (i + 1 < sessions.size()) ? sessions[i + 1].startLine : scannedTotal;
            sessions[i].lineCount = nextStart - sessions[i].startLine;
        }

        m_cachedSessionIndex = sessions;
        m_cachedTotalLines = scannedTotal;
        m_cachedFileSize = size;
        m_cachedFileMTime = mtime;
    }

    if (totalLines) *totalLines = m_cachedTotalLines;
    return m_cachedSessionIndex;
}

QString WebDebugLogger::logFilePath() const
{
    return m_logFilePath;
}

QStringList WebDebugLogger::sessionLinesMatching(const QStringList& markers,
                                                 const QString& minLevel) const
{
    // No subsystem asked for means no lines, not every line. A view built with an
    // empty marker list is a wiring mistake, and answering it with the unfiltered
    // log would put the whole firehose on screen — the exact failure this change
    // exists to end — while looking like it worked.
    if (markers.isEmpty()) {
        qWarning() << "WebDebugLogger: sessionLinesMatching() called with an empty "
                      "marker list — a wiring mistake in whichever view called this. "
                      "Returning no lines rather than the unfiltered log.";
        return {};
    }

    qsizetype totalLines = 0;
    const QList<SessionBoundary> sessions = sessionIndex(&totalLines);

    // The current session is the LAST boundary: install() writes one marker at
    // startup and nothing writes another, so the newest is always ours.
    //
    // With no boundary at all, read the whole file. That is not a hypothetical
    // branch to be defensive: the DECENZA_TESTING constructor deliberately writes
    // no session marker, and a debug.log carried over from a build predating the
    // markers has none either. Returning nothing in those cases would look exactly
    // like "this subsystem logged nothing", which is the one answer a log reader
    // must never be given falsely.
    const qsizetype start = sessions.isEmpty() ? 0 : sessions.last().startLine;
    const qsizetype count = sessions.isEmpty() ? totalLines : sessions.last().lineCount;
    if (count <= 0) {
        return {};
    }

    QStringList result;
    for (const QString& line : getPersistedLogChunk(start, count)) {
        if (lineMatches(line, markers, minLevel)) {
            result.append(line);
        }
    }
    return result;
}

bool WebDebugLogger::lineMatches(const QString& line, const QStringList& markers,
                                const QString& minLevel) const
{
    // The single per-line test, called both by sessionLinesMatching() above for the
    // backfill and directly by the views for each live line. One function, so the
    // set a view is built with and the set it grows by cannot disagree.
    if (!McpLogFilter::matchesAnyMarker(line, markers)) {
        return false;
    }
    if (minLevel.isEmpty()) {
        return true;
    }
    // An unrecognised minLevel ranks -1, which as a threshold would admit
    // everything — including the session markers and trim banners that carry no
    // level at all, because those also rank -1. Treated as "no level constraint"
    // rather than silently putting the firehose back on screen.
    const int minRank = McpLogFilter::levelRank(minLevel);
    if (minRank < 0) {
        // Latched, not per-call: this runs on every live line, and the point is a
        // developer typo ("WARNING", "TRACE") reverting a view to the unfiltered
        // firehose with no sign why — one warning is enough to find it.
        static bool warned = false;
        if (!warned) {
            warned = true;
            qWarning() << "WebDebugLogger: unrecognised minLevel" << minLevel
                       << "- treating as no level constraint (every line, including "
                          "DEBUG, will match). Valid values: DEBUG, INFO, WARN, "
                          "ERROR, FATAL.";
        }
        return true;
    }
    return McpLogFilter::levelRank(McpLogFilter::lineLevel(line)) >= minRank;
}

QStringList WebDebugLogger::getLines(int afterIndex, int* lastIndex) const
{
    QMutexLocker locker(&m_mutex);

    if (lastIndex) {
        *lastIndex = static_cast<int>(m_lines.size());
    }

    if (afterIndex >= m_lines.size()) {
        return QStringList();
    }

    if (afterIndex <= 0) {
        return m_lines;
    }

    return m_lines.mid(afterIndex);
}

QStringList WebDebugLogger::getAllLines() const
{
    QMutexLocker locker(&m_mutex);
    return m_lines;
}

void WebDebugLogger::clear(bool clearFile)
{
    QMutexLocker locker(&m_mutex);
    m_lines.clear();

    if (clearFile && !m_logFilePath.isEmpty()) {
        locker.unlock();
        // Truncates the same file writeToFile() appends to, so it takes the same
        // lock — otherwise a concurrent append lands in a file being emptied.
        QMutexLocker fileLocker(&m_fileMutex);
        QFile file(m_logFilePath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            QTextStream stream(&file);
            stream << "========== LOG CLEARED: " << QDateTime::currentDateTime().toString(Qt::ISODate) << " ==========\n";
            // A session marker IS legitimate here, and the asymmetry with
            // trimLogFile() is the whole point rather than an inconsistency.
            //
            // A trim must not write one because the content it precedes belongs
            // to an OLDER session, so m_startTime would be a borrowed timestamp
            // — the forgery this class was fixed to stop. After a clear, every
            // line that follows belongs to the RUNNING session, so m_startTime is
            // that content's true start and writing it states a fact.
            //
            // Without it the clear banner alone reads as a headless fragment, and
            // the running session gets reported with an unknown start time and a
            // note blaming a trim that never happened. The user pressed Clear.
            stream << "\n========== SESSION START: " << m_startTime.toString(Qt::ISODate)
                   << " ==========\n";
        } else {
            // Never silent: the in-memory buffer is now empty, so the user has
            // been shown a cleared log while the file still holds every line.
            // Believing a log is gone when it is not is the wrong direction to be
            // wrong in.
            qWarning() << "WebDebugLogger: could not clear" << m_logFilePath << "-"
                       << file.errorString()
                       << "- the in-memory buffer was cleared but the file still holds "
                          "the old log.";
        }
    }
}

int WebDebugLogger::lineCount() const
{
    QMutexLocker locker(&m_mutex);
    return static_cast<int>(m_lines.size());
}
