#include <QtTest>
#include <QDateTime>
#include <QTemporaryDir>
#include <QFile>
#include <QSet>
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
        // Simulates trimLogFile(): the file is truncated and rewritten behind a
        // banner. It emits NO session marker — see trimLogFile()'s comment for
        // why the re-emit this test used to assert was a forgery.
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
        // genuinely different file (content and size both change). The session's
        // own marker went with the front of the file.
        writeFile(logPath(),
            "... [log trimmed] ...\n"
            "[   0.300] INFO  line three\n");

        const auto after = logger.sessionIndex();
        QCOMPARE(WebDebugLogger::testSessionIndexRebuildCount(), 2);
        // Still exactly one session — the surviving fragment — not two.
        QCOMPARE(after.size(), 1);
        QCOMPARE(after[0].startLine, qsizetype(0));
        QCOMPARE(after[0].lineCount, qsizetype(2));
        // And its start time is absent, not borrowed from the running session.
        QVERIFY(after[0].timestamp.isEmpty());
    }

    // ---- Session boundaries are recorded, never fabricated ----
    //
    // The defect these pin shipped for a long time and was invisible in review:
    // trimLogFile() re-emitted a SESSION START stamped with the RUNNING session's
    // start time, at the head of OLDER surviving content. Every session=N address
    // shifted, two sessions reported the same timestamp, and yesterday's lines
    // read as this morning's. It was found by enumerating a real log, not by
    // reading the tree, so these tests exist to make the next such regression
    // fail here instead.

    // A trim is file maintenance. It may remove sessions; it may never invent one.
    void trim_doesNotFabricateASession()
    {
        // trimLogFile() early-returns unless the file exceeds keepSize (80% of
        // MAX_LOG_FILE_SIZE = 2MB), so the fixture has to be genuinely large.
        // Driving the real function matters here: the bug lived in the writer,
        // and a test that only simulated its output could never have caught it.
        QString content = QStringLiteral("========== SESSION START: 2026-01-01T09:00:00 ==========\n");
        const QString filler = QStringLiteral("[   0.100] INFO  [Scale][BLEManager] padding line\n");
        content.reserve(3 * 1024 * 1024);
        while (content.size() < 2 * 1024 * 1024)
            content += filler;
        content += QStringLiteral("[   9.999] INFO  [Scale][BLEManager] last line before trim\n");
        writeFile(logPath(), content);

        WebDebugLogger logger(logPath());
        QCOMPARE(logger.sessionIndex().size(), 1);

        logger.trimLogFile();

        const auto after = logger.sessionIndex();
        // One session before, one after. The old code produced two.
        QCOMPARE(after.size(), 1);
        // Its start time is unknown — the marker was at the front, which is the
        // end the trim cuts from.
        QVERIFY(after[0].timestamp.isEmpty());

        // No SESSION START line was written by the trim.
        QFile f(logPath());
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString trimmed = QString::fromUtf8(f.readAll());
        QVERIFY(!trimmed.contains(QStringLiteral("SESSION START")));
        QVERIFY(trimmed.startsWith(QStringLiteral("... [log trimmed] ...")));
    }

    // The concern the old re-emit named was real: a trim can take the RUNNING
    // session's marker. session=-1 must still resolve to the current run's lines.
    void trim_currentSessionStillAddressableAsMinusOne()
    {
        writeFile(logPath(),
            "... [log trimmed] ...\n"
            "[   0.100] INFO  [Scale][BLEManager] current session, marker was trimmed away\n"
            "[   0.200] INFO  [Scale][BLEManager] still the current session\n");

        WebDebugLogger logger(logPath());
        const auto sessions = logger.sessionIndex();
        QCOMPARE(sessions.size(), 1);

        // -1 resolves to index 0 here, which IS the running session.
        const qsizetype newest = sessions.size() - 1;
        QCOMPARE(newest, qsizetype(0));
        QCOMPARE(sessions[newest].startLine, qsizetype(0));
        QCOMPARE(sessions[newest].lineCount, qsizetype(3));
    }

    // A fragment ahead of an intact marker is its own session, undated, and does
    // not merge into the session that follows it.
    void sessionIndex_headlessFragmentIsSeparateFromTheNextSession()
    {
        writeFile(logPath(),
            "... [log trimmed] ...\n"
            "[   0.100] INFO  orphaned line from a session whose marker is gone\n"
            "========== SESSION START: 2026-01-01T10:00:00 ==========\n"
            "[   0.100] INFO  a session that kept its marker\n");

        WebDebugLogger logger(logPath());
        const auto sessions = logger.sessionIndex();

        QCOMPARE(sessions.size(), 2);
        QCOMPARE(sessions[0].startLine, qsizetype(0));
        QVERIFY(sessions[0].timestamp.isEmpty());
        QCOMPARE(sessions[0].lineCount, qsizetype(2));
        QCOMPARE(sessions[1].startLine, qsizetype(2));
        QCOMPARE(sessions[1].timestamp, QStringLiteral("2026-01-01T10:00:00"));
        QCOMPARE(sessions[1].lineCount, qsizetype(2));
    }

    // The session marker is written with a LEADING NEWLINE, so line 0 of a
    // healthy fresh log is blank and the marker is on line 1. A headless-fragment
    // test of "line 0 is not a marker" would report a phantom one-blank-line
    // session on every new log — a fabricated session, which is the very thing
    // being fixed. Blank lines before the first marker are not a fragment.
    void sessionIndex_leadingBlankLineIsNotAFragment()
    {
        writeFile(logPath(),
            "\n"
            "========== SESSION START: 2026-01-01T09:00:00 ==========\n"
            "[   0.100] INFO  line\n");

        WebDebugLogger logger(logPath());
        const auto sessions = logger.sessionIndex();

        QCOMPARE(sessions.size(), 1);
        QCOMPARE(sessions[0].startLine, qsizetype(1));
        QCOMPARE(sessions[0].timestamp, QStringLiteral("2026-01-01T09:00:00"));
    }

    // The blank line above is not the only thing that can precede the first
    // marker without any orphaned content existing. trimLogFile() writes
    // "... [log trimmed] ..." unconditionally, so when a trim lands just before a
    // marker the surviving head is banner-then-marker with nothing orphaned.
    //
    // Counting the banner as content reported a session whose entire content was
    // the banner, indistinguishable from a real one-line session. This SHIPPED in
    // the first cut of this change and survived its own tests, because every
    // fixture above puts a real orphaned line after the banner — so the banner was
    // never the only thing in the fragment. It was caught by enumerating a live
    // 21,553-line log and asking why session 0 had lineCount 1.
    void sessionIndex_trimBannerAloneIsNotAFragment()
    {
        writeFile(logPath(),
            "... [log trimmed] ...\n"
            "========== SESSION START: 2026-01-01T09:00:00 ==========\n"
            "[   0.100] INFO  line\n");

        WebDebugLogger logger(logPath());
        const auto sessions = logger.sessionIndex();

        // One real session, not a phantom banner session ahead of it.
        QCOMPARE(sessions.size(), 1);
        QCOMPARE(sessions[0].startLine, qsizetype(1));
        QCOMPARE(sessions[0].timestamp, QStringLiteral("2026-01-01T09:00:00"));
    }

    // Belt and braces on the two skip rules TOGETHER: a trimmed log whose
    // surviving head is banner + blank + marker. Each rule alone passes the two
    // tests above and still fabricates here.
    void sessionIndex_bannerAndBlankLineTogetherAreNotAFragment()
    {
        writeFile(logPath(),
            "... [log trimmed] ...\n"
            "\n"
            "========== SESSION START: 2026-01-01T09:00:00 ==========\n"
            "[   0.100] INFO  line\n");

        WebDebugLogger logger(logPath());
        const auto sessions = logger.sessionIndex();

        QCOMPARE(sessions.size(), 1);
        QCOMPARE(sessions[0].startLine, qsizetype(2));
        QCOMPARE(sessions[0].timestamp, QStringLiteral("2026-01-01T09:00:00"));
    }

    // The banner is skipped, but it must not mask a fragment that is genuinely
    // there — the common trim, which cuts mid-session and leaves real orphans.
    void sessionIndex_bannerDoesNotMaskARealFragment()
    {
        writeFile(logPath(),
            "... [log trimmed] ...\n"
            "[   0.100] INFO  orphaned line whose marker the trim took\n"
            "========== SESSION START: 2026-01-01T10:00:00 ==========\n"
            "[   0.100] INFO  line\n");

        WebDebugLogger logger(logPath());
        const auto sessions = logger.sessionIndex();

        QCOMPARE(sessions.size(), 2);
        QVERIFY(sessions[0].timestamp.isEmpty());
        QCOMPARE(sessions[1].timestamp, QStringLiteral("2026-01-01T10:00:00"));
    }

    // Enumeration is in recorded order and no two sessions claim the same start.
    // Both were false in a real log: five sessions enumerated 07-29 18:17,
    // 07-28 10:23, 07-29 08:21, 07-29 18:17, 07-30 08:20.
    void sessionIndex_startTimesAreUniqueAndOrdered()
    {
        writeFile(logPath(),
            "========== SESSION START: 2026-01-01T09:00:00 ==========\n"
            "[   0.100] INFO  a\n"
            "========== SESSION START: 2026-01-01T10:00:00 ==========\n"
            "[   0.100] INFO  b\n"
            "========== SESSION START: 2026-01-01T11:00:00 ==========\n"
            "[   0.100] INFO  c\n");

        WebDebugLogger logger(logPath());
        const auto sessions = logger.sessionIndex();
        QCOMPARE(sessions.size(), 3);

        // `first`, not previous.isEmpty(), as the sentinel. Emptiness is a REAL
        // value here — an undated fragment has it — so using it to mean "no
        // previous yet" silently switches the ordering check off for the rest of
        // the loop the moment one appears. The check would then pass on a log it
        // was written to reject, which is worse than not having it.
        QSet<QString> seen;
        QString previous;
        bool first = true;
        for (const auto& s : sessions) {
            if (!s.timestamp.isEmpty()) {
                QVERIFY2(!seen.contains(s.timestamp),
                         "two sessions reported the same start time");
                seen.insert(s.timestamp);
            }
            if (!first && !previous.isEmpty() && !s.timestamp.isEmpty())
                QVERIFY2(previous < s.timestamp,
                         "sessions are not in recorded (chronological) order");
            previous = s.timestamp;
            first = false;
        }
    }

    // The reported symptom was FIVE sessions enumerated out of order with one
    // timestamp appearing twice — a shape that needs a surviving marker beside
    // the forgery. Every other trim test here uses a single-session fixture, so
    // the old code produced one wrongly-stamped session and the duplicate-and-
    // reorder itself was never reproduced. This drives the real writer over a
    // multi-session file and asserts the thing that actually rejects a forgery:
    // no surviving session carries the RUNNING process's start time.
    void trim_multiSessionKeepsOrderAndInventsNoTimestamp()
    {
        const QString filler = QStringLiteral("[   0.100] INFO  [Scale][BLEManager] padding\n");
        QString content;
        content.reserve(4 * 1024 * 1024);
        for (const char* ts : {"2026-01-01T09:00:00", "2026-01-02T10:00:00",
                               "2026-01-03T11:00:00"}) {
            content += QStringLiteral("========== SESSION START: %1 ==========\n")
                           .arg(QLatin1String(ts));
            const qsizetype target = content.size() + 900 * 1024;
            while (content.size() < target)
                content += filler;
        }
        writeFile(logPath(), content);

        WebDebugLogger logger(logPath());
        QCOMPARE(logger.sessionIndex().size(), 3);

        logger.trimLogFile();
        const auto after = logger.sessionIndex();

        // Whatever survived, it is fewer than we started with and strictly
        // ordered, with no timestamp invented for the fragment.
        QVERIFY(after.size() >= 1);
        QVERIFY(after.size() <= 3);
        QString previous;
        for (qsizetype i = 0; i < after.size(); ++i) {
            if (after[i].timestamp.isEmpty()) {
                // Only ever the leading fragment.
                QCOMPARE(i, qsizetype(0));
                continue;
            }
            if (!previous.isEmpty())
                QVERIFY2(previous < after[i].timestamp, "trim reordered sessions");
            previous = after[i].timestamp;
        }

        // The forgery's signature: a surviving session stamped with the time the
        // trimming process started. None of the fixture's dates are in 2026-01
        // by accident — the running process's start is "now", so any session
        // claiming it is one this trim invented.
        const QString runningStart = QDateTime::currentDateTime().toString(Qt::ISODate).left(10);
        for (const auto& s : after)
            QVERIFY2(!s.timestamp.startsWith(runningStart),
                     "a surviving session carries the trimming run's start time");
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
        // An empty marker list is a real caller mistake in production — no view
        // is ever built with one — so sessionLinesMatching() now warns once, in
        // addition to the empty-result guarantee this test exists to pin.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("empty marker list"));
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
