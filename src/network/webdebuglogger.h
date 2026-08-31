#pragma once

#include <type_traits>
#include <QObject>
#include <QMutex>
#include <QStringList>
#include <QElapsedTimer>
#include <QDateTime>
#include <QFile>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

/**
 * Captures Qt debug output for streaming to web interface.
 * Maintains a ring buffer of recent log messages in memory,
 * and persists to a file for crash recovery.
 */
class WebDebugLogger : public QObject {
    Q_OBJECT
    // Exposed to QML by macro, in the header — never setContextProperty and never a
    // runtime qmlRegisterType. Both are invisible to qmllint, qmlcachegen and the
    // language server, which makes a typo indistinguishable from a real property
    // (the #1661 defect class). See docs/CLAUDE_MD/QML_GOTCHAS.md.
    QML_ELEMENT
    QML_SINGLETON

public:
    static WebDebugLogger* instance();
    static void install();

    // QML singleton provider. Returns the existing instance rather than building
    // one: this object owns the global Qt message handler, and a second would
    // either fight over it or silently capture nothing.
    //
    // Returns nullptr when install() has not run. Note what QML does with that — a
    // registered singleton with no instance is TRUTHY, not undefined
    // (qtdeclarative/src/qml/jsruntime/qv4qmlcontext.cpp:229), so
    // `if (WebDebugLogger)` passes and the first member read is what fails. A QML
    // caller must guard the MEMBER (`WebDebugLogger.sessionLinesMatching !==
    // undefined`), not the name.
    static WebDebugLogger* create(QQmlEngine* = nullptr, QJSEngine* = nullptr);

    // Public because the testing constructor above lets a test own one by value.
    // Clears s_instance so the installed message handler cannot dereference a
    // destroyed logger — see the comment on create().
    ~WebDebugLogger() override;

    // Get recent log lines (for polling)
    QStringList getLines(int afterIndex, int* lastIndex = nullptr) const;

    // Get all lines in buffer
    QStringList getAllLines() const;

    // Get persisted log from file (survives crashes)
    QString getPersistedLog() const;

    // Get a chunk of the persisted log by line offset/limit.
    // Returns the lines and sets totalLines to the total number of lines in the file.
    QStringList getPersistedLogChunk(qsizetype offset, qsizetype limit, qsizetype* totalLines = nullptr) const;

    // One "========== SESSION START: ... ==========" marker in the persisted log.
    struct SessionBoundary {
        qsizetype startLine;
        QString timestamp;
        qsizetype lineCount;
    };

    // Session-boundary index for the persisted log, keyed on the log file's
    // size + mtime: rebuilt only when either changed since the last call,
    // instead of rescanning the whole file on every call (used by the
    // debug_get_log MCP tool's sessions/session modes). Sets totalLines to
    // the file's total line count when non-null.
    QList<SessionBoundary> sessionIndex(qsizetype* totalLines = nullptr) const;

    // Clear the buffer (and optionally the file)
    void clear(bool clearFile = false);

    // Get current line count (for polling comparison)
    int lineCount() const;

    // Get log file path
    QString logFilePath() const;

    // The current session's lines matching ANY of `markers` at or above
    // `minLevel` — the query the connections-page views are built from, so a
    // subsystem's on-screen narrative and `debug_get_log`'s answer for the same
    // marker and level are the same set by construction rather than by two
    // implementations agreeing.
    //
    // `markers` are BRACKETED and matched as SUBSTRINGS, not regexes: pass
    // "[Scale]", and note that treating it as a pattern would make it a character
    // class matching almost every line. DecenzaLog::markerFilter() (core/logtags.h)
    // composes them so no caller writes the brackets itself.
    //
    // Reads the PERSISTED log, not the in-memory ring buffer, and the difference
    // matters: the ring buffer is capped at m_maxLines across all levels, so after
    // a busy stretch — a shot produces DEBUG lines far faster than INFO ones — the
    // whole session's scale narrative would have been evicted by chatter it is
    // supposed to be readable apart from. The buffer is the right size for the web
    // poller and the wrong source for "what happened this session".
    //
    // Cost, stated honestly because it runs on the GUI thread: a cold call reads
    // the whole file TWICE. sessionIndex() rebuilds by reading every line whenever
    // the file's size or mtime changed, materialising up to 100,000 QStrings, and
    // getPersistedLogChunk() then streams from line 0 again regardless of `offset`
    // (webdebuglogger.cpp:254-260 — the loop visits every line and only appends
    // those in range). A warm call skips the first read. The file is bounded by
    // MAX_LOG_FILE_SIZE (2 MB), which is what keeps this tolerable.
    //
    // So: a one-shot at page open, with live lines arriving via lineAppended()
    // afterwards. Never per line, and never in a loop. CLAUDE.md forbids disk I/O
    // on the main thread and grants no exemption for this; it is here because a
    // bounded 2 MB read at page open is a hitch rather than a hang. If it ever
    // needs to run anywhere hotter, move it to a worker first.
    //
    // (An earlier version of this comment said "one pass over the current
    // session's slice", which understated the work by two full passes. Left on the
    // record because an understated cost is what licenses the call that hangs.)
    Q_INVOKABLE QStringList sessionLinesMatching(const QStringList& markers,
                                                 const QString& minLevel) const;

