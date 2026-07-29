#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>

#include "network/webdebuglogger.h"

// Exercises WebDebugLogger::sessionIndex()'s cache: reused across repeated
// calls when the persisted file hasn't changed, rebuilt when it has
// (including trimLogFile()'s truncate-and-rewrite path, which changes both
// size and content — see tasks.md 1.3).
class tst_WebDebugLogger : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;

    QString logPath() const { return m_dir.filePath("debug.log"); }

    static void writeFile(const QString& path, const QString& content)
    {
        QFile f(path);
        QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text), "failed to write test log file");
        QTextStream(&f) << content;
    }

private slots:
    void init() { QTest::failOnWarning(); }

    void sessionIndex_findsBoundariesAndCounts()
    {
        writeFile(logPath(),
            "========== SESSION START: 2026-01-01T09:00:00 ==========\n"
            "[   0.100] INFO  first session line one\n"
            "[   0.200] WARN  first session line two\n"
            "========== SESSION START: 2026-01-01T10:00:00 ==========\n"
            "[   0.100] DEBUG second session line one\n");

        WebDebugLogger logger(logPath());
        qsizetype totalLines = 0;
        const auto sessions = logger.sessionIndex(&totalLines);

        QCOMPARE(sessions.size(), 2);
        QCOMPARE(sessions[0].startLine, qsizetype(0));
        QCOMPARE(sessions[0].timestamp, QStringLiteral("2026-01-01T09:00:00"));
        QCOMPARE(sessions[0].lineCount, qsizetype(3));
        QCOMPARE(sessions[1].startLine, qsizetype(3));
        QCOMPARE(sessions[1].timestamp, QStringLiteral("2026-01-01T10:00:00"));
        QCOMPARE(sessions[1].lineCount, qsizetype(2));
        QCOMPARE(totalLines, qsizetype(5));
    }

    void sessionIndex_repeatedCallsReuseCache()
    {
        writeFile(logPath(),
            "========== SESSION START: 2026-01-01T09:00:00 ==========\n"
            "[   0.100] INFO  line\n");

        WebDebugLogger logger(logPath());
        WebDebugLogger::resetTestSessionIndexRebuildCount();

        const auto first = logger.sessionIndex();
        QCOMPARE(WebDebugLogger::testSessionIndexRebuildCount(), 1);

        const auto second = logger.sessionIndex();
        const auto third = logger.sessionIndex();
        // No file change between calls — cache reused, no further rebuilds.
        QCOMPARE(WebDebugLogger::testSessionIndexRebuildCount(), 1);
        QCOMPARE(second.size(), first.size());
        QCOMPARE(third[0].startLine, first[0].startLine);
        QCOMPARE(third[0].timestamp, first[0].timestamp);
    }

    void sessionIndex_rebuildsAfterFileGrows()
    {
        writeFile(logPath(),
            "========== SESSION START: 2026-01-01T09:00:00 ==========\n"
            "[   0.100] INFO  line\n");

        WebDebugLogger logger(logPath());
        WebDebugLogger::resetTestSessionIndexRebuildCount();

        const auto before = logger.sessionIndex();
        QCOMPARE(before.size(), 1);

        // Append a new session — changes both size and mtime.
        QFile f(logPath());
        QVERIFY(f.open(QIODevice::Append | QIODevice::Text));
        QTextStream(&f) << "========== SESSION START: 2026-01-01T11:00:00 ==========\n"
                           "[   0.100] ERROR second session line\n";
        f.close();

        const auto after = logger.sessionIndex();
        QCOMPARE(WebDebugLogger::testSessionIndexRebuildCount(), 2);
        QCOMPARE(after.size(), 2);
        QCOMPARE(after[1].timestamp, QStringLiteral("2026-01-01T11:00:00"));
    }

    void sessionIndex_rebuildsAfterTruncateAndRewrite()
    {
        // Simulates trimLogFile(): the file is truncated and rewritten with a
        // re-emitted session marker so it survives the trim (webdebuglogger.cpp).
        writeFile(logPath(),
            "========== SESSION START: 2026-01-01T09:00:00 ==========\n"
            "[   0.100] INFO  line one\n"
            "[   0.200] INFO  line two\n"
            "[   0.300] INFO  line three\n");

        WebDebugLogger logger(logPath());
        WebDebugLogger::resetTestSessionIndexRebuildCount();
        const auto before = logger.sessionIndex();
        QCOMPARE(before.size(), 1);
        QCOMPARE(before[0].lineCount, qsizetype(4));

        // Truncate-and-rewrite: shorter content, same-ish size class, but a
        // genuinely different file (content and size both change).
        writeFile(logPath(),
            "... [log trimmed] ...\n"
            "========== SESSION START: 2026-01-01T09:00:00 ==========\n"
            "[   0.300] INFO  line three\n");

        const auto after = logger.sessionIndex();
        QCOMPARE(WebDebugLogger::testSessionIndexRebuildCount(), 2);
        QCOMPARE(after.size(), 1);
        QCOMPARE(after[0].startLine, qsizetype(1));
        QCOMPARE(after[0].lineCount, qsizetype(2));
    }

    // ---- sessionLinesMatching(): the query the connections-page views run ----
    //
    // Each of these pins a way the view could show the wrong set while looking
    // like it worked, which is the failure mode that matters: a view is judged by
    // its contents, and a reader has no second source to check them against.

    // The whole point of the marker: two subsystems in one log, separable. The
    // scale view asks for [Scale] and [Refractometer] together (they share the
    // screen), the DE1 view asks for [DE1] alone, and neither may leak into the
    // other.
    void sessionLines_selectsOnlyRequestedMarkers()
    {
        writeFile(logPath(),
            "========== SESSION START: 2026-01-01T09:00:00 ==========\n"
            "[   0.100] INFO  [Scale][BLE AcaiaScale] Reporting connected\n"
            "[   0.200] INFO  [DE1][Device] DE1 CONNECTED (BLE)\n"
            "[   0.300] INFO  [Refractometer][BLE DiFluidR2] Connected and ready\n"
            "[   0.400] INFO  no marker at all\n");

        WebDebugLogger logger(logPath());

        const auto de1 = logger.sessionLinesMatching({QStringLiteral("[DE1]")},
                                                     QStringLiteral("INFO"));
        QCOMPARE(de1.size(), 1);
        QVERIFY(de1[0].contains(QStringLiteral("DE1 CONNECTED")));

        const auto scale = logger.sessionLinesMatching(
            {QStringLiteral("[Scale]"), QStringLiteral("[Refractometer]")},
            QStringLiteral("INFO"));
        QCOMPARE(scale.size(), 2);
        QVERIFY(scale[0].contains(QStringLiteral("AcaiaScale")));
        QVERIFY(scale[1].contains(QStringLiteral("DiFluidR2")));
    }

    // DEBUG is developer detail and the views show INFO+. If the threshold leaks,
    // the firehose this whole change removed comes straight back — and it comes
    // back looking like a working view, just a busy one.
    void sessionLines_appliesMinimumLevel()
    {
        writeFile(logPath(),
            "========== SESSION START: 2026-01-01T09:00:00 ==========\n"
            "[   0.100] DEBUG [Scale][BLE AcaiaScale] weight frame 18.2g\n"
            "[   0.200] INFO  [Scale][BLE AcaiaScale] Reporting connected\n"
            "[   0.300] WARN  [Scale][BLEManager] connection timeout\n");

        WebDebugLogger logger(logPath());

        const auto infoUp = logger.sessionLinesMatching({QStringLiteral("[Scale]")},
                                                        QStringLiteral("INFO"));
        QCOMPARE(infoUp.size(), 2);
        QVERIFY(!infoUp.join(QChar('\n')).contains(QStringLiteral("weight frame")));

        // No threshold means every level, DEBUG included.
        QCOMPARE(logger.sessionLinesMatching({QStringLiteral("[Scale]")}, QString()).size(), 3);
    }

    // Only THIS session. A view that silently included the previous run would
    // show a scale connecting and disconnecting that the user never touched, and
    // there is nothing on screen to reveal the line is hours old.
    void sessionLines_excludesEarlierSessions()
    {
        writeFile(logPath(),
            "========== SESSION START: 2026-01-01T09:00:00 ==========\n"
            "[   0.100] INFO  [Scale][BLEManager] previous session line\n"
            "========== SESSION START: 2026-01-01T10:00:00 ==========\n"
            "[   0.100] INFO  [Scale][BLEManager] current session line\n");

        WebDebugLogger logger(logPath());
        const auto lines = logger.sessionLinesMatching({QStringLiteral("[Scale]")},
                                                        QStringLiteral("INFO"));
        QCOMPARE(lines.size(), 1);
        QVERIFY(lines[0].contains(QStringLiteral("current session")));
    }

    // A log with no session marker reads as one session, not as nothing. Both the
    // DECENZA_TESTING constructor and a debug.log carried over from a build
    // predating the markers produce this, and answering "no lines" would be
    // indistinguishable from "this subsystem never logged".
    void sessionLines_withNoSessionMarkerReadsWholeFile()
    {
        writeFile(logPath(),
            "[   0.100] INFO  [Scale][BLEManager] line one\n"
            "[   0.200] INFO  [Scale][BLEManager] line two\n");

        WebDebugLogger logger(logPath());
        QCOMPARE(logger.sessionLinesMatching({QStringLiteral("[Scale]")},
                                              QStringLiteral("INFO")).size(), 2);
    }

    // No marker requested returns nothing, never everything. A view wired up with
    // an empty list is a bug, and the failure must not be "shows the entire log"
    // — that reads as a working view rather than a broken query.
    void sessionLines_emptyMarkerListMatchesNothing()
    {
        writeFile(logPath(),
            "========== SESSION START: 2026-01-01T09:00:00 ==========\n"
            "[   0.100] INFO  [Scale][BLEManager] a line\n");

        WebDebugLogger logger(logPath());
        QVERIFY(logger.sessionLinesMatching({}, QStringLiteral("INFO")).isEmpty());
    }

    // Markers are case-SENSITIVE substrings. "[scale]" is not a marker, and
    // matching it would only ever be a false positive on prose.
    void lineMatches_markersAreCaseSensitive()
    {
        WebDebugLogger logger(logPath());
        const QString line = QStringLiteral("[   0.100] INFO  [Scale][BLEManager] a line");
        QVERIFY(logger.lineMatches(line, {QStringLiteral("[Scale]")}, QStringLiteral("INFO")));
        QVERIFY(!logger.lineMatches(line, {QStringLiteral("[scale]")}, QStringLiteral("INFO")));
    }

    // The backfill and the live path must agree line-for-line: the view is built
    // from sessionLinesMatching() and then grows via lineMatches(). If they can
    // disagree, a line shown on arrival vanishes on reload, or the reverse — and
    // both look like the log itself is unreliable.
    void lineMatches_agreesWithTheAccessor()
    {
        const QStringList markers{QStringLiteral("[Scale]"), QStringLiteral("[Refractometer]")};
        const QString minLevel = QStringLiteral("INFO");

        // A PRIOR session first, so this also proves the two agree about which
        // session they are looking at and not merely about which lines match. Both
        // must resolve to the last boundary; comparing one against the other across
        // different sessions would pass or fail for the wrong reason.
        writeFile(logPath(),
            "========== SESSION START: 2026-01-01T09:00:00 ==========\n"
            "[   0.100] INFO  [Scale][BLEManager] previous session, must not appear\n"
            "========== SESSION START: 2026-01-01T10:00:00 ==========\n"
            "[   0.100] DEBUG [Scale][BLE AcaiaScale] weight frame\n"
            "[   0.200] INFO  [Scale][BLE AcaiaScale] Reporting connected\n"
            "[   0.300] INFO  [DE1][Device] DE1 CONNECTED (BLE)\n"
            "[   0.400] WARN  [Refractometer][BLE DiFluidR2] Transport error: x\n"
            "[   0.500] INFO  unmarked line\n");

        WebDebugLogger logger(logPath());
        // Read the current session's raw lines back the way the accessor does, then
        // check the per-line predicate reproduces its selection exactly.
        const auto sessions = logger.sessionIndex();
        QCOMPARE(sessions.size(), 2);
        const QStringList raw = logger.getPersistedLogChunk(sessions.last().startLine,
                                                            sessions.last().lineCount);
        QStringList viaPredicate;
        for (const QString& line : raw) {
            if (logger.lineMatches(line, markers, minLevel)) viaPredicate.append(line);
        }
        QCOMPARE(viaPredicate, logger.sessionLinesMatching(markers, minLevel));
        // The scale INFO line and the refractometer WARN — not the DEBUG frame, not
        // the [DE1] line, not the unmarked one, and not the previous session's.
        QCOMPARE(viaPredicate.size(), 2);
        QVERIFY(!viaPredicate.join(QChar('\n')).contains(QStringLiteral("previous session")));
    }

    // ---- lineAppended() ----

    // One emission per captured line, carrying the right type. The type is a plain
    // enum with no Q_DECLARE_METATYPE (qlogging.h:30); this also proves it survives
    // signal marshalling, which is the part that would fail silently at runtime
    // rather than at compile time.
    void lineAppended_firesOncePerLineWithTheRightType()
    {
        WebDebugLogger logger(logPath());
        QSignalSpy spy(&logger, &WebDebugLogger::lineAppended);
        QVERIFY(spy.isValid());

        // handleMessage() is what the global message handler calls. Driven directly
        // so the test does not have to install a handler and capture the whole
        // process's logging — including QtTest's own.
        logger.handleMessage(QtInfoMsg, QStringLiteral("[Scale][BLEManager] hello"));
        logger.handleMessage(QtWarningMsg, QStringLiteral("[DE1][Device] uh oh"));

        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy[0][0].value<QtMsgType>(), QtInfoMsg);
        QVERIFY(spy[0][1].toString().contains(QStringLiteral("INFO")));
        QVERIFY(spy[0][1].toString().contains(QStringLiteral("[Scale][BLEManager] hello")));
        QCOMPARE(spy[1][0].value<QtMsgType>(), QtWarningMsg);
        QVERIFY(spy[1][1].toString().contains(QStringLiteral("WARN")));
    }

    // A slot that logs must not recurse or deadlock. This is the whole reason the
    // guard exists: emitting under the mutex would self-deadlock, and emitting
    // without a guard would recurse until the stack died. Both failures are a hang
    // or a crash on an arbitrary thread with the innocent slot on top of the stack,
    // so neither would be diagnosed from the symptom.
    //
    // The line the slot logs must still be RECORDED — only its signal is
    // suppressed. Losing it would be a silent hole exactly where someone was
    // trying to explain something.
    void lineAppended_slotThatLogsDoesNotRecurse()
    {
        WebDebugLogger logger(logPath());

        int calls = 0;
        connect(&logger, &WebDebugLogger::lineAppended, &logger,
                [&](QtMsgType, const QString&) {
                    ++calls;
                    // Re-enters handleMessage exactly as a real logging slot would.
                    // Unguarded, this is unbounded recursion.
                    if (calls < 100) {
                        logger.handleMessage(QtDebugMsg, QStringLiteral("[Scale] from the slot"));
                    }
                });

        logger.handleMessage(QtInfoMsg, QStringLiteral("[Scale][BLEManager] first"));

        // Exactly one emission: the outer line's. The nested line was buffered but
        // did not emit, so the slot ran once rather than 100 times.
        QCOMPARE(calls, 1);
        // ...and the nested line is still in the buffer, both lines present.
        const QStringList all = logger.getAllLines();
        QCOMPARE(all.size(), 2);
        QVERIFY(all[0].contains(QStringLiteral("first")));
        QVERIFY(all[1].contains(QStringLiteral("from the slot")));
    }
};

QTEST_GUILESS_MAIN(tst_WebDebugLogger)
#include "tst_webdebuglogger.moc"
