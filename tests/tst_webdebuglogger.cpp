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
};

QTEST_GUILESS_MAIN(tst_WebDebugLogger)
#include "tst_webdebuglogger.moc"