    // Whether one line belongs to any of `markers` at or above `minLevel` — the
    // same test sessionLinesMatching() applies to each line, exposed so the live
    // path can reuse it instead of reimplementing it.
    //
    // This exists because the alternative is a marker/level check written in
    // JavaScript in each of the two connections-page views. Those would be a third
    // and fourth definition of "a scale line at INFO or above", in a language with
    // no access to McpLogFilter::levelRank, drifting from the accessor that
    // populated the very same view a moment earlier — so a view could show a line
    // on arrival that a reload would then drop, or the reverse.
    Q_INVOKABLE bool lineMatches(const QString& line, const QStringList& markers,
                                 const QString& minLevel) const;

signals:
    // One emission per captured line, after it is in the buffer and on disk.
    //
    // Emitted from whatever thread called qDebug/qInfo/qWarning — the message
    // handler is global and the database and network threads log too — so a
    // receiver on another thread gets it queued and must not assume otherwise.
    //
    // QtMsgType survives that queued hop without a Q_DECLARE_METATYPE, which is
    // worth stating because the enum has none: it is a plain enum at
    // qtbase/src/corelib/global/qlogging.h:30 and nothing in corelib declares a
    // metatype for it. Qt 6 does not need one — moc's SignalData::metaTypes()
    // emits QMetaType::fromType<> for every argument
    // (qtbase/src/corelib/kernel/qtmochelpers.h:381-395), so the type resolves from
    // the generated metatypes array. The 0x80000000 flag on the parameter's entry in
    // the string table is IsUnresolvedType (qtmocconstants.h:89) and refers only to
    // introspection by name, not to argument marshalling.
    //
    // QML handlers do not need the type parameter at all: the line text carries its
    // own level tag and lineMatches() reads it from there. Ignore it rather than
    // teaching QML about the enum.
    //
    // A slot connected to this MUST NOT log. Doing so re-enters the global message
    // handler from inside the emit; see the recursion guard in handleMessage() for
    // what happens then and why the guard exists rather than the rule being left
    // to documentation.
    void lineAppended(QtMsgType type, const QString& line);

public:

#ifdef DECENZA_TESTING
    // Test-only: construct pointed at an explicit file instead of the real
    // singleton's AppDataLocation path, so a test can seed exact file
    // content/size/mtime without touching s_instance or the developer's
    // real debug.log. Writes no session-start marker — the test controls
    // the file's content directly.
    explicit WebDebugLogger(const QString& testLogFilePath, QObject* parent = nullptr);

    // Test-only: point the singleton instance() resolves at a test-owned
    // object (e.g. one built with the constructor above), so a real
    // registerDebugTools() handler under test resolves WebDebugLogger::instance()
    // to test-controlled content. Does not take ownership; pass nullptr to
    // restore no singleton.
    static void installForTesting(WebDebugLogger* instance);

    // Test-only: counts sessionIndex() cache *misses* (rebuilds), so a test
    // can assert a repeated call with no file change reused the cache
    // instead of rescanning.
    static int testSessionIndexRebuildCount() { return s_testSessionIndexRebuildCount; }
    static void resetTestSessionIndexRebuildCount() { s_testSessionIndexRebuildCount = 0; }

    // Reaches handleMessage(), so a test can drive capture directly instead of
    // installing the global handler — which would capture the whole process's
    // logging, QtTest's own included, and make the assertions depend on it.
    friend class tst_WebDebugLogger;
#endif

private:
    explicit WebDebugLogger(QObject* parent = nullptr);

    void handleMessage(QtMsgType type, const QString& message);
    void writeToFile(const QString& line);
    void trimLogFile();

    static void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg);
    static QtMessageHandler s_previousHandler;
    static WebDebugLogger* s_instance;

    mutable QMutex m_mutex;

    // Serialises everything that touches the FILE — the append in writeToFile(),
    // the truncate-and-rewrite in trimLogFile(), and the truncate in clear().
    //
    // Not m_mutex, which guards the in-memory ring buffer and is deliberately
    // RELEASED before writeToFile() (see handleMessage(): holding it across the
    // emit self-deadlocks when a slot logs). That release is what leaves the file
    // unprotected: handleMessage() runs on the database and network threads as
    // well as the main one, so two threads can enter trimLogFile() together, both
    // read the whole file, both truncate, and both write their own tail — second
    // writer wins and the first one's lines are gone. A third thread appending
    // during the truncate window writes into a file being rewritten from offset 0.
    //
    // That damage is indistinguishable from the forged-session-marker bug this
    // class was just fixed for: lines from one moment appearing where they do not
    // belong. Different cause, same wrong answer to the reader.
    //
    // RECURSIVE because writeToFile() calls trimLogFile() while holding it, and
    // trimLogFile() is also called directly (tests, and any future caller) where
    // it must lock for itself.
    mutable QRecursiveMutex m_fileMutex;
    QStringList m_lines;
    int m_maxLines = 500;   // Ring buffer size
    QElapsedTimer m_timer;
    QDateTime m_startTime;

    // File persistence
    QString m_logFilePath;
    static constexpr qint64 MAX_LOG_FILE_SIZE = 2 * 1024 * 1024;  // 2MB - several days of sessions
    // Set the first time writeToFile() fails to open the file. Both connections
    // views now read ONLY the persisted file — the in-memory buffers they used to
    // fall back on are gone — so a silently unwritable log used to just mean
    // "no user-visible symptom"; now it means both views permanently empty and
    // debug_get_log/Share reporting nothing, none of it explained anywhere. This
    // latches so the failure is reported once rather than once per line.
    bool m_writeFailureWarned = false;
    // Same latch for the TRIM path, and separate from the one above on purpose:
    // the two failures are different diagnoses. An unwritable file loses new
    // lines; a file that cannot be opened to trim keeps every line and grows past
    // the 2 MB cap without bound, because writeToFile() re-checks the size after
    // every line and so retries the trim forever. The user's symptom there is a
    // huge debug.log whose recent past is unreachable — the opposite shape, and it
    // was previously a bare `return` that said nothing at all.
    bool m_trimFailureWarned = false;

    // Session-index cache (see sessionIndex()). Separate mutex from m_mutex,
    // which guards the in-memory ring buffer, not the persisted file.
    mutable QMutex m_sessionIndexMutex;
    mutable QList<SessionBoundary> m_cachedSessionIndex;
    mutable qsizetype m_cachedTotalLines = 0;
    mutable qint64 m_cachedFileSize = -1;
    mutable QDateTime m_cachedFileMTime;

#ifdef DECENZA_TESTING
    static inline int s_testSessionIndexRebuildCount = 0;
#endif
};

// Qt tests is_default_constructible BEFORE HasSingletonFactory when it picks a QML_SINGLETON's
// construction mode (qtdeclarative/src/qml/qml/qqmlprivate.h:161-164). A default-constructible
// singleton therefore gets `new T` (:190) and its create() is never called — dead code that
// still compiles, with no diagnostic from the compiler, moc, qmllint or the suite. Decenza
// shipped exactly that for AccessibilityManager; see docs/CLAUDE_MD/QML_GOTCHAS.md.
//
// WebDebugLogger is safe today only because its constructor requires arguments. That is incidental, so
// assert it: adding a default to every parameter would silently detach QML from the published
// instance again.
static_assert(!std::is_default_constructible_v<WebDebugLogger>,
              "WebDebugLogger is a QML_SINGLETON with a create() factory: it must NOT be "
              "default-constructible, or Qt will 'new' its own instance and never call create().");
