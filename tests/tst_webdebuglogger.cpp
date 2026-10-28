#include <QtTest>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>
#include <QBuffer>
#include <QThreadPool>
#include <thread>
#include <quazip/quazipfile.h>
#include "remote/serviceoperation.h"
#include "remote/filebundlepolicy.h"

#include "core/crashhandler.h"
#include "core/diagnosticlogging.h"
#include "core/btlogfilter.h"
#include "mcp/mcplogfilter.h"
#include "network/webdebuglogger.h"
#include "remote/systemlogservicesource.h"

static QStringList gcTestMessages;
static void gcTestHandler(QtMsgType, const QMessageLogContext&, const QString& message)
{
    gcTestMessages.append(message);
}

#ifdef Q_OS_MACOS
#include <mach-o/ldsyms.h>

static int codeInThisBinary() { return 42; }
#endif

// The protobuf wire format, written independently of CrashHandler's reader, so
// a tombstone can be built field by field from tombstone.proto's numbers.
namespace pb {
QByteArray varint(quint64 v)
{
    QByteArray out;
    do {
        char b = char(v & 0x7f);
        v >>= 7;
        if (v)
            b = char(b | 0x80);
        out.append(b);
    } while (v);
    return out;
}
QByteArray u(quint32 field, quint64 v) { return varint(quint64(field) << 3) + varint(v); }
QByteArray len(quint32 field, const QByteArray& b) { return varint((quint64(field) << 3) | 2) + varint(b.size()) + b; }
QByteArray fixed64(quint32 field) { return varint((quint64(field) << 3) | 1) + QByteArray(8, 'x'); }
QByteArray frame(quint64 relPc, const QByteArray& function, const QByteArray& file, const QByteArray& buildId = {})
{
    return u(1, relPc) + u(2, relPc + 0x7000000000) + len(4, function) + u(5, 16) + len(6, file)
         + (buildId.isEmpty() ? QByteArray() : len(8, buildId));
}
QByteArray threadEntry(quint32 tid, const QByteArray& name, const QList<QByteArray>& frames,
                       const QByteArray& extra = {})
{
    QByteArray thread = u(1, tid) + len(2, name) + len(3, len(1, "x0") + u(2, 1));
    for (const QByteArray& f : frames)
        thread += len(4, f);
    return len(16, u(1, tid) + len(2, thread + extra));
}
} // namespace pb

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

    // Writes `content` where CrashHandler::getDebugLogTail() will look for it.
    // Test mode (enabled for the whole run in initTestCase) redirects
    // AppDataLocation — and hence DecenzaPaths::logsDirectory()'s desktop answer
    // — under ~/.qttest, scoped by this binary's name, so neither a parallel
    // ctest run nor the developer's real log is touched.
    static void writeDebugLog(const QString& content)
    {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QVERIFY2(QDir().mkpath(dir), "failed to create test app-data directory");
        writeFile(dir + "/debug.log", content);
    }

    // Asked of CrashHandler rather than spelled out, so a respelt marker fails
    // these tests instead of silently disabling the strip in production.
    static QString startMarker() { return QString::fromLatin1(CrashHandler::kReportStart); }
    static QString endMarker()   { return QString::fromLatin1(CrashHandler::kReportEnd); }

private slots:
    void init() { QTest::failOnWarning(); }

    // Enabled once for the whole run rather than per slot: a QVERIFY failure
    // returns from the slot immediately, so a per-slot disable is skipped on
    // exactly the run where it matters and leaks a process-global path
    // redirection into whatever executes next.
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void cleanupTestCase() { QStandardPaths::setTestModeEnabled(false); }

    void systemLogSnapshot_isFilteredAndBounded()
    {
        writeFile(logPath(), {});
        WebDebugLogger logger(logPath());
        NativeSystemLogServiceSource source(&logger);
        QSignalSpy snapshotSpy(&source, &SystemLogStateSimpleSource::snapshotCompleted);
        QVERIFY(snapshotSpy.isValid());

        logger.handleMessage(QtInfoMsg, QStringLiteral("[DE1] older"));
        logger.handleMessage(QtDebugMsg, QStringLiteral("[DE1] debug"));
        logger.handleMessage(QtInfoMsg, QStringLiteral("[Scale] unrelated"));
        logger.handleMessage(QtInfoMsg, QStringLiteral("[DE1] newest"));

        const QString requestId = QStringLiteral("b3d94c92-71c9-4df0-8c6d-5a3e761d77bd");
        source.requestSnapshot(requestId, {QStringLiteral("[DE1]")},
                               QStringLiteral("INFO"), 1);
        QTRY_COMPARE(snapshotSpy.count(), 1);
        const QList<QVariant> snapshot = snapshotSpy.takeFirst();
        QCOMPARE(snapshot.at(0).toString(), requestId);
        QVERIFY(snapshot.at(1).toBool());
        const QStringList lines = snapshot.at(2).toStringList();
        QCOMPARE(lines.size(), 1);
        QVERIFY(lines.first().contains(QStringLiteral("newest")));
        QVERIFY(!lines.first().contains(QStringLiteral("older")));
        QVERIFY(!lines.first().contains(QStringLiteral("debug")));
        QVERIFY(!lines.first().contains(QStringLiteral("unrelated")));
        QVERIFY(snapshot.at(3).toLongLong() > 0);
    }

    void systemLogRawStream_isSingleAndFiltersShareCanonicalPredicate()
    {
        writeFile(logPath(), {});
        WebDebugLogger logger(logPath());
        NativeSystemLogServiceSource source(&logger);
        QSignalSpy lineSpy(&source, &SystemLogStateSimpleSource::lineAppended);
        QSignalSpy boundarySpy(&source, &SystemLogStateSimpleSource::boundaryCompleted);
        QVERIFY(lineSpy.isValid());
        QVERIFY(boundarySpy.isValid());

        const QString requestId = QStringLiteral("ff13f0a4-b072-4edf-9d9a-112a86508ca2");
        source.requestBoundary(requestId);
        QCOMPARE(boundarySpy.count(), 1);
        const auto boundary = boundarySpy.takeFirst();
        QCOMPARE(boundary.at(0).toString(), requestId);
        QVERIFY(boundary.at(1).toBool());
        const qint64 before = boundary.at(2).toLongLong();

        logger.handleMessage(QtDebugMsg, QStringLiteral("[Scale] below threshold"));
        logger.handleMessage(QtInfoMsg, QStringLiteral("[DE1] other subsystem"));
        logger.handleMessage(QtInfoMsg, QStringLiteral("[Scale] matching line"));
        QTRY_COMPARE(lineSpy.count(), 3); // Logger delivery is queued outside its message handler.

        const auto raw = lineSpy.at(2);
        const QString line = raw.at(0).toString();
        const qint64 sequence = raw.at(1).toLongLong();
        QVERIFY(sequence > before);
        QVERIFY(McpLogFilter::lineMatches(line, {QStringLiteral("[Scale]")}, QStringLiteral("INFO")));
        QVERIFY(!McpLogFilter::lineMatches(lineSpy.at(0).at(0).toString(),
                                           {QStringLiteral("[Scale]")}, QStringLiteral("INFO")));
        QVERIFY(!McpLogFilter::lineMatches(lineSpy.at(1).at(0).toString(),
                                           {QStringLiteral("[Scale]")}, QStringLiteral("INFO")));

        logger.handleMessage(QtInfoMsg, QStringLiteral("[Scale] after boundary"));
        QTRY_COMPARE(lineSpy.count(), 4);
    }

    void systemLogMultilineStreamKeepsEveryLineAcrossSnapshotBoundary()
    {
        writeFile(logPath(), {});
        WebDebugLogger logger(logPath());
        NativeSystemLogServiceSource source(&logger);
        QSignalSpy streamed(&source, &NativeSystemLogServiceSource::lineAppended);
        QSignalSpy snapshots(&source, &NativeSystemLogServiceSource::snapshotCompleted);
        logger.handleMessage(QtInfoMsg, QStringLiteral("[Scale][Fixture] first\nsecond\nthird"));
        source.requestSnapshot(ServiceOperation::newId(), {QStringLiteral("[Scale]")}, QStringLiteral("INFO"), 10);
        QTRY_COMPARE(snapshots.count(), 1);
        const QStringList before = snapshots.first().at(2).toStringList();
        const qint64 boundary = snapshots.first().at(3).toLongLong();
        QCOMPARE(before.size(), 3);
        logger.handleMessage(QtInfoMsg, QStringLiteral("[Scale][Fixture] fourth\nfifth"));
        QTRY_COMPARE(streamed.count(), 5);
        qint64 shownThrough = boundary;
        QStringList after;
        for (qsizetype i = 0; i < streamed.size(); ++i) {
            const auto event = streamed.at(i);
            const qint64 sequence = event.at(1).toLongLong();
            if (i > 0) QVERIFY(sequence > streamed.at(i - 1).at(1).toLongLong());
            if (sequence <= shownThrough) continue;
            shownThrough = sequence;
            after.append(event.at(0).toString());
        }
        QCOMPARE(after.size(), 2);
        QVERIFY(after.first().contains(QStringLiteral("fourth")));
        QVERIFY(after.last().contains(QStringLiteral("fifth")));
        QCOMPARE(logger.sessionSnapshotReader({QStringLiteral("[Scale]")}, QStringLiteral("INFO"), 0, FileBundlePolicy::MaximumLogBytes)().lines, before + after);
    }

    void concurrentMultilineDeliveriesKeepPersistedSequenceOrder()
    {
        writeFile(logPath(), {});
        WebDebugLogger logger(logPath());
        QSignalSpy streamed(&logger, &WebDebugLogger::lineAppended);
        const auto logMessages = [&logger](const QString& label) {
            for (int i = 0; i < 50; ++i)
                logger.handleMessage(QtInfoMsg, QStringLiteral("[Scale][Fixture] %1 %2 first\nsecond").arg(label).arg(i));
        };
        std::thread first(logMessages, QStringLiteral("first-thread"));
        std::thread second(logMessages, QStringLiteral("second-thread"));
        first.join();
        second.join();
        QTRY_COMPARE(streamed.count(), 200);
        QStringList delivered;
        for (qsizetype i = 0; i < streamed.size(); ++i) {
            delivered.append(streamed.at(i).at(1).toString());
            if (i > 0)
                QCOMPARE(streamed.at(i).at(2).toLongLong(), streamed.at(i - 1).at(2).toLongLong() + 1);
        }
        QCOMPARE(delivered, logger.sessionSnapshotReader({QStringLiteral("[Scale]")}, QStringLiteral("INFO"), 0, FileBundlePolicy::MaximumLogBytes)().lines);
        QCOMPARE(logger.writtenSequence(), streamed.last().at(2).toLongLong());
    }

    void systemLogSnapshot_rejectsMalformedOrUnboundedRequests()
    {
        writeFile(logPath(), {});
        WebDebugLogger logger(logPath());
        NativeSystemLogServiceSource source(&logger);
        QSignalSpy snapshotSpy(&source, &SystemLogStateSimpleSource::snapshotCompleted);
        QVERIFY(snapshotSpy.isValid());
        const QString requestId = QStringLiteral("45d87ab3-cdce-4faf-9a2c-d1f0d9827a80");

        source.requestSnapshot(requestId, {}, QStringLiteral("INFO"), 10);
        QCOMPARE(snapshotSpy.count(), 1);
        QVERIFY(!snapshotSpy.takeFirst().at(1).toBool());

        source.requestSnapshot(requestId, {QStringLiteral("[DE1]")}, QStringLiteral("INFO"), 1001);
        QCOMPARE(snapshotSpy.count(), 1);
        QVERIFY(!snapshotSpy.takeFirst().at(1).toBool());

        source.requestSnapshot(QStringLiteral("not-a-uuid"), {QStringLiteral("[DE1]")},
                               QStringLiteral("INFO"), 10);
        QCOMPARE(snapshotSpy.count(), 1);
        QVERIFY(!snapshotSpy.takeFirst().at(1).toBool());
    }

    void asynchronousSnapshotsKeepPhysicalAdmissionBoundedUntilCallbacksDrain()
    {
        writeFile(logPath(), {});
        WebDebugLogger logger(logPath());
        logger.handleMessage(QtInfoMsg, QStringLiteral("[DE1] bounded snapshot"));
        NativeSystemLogServiceSource source(&logger);
        QSignalSpy snapshots(&source, &NativeSystemLogServiceSource::snapshotCompleted);
        for (int i = 0; i < 8; ++i)
            source.requestSnapshot(ServiceOperation::newId(), {QStringLiteral("[DE1]")},
                                   QStringLiteral("INFO"), 400);
        // Even workers that have finished cannot reopen admission until their
        // application-thread completion has been handled.
        QThreadPool::globalInstance()->waitForDone();
        QCOMPARE(snapshots.count(), 0);
        const QString refused = ServiceOperation::newId();
        source.requestSnapshot(refused, {QStringLiteral("[DE1]")}, QStringLiteral("INFO"), 400);
        QCOMPARE(snapshots.count(), 1);
        QCOMPARE(snapshots.first().at(0).toString(), refused);
        QVERIFY(!snapshots.first().at(1).toBool());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(snapshots.count(), 9);
        for (int i = 1; i < snapshots.count(); ++i) {
            QVERIFY(snapshots.at(i).at(1).toBool());
            QCOMPARE(snapshots.at(i).at(2).toStringList().size(), 1);
        }
        source.requestSnapshot(ServiceOperation::newId(), {QStringLiteral("[DE1]")}, QStringLiteral("INFO"), 400);
        QTRY_COMPARE(snapshots.count(), 10);
        QVERIFY(snapshots.last().at(1).toBool());
    }

    void asynchronousSnapshotsPreserveTheCopyFenceAcrossQueuedLiveDelivery()
    {
        writeFile(logPath(), {});
        WebDebugLogger logger(logPath());
        NativeSystemLogServiceSource source(&logger);
        QSignalSpy snapshots(&source, &NativeSystemLogServiceSource::snapshotCompleted);
        QSignalSpy streamed(&source, &NativeSystemLogServiceSource::lineAppended);
        logger.handleMessage(QtInfoMsg, QStringLiteral("[Scale] first\nsecond"));
        source.requestSnapshot(ServiceOperation::newId(), {QStringLiteral("[Scale]")}, QStringLiteral("INFO"), 400);
        QThreadPool::globalInstance()->waitForDone();
        QCOMPARE(snapshots.count(), 0);
        logger.handleMessage(QtInfoMsg, QStringLiteral("[Scale] third\nfourth"));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(snapshots.count(), 1);
        QTRY_COMPARE(streamed.count(), 4);
        const auto snapshot = snapshots.first();
        QVERIFY(snapshot.at(1).toBool());
        const qint64 fence = snapshot.at(3).toLongLong();
        QStringList merged = snapshot.at(2).toStringList();
        QCOMPARE(merged.size(), 2);
        for (const auto& line : streamed)
            if (line.at(1).toLongLong() > fence) merged.append(line.at(0).toString());
        QCOMPARE(merged, logger.sessionSnapshotReader({QStringLiteral("[Scale]")}, QStringLiteral("INFO"), 0, FileBundlePolicy::MaximumLogBytes)().lines);
        QCOMPARE(merged.size(), 4);
    }

    void asynchronousSnapshotHandlesLoggerAndSourceDestructionWithoutWorkerQObjectAccess()
    {
        writeFile(logPath(), {});
        auto logger = std::make_unique<WebDebugLogger>(logPath());
        logger->handleMessage(QtInfoMsg, QStringLiteral("[DE1] retained file state"));
        const auto reader = logger->sessionSnapshotReader({QStringLiteral("[DE1]")},
            QStringLiteral("INFO"), 400, FileBundlePolicy::MaximumLogBytes);
        auto source = std::make_unique<NativeSystemLogServiceSource>(logger.get());
        QSignalSpy snapshots(source.get(), &NativeSystemLogServiceSource::snapshotCompleted);
        source->requestSnapshot(ServiceOperation::newId(), {QStringLiteral("[DE1]")}, QStringLiteral("INFO"), 400);
        logger.reset();
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(snapshots.count(), 1);
        QVERIFY(!snapshots.first().at(1).toBool());
        QVERIFY(!source->ready());
        WebDebugLogger::SessionSnapshot retained;
        QThreadPool::globalInstance()->start([reader, &retained] { retained = reader(); });
        QThreadPool::globalInstance()->waitForDone();
        QVERIFY(retained.reason.isEmpty());
        QCOMPARE(retained.lines.size(), 1);
        QCOMPARE(retained.sequence, qint64(1));
        QVERIFY(retained.lines.first().contains(QStringLiteral("retained file state")));

        logger = std::make_unique<WebDebugLogger>(logPath());
        source = std::make_unique<NativeSystemLogServiceSource>(logger.get());
        QObject observer;
        int delivered = 0;
        connect(source.get(), &NativeSystemLogServiceSource::snapshotCompleted, &observer,
                [&delivered] { ++delivered; });
        source->requestSnapshot(ServiceOperation::newId(), {QStringLiteral("[DE1]")}, QStringLiteral("INFO"), 400);
        source.reset();
        logger.reset();
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(delivered, 0);
    }

    void asynchronousSnapshotRejectsAnOversizedRawFileAndCanRetry()
    {
        writeFile(logPath(), QStringLiteral("x").repeated(FileBundlePolicy::MaximumLogBytes + 1));
        WebDebugLogger logger(logPath());
        NativeSystemLogServiceSource source(&logger);
        QSignalSpy snapshots(&source, &NativeSystemLogServiceSource::snapshotCompleted);
        source.requestSnapshot(ServiceOperation::newId(), {QStringLiteral("[DE1]")}, QStringLiteral("INFO"), 400);
        QTRY_COMPARE(snapshots.count(), 1);
        QVERIFY(!snapshots.first().at(1).toBool());
        QCOMPARE(snapshots.first().at(4).toString(), QStringLiteral("fileTooLarge"));
        writeFile(logPath(), QStringLiteral("[0.100] INFO [DE1] recovered\n"));
        source.requestSnapshot(ServiceOperation::newId(), {QStringLiteral("[DE1]")}, QStringLiteral("INFO"), 400);
        QTRY_COMPARE(snapshots.count(), 2);
        QVERIFY(snapshots.last().at(1).toBool());
        QCOMPARE(snapshots.last().at(2).toStringList().size(), 1);
    }

    void missingLogDownloadCanRetryAfterTheFileBecomesAvailable()
    {
        const QString missing = m_dir.filePath(QStringLiteral("recovered-debug.log"));
        QVERIFY(!QFile::exists(missing));
        WebDebugLogger logger(missing);
        NativeSystemLogServiceSource source(&logger);
        const QString client = ServiceOperation::newId();
        source.openClient(client);
        QSignalSpy completed(&source, &NativeSystemLogServiceSource::operationCompleted);
        const auto prepare = [&] {
            const QString id = ServiceOperation::newId();
            source.execute(id, QStringLiteral("prepareLogDownload"),
                {{QStringLiteral("clientId"), client}, {QStringLiteral("format"), QStringLiteral("log")}});
            return id;
        };
        const QString failedId = prepare();
        QTRY_COMPARE(completed.count(), 1);
        QCOMPARE(completed.first().at(0).toString(), failedId);
        QVERIFY(!completed.first().at(2).toBool());
        QCOMPARE(completed.takeFirst().at(4).toString(), QStringLiteral("logReadFailed"));

        const QByteArray contents("[DE1] recovered persisted diagnostic\n");
        writeFile(missing, QString::fromUtf8(contents));
        const QString recoveredId = prepare();
        QTRY_COMPARE(completed.count(), 1);
        const auto recovered = completed.takeFirst();
        QCOMPARE(recovered.at(0).toString(), recoveredId);
        QVERIFY2(recovered.at(2).toBool(), qPrintable(recovered.at(4).toString()));
        const QVariantMap bundle = recovered.at(3).toMap();
        source.execute(ServiceOperation::newId(), QStringLiteral("readBundleChunk"),
            {{QStringLiteral("clientId"), client},
             {QStringLiteral("bundleId"), bundle.value(QStringLiteral("bundleId"))},
             {QStringLiteral("offset"), 0}});
        QCOMPARE(completed.count(), 1);
        const auto chunk = completed.takeFirst();
        QVERIFY(chunk.at(2).toBool());
        QCOMPARE(chunk.at(3).toMap().value(QStringLiteral("data")).toByteArray(), contents);
        source.closeClient(client);
    }

    void sparseSessionSnapshotReadsPastAnUnrelatedNearLimitTail()
    {
        QString contents;
        constexpr int matches = 400;
        for (int i = 0; i < matches; ++i)
            contents += QStringLiteral("[0.100] INFO [DE1] retained %1\n").arg(i);
        const QString filler = QStringLiteral("[0.100] DEBUG [Scale] unrelated ")
            + QString(180, QLatin1Char('x')) + QLatin1Char('\n');
        constexpr qsizetype nearLimit = 2 * 1024 * 1024 - 64 * 1024;
        while (contents.size() + filler.size() <= nearLimit)
            contents += filler;
        writeFile(logPath(), contents);
        WebDebugLogger logger(logPath());
        NativeSystemLogServiceSource source(&logger);
        QSignalSpy snapshots(&source, &NativeSystemLogServiceSource::snapshotCompleted);
        source.requestSnapshot(ServiceOperation::newId(), {QStringLiteral("[DE1]")}, QStringLiteral("INFO"), matches);
        QTRY_COMPARE(snapshots.count(), 1);
        QVERIFY(snapshots.first().at(1).toBool());
        const QStringList lines = snapshots.first().at(2).toStringList();
        QCOMPARE(lines.size(), matches);
        QVERIFY(lines.first().endsWith(QStringLiteral("retained 0")));
        QVERIFY(lines.last().endsWith(QStringLiteral("retained 399")));
    }

    void logDownloadRoundTrip_data()
    {
        QTest::addColumn<QString>("format");
        QTest::newRow("plain-log") << QStringLiteral("log");
        QTest::newRow("compressed-zip") << QStringLiteral("zip");
    }

    void logDownloadRoundTrip()
    {
        QFETCH(QString, format);
        const QString content = QStringLiteral("[DE1] preserved diagnostic line\n").repeated(5000);
        const QString summary = QStringLiteral("\nMemory summary: test snapshot\n");
        writeFile(logPath(), content);
        WebDebugLogger logger(logPath());
        NativeSystemLogServiceSource source(&logger);
        source.setMemorySummaryProvider([summary] { return summary; });
        const QString owner = ServiceOperation::newId();
        const QString other = ServiceOperation::newId();
        source.openClient(owner);
        source.openClient(other);
        QSignalSpy completed(&source, &NativeSystemLogServiceSource::operationCompleted);
        const QString prepare = ServiceOperation::newId();
        source.execute(prepare, QStringLiteral("prepareLogDownload"),
                       {{QStringLiteral("clientId"), owner}, {QStringLiteral("format"), format}});
        QTRY_COMPARE(completed.count(), 1);
        const auto prepared = completed.takeFirst();
        QCOMPARE(prepared.at(0).toString(), prepare);
        QVERIFY2(prepared.at(2).toBool(), qPrintable(prepared.at(4).toString()));
        const QVariantMap bundle = prepared.at(3).toMap();
        QCOMPARE(bundle.value(QStringLiteral("displayName")).toString(), QStringLiteral("debug.") + format);
        QCOMPARE(bundle.size(), 5); // Only opaque identity and save metadata, no host path.
        const QString bundleId = bundle.value(QStringLiteral("bundleId")).toString();
        QVERIFY(!bundleId.isEmpty());
        source.execute(ServiceOperation::newId(), QStringLiteral("readBundleChunk"),
                       {{QStringLiteral("clientId"), other}, {QStringLiteral("bundleId"), bundleId},
                        {QStringLiteral("offset"), 0}});
        QCOMPARE(completed.count(), 1);
        QVERIFY(!completed.takeFirst().at(2).toBool());
        QByteArray output;
        const int total = bundle.value(QStringLiteral("size")).toInt();
        while (output.size() < total) {
            source.execute(ServiceOperation::newId(), QStringLiteral("readBundleChunk"),
                           {{QStringLiteral("clientId"), owner}, {QStringLiteral("bundleId"), bundleId},
                            {QStringLiteral("offset"), output.size()}});
            QCOMPARE(completed.count(), 1);
            const auto reply = completed.takeFirst();
            QVERIFY(reply.at(2).toBool());
            const QVariantMap chunk = reply.at(3).toMap();
            const QByteArray bytes = chunk.value(QStringLiteral("data")).toByteArray();
            QVERIFY(!bytes.isEmpty());
            QVERIFY(bytes.size() <= FileBundlePolicy::MaximumChunkBytes);
            output += bytes;
            QCOMPARE(chunk.value(QStringLiteral("nextOffset")).toInt(), output.size());
            QCOMPARE(chunk.value(QStringLiteral("complete")).toBool(), output.size() == total);
        }
        const QByteArray expected = (content + summary).toUtf8();
        if (format == QLatin1String("zip")) {
            QBuffer buffer(&output);
            QVERIFY(buffer.open(QIODevice::ReadOnly));
            QuaZip reader(&buffer);
            QVERIFY(reader.open(QuaZip::mdUnzip));
            QCOMPARE(reader.getEntriesCount(), 1);
            QVERIFY(reader.setCurrentFile(QStringLiteral("debug.log")));
            QuaZipFile entry(&reader);
            QVERIFY(entry.open(QIODevice::ReadOnly));
            QCOMPARE(entry.readAll(), expected);
            entry.close();
            QCOMPARE(entry.getZipError(), UNZ_OK);
        } else QCOMPARE(output, expected);
        source.closeClient(owner);
        source.execute(ServiceOperation::newId(), QStringLiteral("readBundleChunk"),
                       {{QStringLiteral("clientId"), owner}, {QStringLiteral("bundleId"), bundleId},
                        {QStringLiteral("offset"), 0}});
        QCOMPARE(completed.count(), 1);
        QCOMPARE(completed.takeFirst().at(4).toString(), QStringLiteral("clientUnavailable"));
        source.openClient(owner);
        source.execute(ServiceOperation::newId(), QStringLiteral("readBundleChunk"),
                       {{QStringLiteral("clientId"), owner}, {QStringLiteral("bundleId"), bundleId},
                        {QStringLiteral("offset"), 0}});
        QCOMPARE(completed.takeFirst().at(4).toString(), QStringLiteral("invalidBundle"));
    }

    void logDownloadCancellationKeepsWorkerBoundedAndRetiresPreparedOutput()
    {
        writeFile(logPath(), QStringLiteral("[DE1] saved log\n").repeated(10000));
        WebDebugLogger logger(logPath());
        NativeSystemLogServiceSource source(&logger);
        const QString client = ServiceOperation::newId();
        source.openClient(client);
        QSignalSpy completed(&source, &NativeSystemLogServiceSource::operationCompleted);
        const auto prepare = [&] {
            const QString id = ServiceOperation::newId();
            source.execute(id, QStringLiteral("prepareLogDownload"),
                           {{QStringLiteral("clientId"), client}, {QStringLiteral("format"), QStringLiteral("zip")}});
            return id;
        };
        const QString cancelled = prepare();
        source.execute(ServiceOperation::newId(), QStringLiteral("cancelFileOperation"),
                       {{QStringLiteral("clientId"), client}, {QStringLiteral("requestId"), cancelled}});
        QCOMPARE(completed.count(), 2);
        QCOMPARE(completed.at(0).at(0).toString(), cancelled);
        QCOMPARE(completed.at(0).at(4).toString(), QStringLiteral("cancelled"));
        QVERIFY(completed.at(1).at(3).toMap().value(QStringLiteral("cancelled")).toBool());
        prepare();
        QCOMPARE(completed.count(), 3);
        QCOMPARE(completed.at(2).at(4).toString(), QStringLiteral("busy"));
        // Drain the real worker and its application-thread callback; cancellation
        // must not open another physical worker slot before this callback runs.
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(completed.count(), 3);
        completed.clear();
        const QString preparedId = prepare();
        QTRY_COMPARE(completed.count(), 1);
        const auto reply = completed.takeFirst();
        QVERIFY2(reply.at(2).toBool(), qPrintable(reply.at(4).toString()));
        const QString bundleId = reply.at(3).toMap().value(QStringLiteral("bundleId")).toString();
        source.execute(ServiceOperation::newId(), QStringLiteral("cancelFileOperation"),
                       {{QStringLiteral("clientId"), client}, {QStringLiteral("requestId"), preparedId}});
        QVERIFY(completed.takeFirst().at(3).toMap().value(QStringLiteral("cancelled")).toBool());
        source.execute(ServiceOperation::newId(), QStringLiteral("readBundleChunk"),
                       {{QStringLiteral("clientId"), client}, {QStringLiteral("bundleId"), bundleId},
                        {QStringLiteral("offset"), 0}});
        QCOMPARE(completed.takeFirst().at(4).toString(), QStringLiteral("invalidBundle"));
    }

    void logDownloadDisconnectAndDestructionDoNotPublishStaleOutput()
    {
        writeFile(logPath(), QStringLiteral("[DE1] diagnostic snapshot\n"));
        auto logger = std::make_unique<WebDebugLogger>(logPath());
        const auto reader = logger->persistedLogReader(FileBundlePolicy::MaximumLogBytes);
        auto source = std::make_unique<NativeSystemLogServiceSource>(logger.get());
        const QString client = ServiceOperation::newId();
        source->openClient(client);
        QSignalSpy completed(source.get(), &NativeSystemLogServiceSource::operationCompleted);
        const QString request = ServiceOperation::newId();
        source->execute(request, QStringLiteral("prepareLogDownload"),
                        {{QStringLiteral("clientId"), client}, {QStringLiteral("format"), QStringLiteral("zip")}});
        source->closeClient(client);
        QCOMPARE(completed.count(), 1);
        QCOMPARE(completed.at(0).at(0).toString(), request);
        QCOMPARE(completed.at(0).at(4).toString(), QStringLiteral("cancelled"));
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(completed.count(), 1);
        source->openClient(client);
        source->execute(ServiceOperation::newId(), QStringLiteral("prepareLogDownload"),
                        {{QStringLiteral("clientId"), client}, {QStringLiteral("format"), QStringLiteral("log")}});
        source.reset();
        logger.reset();
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QByteArray bytes;
        QString reason;
        QVERIFY(reader(&bytes, &reason));
        QCOMPARE(bytes, QByteArray("[DE1] diagnostic snapshot\n"));
    }

    void logDownloadRejectsPathsAndOversizedFiles()
    {
        writeFile(logPath(), QStringLiteral("x").repeated(FileBundlePolicy::MaximumLogBytes + 1));
        WebDebugLogger logger(logPath());
        NativeSystemLogServiceSource source(&logger);
        const QString client = ServiceOperation::newId();
        source.openClient(client);
        QSignalSpy completed(&source, &NativeSystemLogServiceSource::operationCompleted);
        source.execute(ServiceOperation::newId(), QStringLiteral("prepareLogDownload"),
                       {{QStringLiteral("clientId"), client}, {QStringLiteral("format"), QStringLiteral("log")},
                        {QStringLiteral("path"), logPath()}});
        QCOMPARE(completed.takeFirst().at(4).toString(), QStringLiteral("invalidArguments"));
        source.execute(ServiceOperation::newId(), QStringLiteral("prepareLogDownload"),
                       {{QStringLiteral("clientId"), client}, {QStringLiteral("format"), QStringLiteral("log")}});
        QTRY_COMPARE(completed.count(), 1);
        QCOMPARE(completed.takeFirst().at(4).toString(), QStringLiteral("fileTooLarge"));
    }

    void applicationAndQmlHelpersShareTheRegistry()
    {
        WebDebugLogger logger(logPath());
        QVERIFY(DecenzaLog::isRegistered("Battery"));
        QVERIFY(!DecenzaLog::isRegistered("battery"));
        QTest::ignoreMessage(QtInfoMsg, "[Battery][BatteryManager] requesting charge enable");
        DIAG_INFO(BATTERY, "BatteryManager") << "requesting charge enable";
        QTest::ignoreMessage(QtWarningMsg, "[Recipes][RecipesItem] Start blocked");
        logger.warn("Recipes", "RecipesItem", "Start blocked");
        QTest::ignoreMessage(QtWarningMsg, "[Runtime][bad_tag] Unknown owner");
        logger.warn("not-registered", "bad]tag", "Unknown owner");
    }

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

    // The line prefix is SECONDS SINCE SESSION START, inside ONE bracket.
    //
    // Both halves are load-bearing and neither was pinned before.
    //
    // Elapsed rather than a wall clock, deliberately: this branch tried the swap
    // and reverted it. A wall clock carries no date, so on the 73-hour session in
    // the log that motivated the change, "09:04" matched three different days
    // with nothing on the line to choose between them — it looked like a
    // zero-arithmetic join and was actually an ambiguous one. Elapsed is
    // monotone and unambiguous within a session, and the absolute anchor already
    // exists once per session on the SESSION START marker. It is also 4 bytes per
    // line cheaper, which is ~5% of the 2 MB cap over a full buffer.
    //
    // One bracket, because lineLevel() and stripTimestampPrefix() both match
    // "[^]]*" inside a single bracket. A second bracket here would silently break
    // both parsers and every log already on disk.
    void handleMessage_prefixesLinesWithElapsedSeconds()
    {
        WebDebugLogger logger(logPath());
        logger.handleMessage(QtWarningMsg, QStringLiteral("hello"));

        const QStringList lines = logger.getPersistedLogChunk(0, 100);
        QVERIFY(!lines.isEmpty());
        const QString line = lines.last();

        // [ SSSS.mmm] LEVEL message — right-aligned to 8, three decimals.
        QRegularExpression shape(QStringLiteral(R"(^\[\s*\d+\.\d{3}\] WARN\s+\[Runtime\]\[Unattributed\] hello$)"));
        QVERIFY2(shape.match(line).hasMatch(), qPrintable("unexpected line shape: " + line));

        // And the shared parsers read it.
        QCOMPARE(McpLogFilter::lineLevel(line), QStringLiteral("WARN"));
        QCOMPARE(McpLogFilter::stripTimestampPrefix(line), QStringLiteral("WARN  [Runtime][Unattributed] hello"));
    }

    void multilineWarningsRetainContextAndCannotCreateSessions()
    {
        writeFile(logPath(), "========== SESSION START: 2026-09-09T09:00:00 ==========\n");
        WebDebugLogger logger(logPath());
        QSignalSpy spy(&logger, &WebDebugLogger::lineAppended);
        const QMessageLogContext context("/build/Decenza/qml/components/BagCard.qml", 42,
                                         "updateDetails", "qml");
        logger.handleMessage(QtWarningMsg,
            QStringLiteral("[BeanBase][Extract] failed\r\n"
                           "========== SESSION START: 2099-01-01T00:00:00 ==========\n"
                           "last detail"), context);
        const auto lines = logger.sessionSnapshotReader({QStringLiteral("[BeanBase]")}, "WARN", 0, FileBundlePolicy::MaximumLogBytes)().lines;
        QCOMPARE(lines.size(), 3);
        QTRY_COMPARE(spy.size(), 3);
        for (qsizetype i = 1; i < spy.size(); ++i)
            QCOMPARE(spy.at(i).at(2).toLongLong(), spy.at(i - 1).at(2).toLongLong() + 1);
        QCOMPARE(logger.writtenSequence(), spy.last().at(2).toLongLong());
        for (const auto& line : lines) {
            QVERIFY(line.contains("WARN  [BeanBase][Extract] "));
            QVERIFY(line.contains("source=qml/components/BagCard.qml:42"));
            QVERIFY(line.contains("category=qml"));
            QVERIFY(!line.contains("function=updateDetails"));
            QVERIFY(!line.contains("/build/"));
        }
        QVERIFY(lines[1].contains("2099-01-01"));
        QVERIFY(lines[2].contains("last detail"));
        QCOMPARE(logger.sessionIndex().size(), 1);
        QCOMPARE(logger.getAllLines(), lines);
    }

    void unattributedContextIsPreservedWithoutInventingAnOwner()
    {
        WebDebugLogger logger(logPath());
        const QMessageLogContext context(nullptr, 0, nullptr, "qt.network.ssl");
        logger.handleMessage(QtWarningMsg, "QIODevice::read: device not open", context);
        const auto line = logger.getAllLines().last();
        QVERIFY(line.contains("[Runtime][Unattributed] QIODevice::read: device not open"));
        QVERIFY(line.contains("category=qt.network.ssl"));
        QVERIFY(!line.contains("source="));
        QVERIFY(!line.contains("function="));
        QVERIFY(!McpLogFilter::lineMatches(line, {"[Network]"}, "WARN"));
        QVERIFY(McpLogFilter::lineMatches(line, {"[Runtime]"}, "WARN"));
        logger.handleMessage(QtDebugMsg, "[Memory][Sample] detail", context);
        QVERIFY(!logger.getAllLines().last().contains("category="));
    }

    void functionIsRetainedWhenSourceLocationIsUnavailable()
    {
        WebDebugLogger logger(logPath());
        const QMessageLogContext context(nullptr, 0, "readResponse", "qt.network.ssl");
        logger.handleMessage(QtWarningMsg, "device not open", context);
        const auto line = logger.getAllLines().last();
        QVERIFY(line.contains("function=readResponse"));
        QVERIFY(!line.contains("source="));
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

    // ---- sessionSnapshotReader(): the query the connections-page views run ----
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

        const auto de1 = logger.sessionSnapshotReader({QStringLiteral("[DE1]")},
                                                     QStringLiteral("INFO"), 0, FileBundlePolicy::MaximumLogBytes)().lines;
        QCOMPARE(de1.size(), 1);
        QVERIFY(de1[0].contains(QStringLiteral("DE1 CONNECTED")));

        const auto scale = logger.sessionSnapshotReader(
            {QStringLiteral("[Scale]"), QStringLiteral("[Refractometer]")},
            QStringLiteral("INFO"), 0, FileBundlePolicy::MaximumLogBytes)().lines;
        QCOMPARE(scale.size(), 2);
        QVERIFY(scale[0].contains(QStringLiteral("AcaiaScale")));
        QVERIFY(scale[1].contains(QStringLiteral("DiFluidR2")));

        // The cap counts MATCHES: a cap of 2 counting scanned lines would stop at the
        // unmarked and DiFluid lines and return one.
        QCOMPARE(logger.sessionSnapshotReader({QStringLiteral("[Scale]"), QStringLiteral("[Refractometer]")},
                                             QStringLiteral("INFO"), 2, FileBundlePolicy::MaximumLogBytes)().lines, scale);
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

        const auto infoUp = logger.sessionSnapshotReader({QStringLiteral("[Scale]")},
                                                        QStringLiteral("INFO"), 0, FileBundlePolicy::MaximumLogBytes)().lines;
        QCOMPARE(infoUp.size(), 2);
        QVERIFY(!infoUp.join(QChar('\n')).contains(QStringLiteral("weight frame")));

        // No threshold means every level, DEBUG included.
        QCOMPARE(logger.sessionSnapshotReader({QStringLiteral("[Scale]")}, QString(), 0, FileBundlePolicy::MaximumLogBytes)().lines.size(), 3);
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
        const auto lines = logger.sessionSnapshotReader({QStringLiteral("[Scale]")},
                                                        QStringLiteral("INFO"), 0, FileBundlePolicy::MaximumLogBytes)().lines;
        QCOMPARE(lines.size(), 1);
        QVERIFY(lines[0].contains(QStringLiteral("current session")));
    }

    // The read runs backwards in 64 KB blocks, so a line straddling a block boundary
    // has to be reassembled, and a cap has to keep the NEWEST lines in their order.
    // ~160 KB of numbered lines crosses two boundaries.
    void sessionLines_readsBackwardsAcrossBlocksAndCapsToNewest()
    {
        QString content = QStringLiteral("[   0.000] INFO  [Scale][BLEManager] previous session\n"
                                         "========== SESSION START: 2026-01-01T09:00:00 ==========\n");
        constexpr int kLines = 2000;
        for (int i = 0; i < kLines; ++i)
            content += QStringLiteral("[%1.000] INFO  [Scale][BLEManager] line %2 padding padding padding padding\n")
                           .arg(i, 4).arg(i, 4, 10, QChar('0'));
        writeFile(logPath(), content);
        QVERIFY(content.size() > 2 * 64 * 1024);  // the reader's block size; keep the fixture crossing two

        WebDebugLogger logger(logPath());
        const auto all = logger.sessionSnapshotReader({QStringLiteral("[Scale]")}, QStringLiteral("INFO"), 0, FileBundlePolicy::MaximumLogBytes)().lines;
        QCOMPARE(all.size(), kLines);
        for (int i = 0; i < kLines; ++i)
            QVERIFY2(all[i].endsWith(QStringLiteral("line %1 padding padding padding padding")
                                         .arg(i, 4, 10, QChar('0'))),
                     qPrintable(all[i]));

        const auto tail = logger.sessionSnapshotReader({QStringLiteral("[Scale]")}, QStringLiteral("INFO"), 5, FileBundlePolicy::MaximumLogBytes)().lines;
        QCOMPARE(tail, all.mid(kLines - 5));
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
        QCOMPARE(logger.sessionSnapshotReader({QStringLiteral("[Scale]")},
                                              QStringLiteral("INFO"), 0, FileBundlePolicy::MaximumLogBytes)().lines.size(), 2);
    }

    // The writer opens the log in Text mode, so on Windows its lines end in CRLF while the
    // backward reader opens it raw: without stripping the \r, backfilled lines would carry
    // one that lines arriving live do not. Written raw here so it is exercised everywhere.
    void sessionLines_stripsCarriageReturns()
    {
        QFile f(logPath());
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("========== SESSION START: 2026-01-01T09:00:00 ==========\r\n"
                "[   0.100] INFO  [Scale][BLEManager] crlf line\r\n");
        f.close();

        WebDebugLogger logger(logPath());
        QCOMPARE(logger.sessionSnapshotReader({QStringLiteral("[Scale]")}, QStringLiteral("INFO"), 0, FileBundlePolicy::MaximumLogBytes)().lines,
                 QStringList{QStringLiteral("[   0.100] INFO  [Scale][BLEManager] crlf line")});
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
        QVERIFY(logger.sessionSnapshotReader({}, QStringLiteral("INFO"), 0, FileBundlePolicy::MaximumLogBytes)().lines.isEmpty());
    }

    // Markers are case-SENSITIVE substrings. "[scale]" is not a marker, and
    // matching it would only ever be a false positive on prose.
    void lineMatches_markersAreCaseSensitive()
    {
        const QString line = QStringLiteral("[   0.100] INFO  [Scale][BLEManager] a line");
        QVERIFY(McpLogFilter::lineMatches(line, {QStringLiteral("[Scale]")}, QStringLiteral("INFO")));
        QVERIFY(!McpLogFilter::lineMatches(line, {QStringLiteral("[scale]")}, QStringLiteral("INFO")));
    }

    void lineMatches_purePredicatePreservesSeverityRules()
    {
        const QStringList markers{QStringLiteral("[Scale]")};
        const QString debugLine = QStringLiteral("[   0.100] DEBUG [Scale] frame");
        const QString infoLine = QStringLiteral("[   0.200] INFO  [Scale] connected");
        const QString unmarkedLine = QStringLiteral("[   0.300] INFO  [DE1] connected");

        QVERIFY(!McpLogFilter::lineMatches(debugLine, markers, QStringLiteral("INFO")));
        QVERIFY(McpLogFilter::lineMatches(infoLine, markers, QStringLiteral("INFO")));
        QVERIFY(McpLogFilter::lineMatches(debugLine, markers, QStringLiteral("")));
        QVERIFY(McpLogFilter::lineMatches(debugLine, markers, QStringLiteral("TRACE")));
        QVERIFY(!McpLogFilter::lineMatches(unmarkedLine, markers, QStringLiteral("")));
    }

    // The backfill and the live path must agree line-for-line: the view is built
    // from sessionSnapshotReader() and then grows via McpLogFilter::lineMatches(). If they can
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
            if (McpLogFilter::lineMatches(line, markers, minLevel)) viaPredicate.append(line);
        }
        QCOMPARE(viaPredicate, logger.sessionSnapshotReader(markers, minLevel, 0, FileBundlePolicy::MaximumLogBytes)().lines);
        // The scale INFO line and the refractometer WARN — not the DEBUG frame, not
        // the [DE1] line, not the unmarked one, and not the previous session's.
        QCOMPARE(viaPredicate.size(), 2);
        QVERIFY(!viaPredicate.join(QChar('\n')).contains(QStringLiteral("previous session")));
    }

    // ---- lineAppended() ----

    // One emission per captured line, carrying the right type, and none from inside
    // handleMessage() itself: the receivers are QML views, and an emit there runs JS
    // inside whatever frame logged (suspected in #2030). The type is a plain enum with
    // no Q_DECLARE_METATYPE (qlogging.h:30); QSignalSpy's capture shows it still converts.
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

        QCOMPARE(spy.count(), 0);
        QTRY_COMPARE(spy.count(), 2);
        QCOMPARE(spy[0][0].value<QtMsgType>(), QtInfoMsg);
        QVERIFY(spy[0][1].toString().contains(QStringLiteral("INFO")));
        QVERIFY(spy[0][1].toString().contains(QStringLiteral("[Scale][BLEManager] hello")));
        QCOMPARE(spy[1][0].value<QtMsgType>(), QtWarningMsg);
        QVERIFY(spy[1][1].toString().contains(QStringLiteral("WARN")));
    }

    // A view backfills, then drops deliveries at or below the snapshot's sequence.
    // So a line already in the snapshot must arrive at or below it, and a later
    // line above it — otherwise the view shows the first twice or loses the second.
    void lineAppended_sequenceSeparatesSnapshotFromLaterLines()
    {
        writeFile(logPath(), "========== SESSION START: 2026-10-09T09:00:00 ==========\n");
        WebDebugLogger logger(logPath());
        QSignalSpy spy(&logger, &WebDebugLogger::lineAppended);

        logger.handleMessage(QtInfoMsg, QStringLiteral("[Scale][BLEManager] before"));
        const auto snapshot = logger.sessionSnapshotReader(
            {QStringLiteral("[Scale]")}, QStringLiteral("INFO"), 0, FileBundlePolicy::MaximumLogBytes)();
        logger.handleMessage(QtInfoMsg, QStringLiteral("[Scale][BLEManager] after"));

        const QStringList lines = snapshot.lines;
        QCOMPARE(lines.size(), 1);
        QVERIFY(lines.first().contains(QStringLiteral("before")));

        QTRY_COMPARE(spy.count(), 2);
        const qint64 boundary = snapshot.sequence;
        QVERIFY(spy[0][2].toLongLong() <= boundary);
        QVERIFY(spy[1][2].toLongLong() > boundary);
        // Clear moves the boundary to writtenSequence(): everything delivered so far.
        QCOMPARE(logger.writtenSequence(), spy[1][2].toLongLong());
    }

    // A slot that logs must not queue another delivery, or every delivery would
    // post the next one forever. The line the slot logs must still be RECORDED —
    // only its delivery is suppressed.
    void lineAppended_slotThatLogsDoesNotRecurse()
    {
        WebDebugLogger logger(logPath());

        int calls = 0;
        connect(&logger, &WebDebugLogger::lineAppended, &logger,
                [&](QtMsgType, const QString&) {
                    ++calls;
                    // Re-enters handleMessage exactly as a real logging slot would.
                    if (calls < 100) {
                        logger.handleMessage(QtDebugMsg, QStringLiteral("[Scale] from the slot"));
                    }
                });

        logger.handleMessage(QtInfoMsg, QStringLiteral("[Scale][BLEManager] first"));

        // Several rounds, so an unguarded chain of deliveries would show up as
        // calls > 1. The nested line was buffered but not delivered.
        for (int round = 0; round < 5; ++round)
            QCoreApplication::sendPostedEvents(&logger);
        QCOMPARE(calls, 1);
        // ...and the nested line is still in the buffer, both lines present.
        const QStringList all = logger.getAllLines();
        QCOMPARE(all.size(), 2);
        QVERIFY(all[0].contains(QStringLiteral("first")));
        QVERIFY(all[1].contains(QStringLiteral("from the slot")));
    }

    // ---- CrashHandler::getDebugLogTail() ----------------------------------
    //
    // The tail's job is to say what the app was doing before it died. Crash-report
    // text in it is pure duplicate — the same text ships as crashLog — so these
    // blocks are stripped. Fixtures below use CrashHandler's own marker constants
    // rather than spelling them, so a respelt marker fails here instead of
    // silently disabling the strip in production.

    void debugLogTail_dropsTheCrashReportBlock()
    {
        writeDebugLog(
            "[   0.100] INFO  app line one\n"
            "[   0.200] INFO  app line two\n"
            "\n"
            + startMarker() + "\n"
            "Signal: 6 (SIGABRT (Abort))\n"
            "Backtrace (29 frames, module+offset):\n"
            "  #0: 0x6f925a7490\n"
            + endMarker() + "\n");

        const QString tail = CrashHandler::getDebugLogTail();

        QVERIFY(tail.contains(QStringLiteral("app line one")));
        QVERIFY(tail.contains(QStringLiteral("app line two")));
        QVERIFY(!tail.contains(QStringLiteral("SIGABRT")));
        QVERIFY(!tail.contains(QStringLiteral("Backtrace")));
        QVERIFY(!tail.contains(QStringLiteral("CRASH REPORT")));
    }

    // The startup re-log's real on-disk shape, which is NOT one record: main.cpp
    // emits "=== PREVIOUS CRASH DETECTED ===", then the report body as one
    // noquote record, then a standalone end marker. So the body's interior lines
    // carry no log prefix while the markers around it do, and the body brings its
    // own unprefixed end marker along. Matching anywhere in the line, rather than
    // at its start, is what makes that strippable.
    void debugLogTail_dropsAPrefixedCrashReportBlock()
    {
        writeDebugLog(
            "[   0.100] INFO  app line one\n"
            "[   0.200] WARN  " + startMarker() + "\n"
            "Signal: 11 (SIGSEGV (Segmentation fault))\n"
            + endMarker() + "\n"                                  // body's own, unprefixed
            "[   0.300] WARN  " + endMarker() + "\n"              // main.cpp's standalone
            "[   0.400] INFO  app line after\n");

        const QString tail = CrashHandler::getDebugLogTail();

        // No writeCrashLog() block to anchor on, and the report must say so.
        QVERIFY(tail.startsWith(QStringLiteral("(This crash's block is not in debug.log")));
        QVERIFY(tail.contains(QStringLiteral("app line one")));
        QVERIFY(tail.contains(QStringLiteral("app line after")));
        QVERIFY(!tail.contains(QStringLiteral("SIGSEGV")));
        QVERIFY(!tail.contains(QStringLiteral("CRASH REPORT")));
    }

    // A block that never closes must not swallow the narrative before it, nor put
    // its own text at the end of it. writeCrashLog()'s debug.log append can die
    // mid-block — it demangles from a signal handler on the heap that may have
    // caused the crash — and the narrative ends where that block begins.
    void debugLogTail_survivesAnUnterminatedBlock()
    {
        writeDebugLog(
            "[   0.100] INFO  app line one\n"
            + startMarker() + "\n"
            "Signal: 6 (SIGABRT (Abort))\n"
            "  #0: 0x6f925a7490\n");   // died here — no end marker

        const QString tail = CrashHandler::getDebugLogTail();

        QVERIFY(tail.contains(QStringLiteral("app line one")));
        QVERIFY(!tail.contains(QStringLiteral("SIGABRT")));
    }

    // WebDebugLogger::trimLogFile() keeps the TAIL of the file, so a trim can cut
    // a block's opening line away and leave its body and closer behind. Read
    // naively, that orphan body is exactly the report text this function exists
    // to remove — reappearing in the one situation that produces it, a debug.log
    // big enough to have been trimmed.
    //
    // Told apart from main.cpp's standalone closer by position, not by shape:
    // a trim cuts the head, so a headless body can only precede the first start
    // marker. The slot above covers the other side of that rule — get it wrong
    // and the redundant closer wipes the narrative before it.
    void debugLogTail_dropsABlockWhoseStartWasTrimmedAway()
    {
        writeDebugLog(
            "Backtrace (29 frames, module+offset):\n"          // orphaned body, start marker trimmed off
            "  #0: 0x6f925a7490\n"
            "Signal: 6 (SIGABRT (Abort))\n"
            + endMarker() + "\n"
            "[   0.400] INFO  app line after\n");

        const QString tail = CrashHandler::getDebugLogTail();

        QVERIFY(tail.contains(QStringLiteral("app line after")));
        QVERIFY(!tail.contains(QStringLiteral("SIGABRT")));
        QVERIFY(!tail.contains(QStringLiteral("Backtrace")));
    }

    // Crash-report text must be stripped before selection: left in, it takes the
    // last-entry slots the narrative needs (#1745's symptom), which the small
    // fixtures above never reach a budget to show.
    void debugLogTail_spendsItsBudgetOnNarrativeNotCrashText()
    {
        QString content;
        for (int i = 0; i < 60; ++i)
            content += QStringLiteral("[   0.%1] INFO  narrative line %2\n")
                           .arg(i, 3, 10, QLatin1Char('0')).arg(i);
        content += startMarker() + "\n";
        for (int i = 0; i < 40; ++i)
            content += QStringLiteral("  #%1: 0x6f925a7490\n").arg(i);
        content += endMarker() + "\n";
        writeDebugLog(content);

        const QString tail = CrashHandler::getDebugLogTail(600);

        QVERIFY(tail.size() <= 600);
        QVERIFY(tail.contains(QStringLiteral("narrative line 59")));
        QVERIFY(!tail.contains(QStringLiteral("0x6f925a7490")));
    }

    // The crashed run is the session holding writeCrashLog()'s own block, not the
    // last session: a launch that showed the report and closed before the user
    // answered leaves crash.log in place and adds a session that did not crash.
    void debugLogTail_isTheRunThatCrashed()
    {
        writeDebugLog(
            "========== SESSION START: 2026-09-13T08:00:00 ==========\n"
            "[   1.000] WARN  [Scale][BLEManager] an older run's warning\n"
            "\n"
            "========== SESSION START: 2026-09-13T09:00:00 ==========\n"
            "[   2.000] INFO  [Scale][BLEManager] the crashed run\n"
            "\n\n"
            + startMarker() + "\n"
            "Signal: 11 (SIGSEGV (Segmentation fault))\n"
            + endMarker() + "\n"
            "\n"
            "========== SESSION START: 2026-09-13T10:00:00 ==========\n"
            "[   0.500] WARN  [App][main] " + startMarker() + "\n"
            "[   0.500] WARN  [App][main] " + endMarker() + "\n"
            "[   0.600] INFO  [App][main] the launch that showed the report\n");

        const QString tail = CrashHandler::getDebugLogTail();

        QVERIFY(tail.startsWith(QStringLiteral("========== SESSION START: 2026-09-13T09:00:00")));
        QVERIFY(tail.contains(QStringLiteral("the crashed run")));
        QVERIFY(!tail.contains(QStringLiteral("an older run's warning")));
        QVERIFY(!tail.contains(QStringLiteral("the launch that showed the report")));
    }

    // Warnings early, 400 DEBUG lines after: the selection must keep the session
    // marker, both ends and every level, count a merged run correctly, fold a
    // repeated warning into one line, keep a long warning's source location, and
    // mark where it cut.
    void crashNarrative_keepsEarlierWarningsPastTrailingChatter()
    {
        const QString marker = QStringLiteral("========== SESSION START: 2026-09-13T09:00:00 ==========");
        QStringList lines;
        lines << marker;
        lines << QStringLiteral("[   0.500] ERROR [DE1][BLE] link lost");
        lines << QStringLiteral("[   1.000] WARN  [Runtime][Unattributed] qrc:/qml/Page.qml:12: TypeError: %1 {source=qml/Page.qml:12}")
                     .arg(QString(400, QLatin1Char('x')));
        for (int i = 0; i < 3; ++i)
            lines << QStringLiteral("[   1.1%1] WARN  [Scale][BLEManager] retrying").arg(i);
        for (int i = 0; i < 5; ++i) {
            lines << QStringLiteral("[   2.%1] WARN  [Bluetooth][GattQueue] no answer within 3000 ms").arg(i);
            lines << QStringLiteral("[   2.%1] INFO  [Bluetooth][GattQueue] scale write failed").arg(i);
        }
        for (int i = 0; i < 400; ++i)
            lines << QStringLiteral("[   3.%1] DEBUG [Shot][ShotDataModel] chatter %1").arg(i, 3, 10, QLatin1Char('0'));

        const QString out = CrashHandler::selectCrashNarrative(lines, CrashHandler::kDebugLogTailBudget);

        QVERIFY(out.size() <= CrashHandler::kDebugLogTailBudget);
        QVERIFY(out.contains(marker));
        QVERIFY(out.contains(QStringLiteral("link lost")));
        QVERIFY(out.contains(QStringLiteral("TypeError: xxx")));
        QVERIFY(out.contains(QStringLiteral(" … {source=qml/Page.qml:12}")));
        QVERIFY(out.contains(QStringLiteral("retrying (x3)\n  … 8 lines omitted …")));
        QVERIFY(out.contains(QStringLiteral("chatter 399")));
        QVERIFY(out.contains(QStringLiteral("lines omitted")));
        QCOMPARE(out.count(QStringLiteral("no answer within 3000 ms")), 1);
        QVERIFY(out.contains(QStringLiteral("(+4 earlier)")));
    }

    void crashNarrative_keepsGcRetainingPathWithinBudget()
    {
        const QString path = QStringLiteral("[  12.000] ERROR [Memory][QmlGc] MARK_PATH leafToRoot=")
            + QString(450, QLatin1Char('x')) + QStringLiteral(" <- root rootStage=MarkWeakValues");
        QStringList lines {QStringLiteral("========== SESSION START: 2026-10-28T09:00:00 ==========")};
        for (int i = 0; i < 50; ++i)
            lines << QStringLiteral("[   1.%1] DEBUG [App][main] chatter %1").arg(i);
        lines << path << QStringLiteral("[  12.001] FATAL [Memory][QmlGc] POST_RESCAN_MISMATCH");
        const QString out = CrashHandler::selectCrashNarrative(lines, CrashHandler::kDebugLogTailBudget);
        QVERIFY(out.size() <= CrashHandler::kDebugLogTailBudget);
        QVERIFY(out.contains(path));
        QVERIFY(out.contains(QStringLiteral("POST_RESCAN_MISMATCH")));
    }

    // A Java exception reaches debug.log as one prefixed line per frame, all at
    // WARN. The exception line, not the outermost frames, must be what survives.
    void crashNarrative_javaTraceKeepsItsException()
    {
        QStringList lines;
        lines << QStringLiteral("[   1.000] WARN  [Runtime][Unattributed] java.lang.IllegalStateException: boom");
        for (int i = 0; i < 30; ++i)
            lines << QStringLiteral("[   1.000] WARN  [Runtime][Unattributed] \tat a.b.Frame.m%1(Frame.java:%1)").arg(i);
        lines << QStringLiteral("[   1.000] WARN  [Runtime][Unattributed] ");
        for (int i = 0; i < 40; ++i)
            lines << QStringLiteral("[   2.%1] DEBUG [Shot][ShotDataModel] chatter %1").arg(i, 3, 10, QLatin1Char('0'));

        const QString out = CrashHandler::selectCrashNarrative(lines, 1600);

        QVERIFY(out.contains(QStringLiteral("IllegalStateException: boom")));
        QVERIFY(out.contains(QStringLiteral("Frame.m0(")));
        QVERIFY(!out.contains(QStringLiteral("Frame.m29(")));
        QVERIFY(!out.contains(QStringLiteral("[Unattributed] \n")));  // the trace's empty last line
    }

    void tombstone_summaryPutsTheCrashFirst()
    {
        const QByteArray app = "/data/app/~~a/io.github.kulitorum.decenza_de1-b/base.apk!libDecenza_arm64-v8a.so";
        const QByteArray qt = "/data/app/~~a/io.github.kulitorum.decenza_de1-b/lib/arm64/libQt6Core_arm64-v8a.so";
        const QByteArray webview = "/data/app/~~c/com.google.android.trichromelibrary-d/base.apk!libmonochrome.so";
        const QByteArray libc = "/apex/com.android.runtime/lib64/bionic/libc.so";
        const QByteArray qtGui = "/data/app/~~a/io.github.kulitorum.decenza_de1-b/lib/arm64/libQt6Gui_arm64-v8a.so";
        QByteArray words;  // what x1's dump holds around the fault address 0x10
        for (quint64 w = 1; w <= 8; ++w)
            words.append(reinterpret_cast<const char*>(&w), sizeof w);
        const QByteArray signal = pb::u(1, 11) + pb::len(2, "SIGSEGV")
            + pb::u(3, quint64(qint64(-6))) + pb::len(4, "SI_TKILL") + pb::u(8, 1) + pb::u(9, 0x10);
        const QByteArray heap = pb::u(1, 0x1234) + pb::u(2, 64) + pb::u(3, 7)
            + pb::len(4, pb::frame(0x100, "malloc", libc)) + pb::len(4, pb::frame(0x200, "ShotModel::add", app, "aaaa"))
            + pb::u(5, 8) + pb::len(6, pb::frame(0x300, "ShotModel::~ShotModel", app, "aaaa"));
        const QByteArray cause = pb::len(1, "[GWP-ASan]: Use After Free, 0 bytes into a 64-byte allocation")
            + pb::len(2, pb::u(1, 0) + pb::u(2, 1) + pb::len(3, heap));
        const QByteArray head = pb::u(1, 1) + pb::len(2, "vendor/device:14/UP1A/1:user/release-keys")
            + pb::len(4, "2026-09-24 10:11:12.345+0200")
            + pb::u(5, 999) + pb::u(6, 42) + pb::len(10, signal)
            + pb::len(14, "Abort message line one\nline two")
            + pb::len(15, cause)
            + pb::len(17, pb::u(1, 0) + pb::u(2, 0x1000) + pb::u(3, 0x5000) + pb::u(4, 1) + pb::len(7, qtGui))
            + pb::len(17, QByteArray(5000, 'm'))  // a mapping of unknown fields matches no address
            + pb::fixed64(99);                    // an unknown field of another wire type
        const QByteArray crashingThread = pb::threadEntry(42, "QSGRenderThread",
            {pb::frame(0x500, "QSGRenderer::render", qt, "bbbb"), pb::frame(0x600, "Decenza::draw", app, "aaaa")},
            pb::len(3, pb::len(1, "x1") + pb::u(2, 0x10))
                + pb::len(3, pb::len(1, "x2") + pb::u(2, 0xb400000000000020ULL))  // a top-byte-tagged pointer
                + pb::len(5, pb::len(1, "x1") + pb::len(2, qtGui) + pb::u(3, 0) + pb::len(4, words)));
        const QByteArray proto = head
            + pb::threadEntry(43, "Binder:1", {pb::frame(0x10, "ioctl", libc)})
            + pb::threadEntry(45, "Chrome_IO", {pb::frame(0x30, "base::Run", webview, "cccc")})
            + pb::threadEntry(44, "BleWorker", {pb::frame(0x20, "futex", libc), pb::frame(0x400, "QThread::run", qt, "bbbb")})
            + crashingThread;

        const CrashHandler::TombstoneSummary summary = CrashHandler::summarizeTombstone(proto, 3500);
        const QString& out = summary.text;

        QVERIFY2(!out.contains(QStringLiteral("could not be parsed")), qPrintable(out));
        QVERIFY(summary.hasWholeCrashingThread);
        QCOMPARE(summary.tid, qint64(42));
        QCOMPARE(summary.signal, 11);
        QVERIFY(out.contains(QStringLiteral("Time: 2026-09-24 10:11:12.345+0200")));
        QVERIFY(out.contains(QStringLiteral("Signal 11 (SIGSEGV), code -6 (SI_TKILL), fault addr 0x10")));
        // The mapping offset is added, so the address resolves against the file offline.
        QVERIFY2(out.contains(QStringLiteral("Fault addr is in libQt6Gui_arm64-v8a.so+0x5010 r--")), qPrintable(out));
        QVERIFY(out.contains(QStringLiteral("Memory from 0x0 (x1's dump, fault word bracketed): 0000000000000001 "
                                            "0000000000000002 [0000000000000003] 0000000000000004 "
                                            "0000000000000005 0000000000000006\n")));
        QVERIFY2(out.contains(QStringLiteral(
                     "Crashing thread registers:\n"
                     "    into files: x0 libQt6Gui_arm64-v8a.so+0x5001 r--, x1 libQt6Gui_arm64-v8a.so+0x5010 r--, "
                     "x2 libQt6Gui_arm64-v8a.so+0x5020 r--\n"
                     "    x0=1 x1=10 x2=b400000000000020\n")),
                 qPrintable(out));
        QVERIFY(out.contains(QStringLiteral("Abort message: Abort message line one\n    line two")));
        QVERIFY(out.contains(QStringLiteral("#01 libDecenza_arm64-v8a.so+0x200 ShotModel::add+16")));
        QVERIFY(out.contains(QStringLiteral("libDecenza_arm64-v8a.so=aaaa")));
        QVERIFY(out.contains(QStringLiteral("44 \"BleWorker\": #01 libQt6Core_arm64-v8a.so+0x400")));
        QVERIFY(!out.contains(QStringLiteral("Binder:1")));   // no frame in the app's libraries
        QVERIFY(!out.contains(QStringLiteral("Chrome_IO")));  // under /data/app, but not ours

        const QStringList order = {QStringLiteral("Signal 11"), QStringLiteral("Fault addr is in"),
                                   QStringLiteral("Memory from"), QStringLiteral("Abort message"),
                                   QStringLiteral("Cause: [GWP-ASan]"),
                                   QStringLiteral("Allocated by thread 7"), QStringLiteral("Freed by thread 8"),
                                   QStringLiteral("Crashing thread 42 \"QSGRenderThread\""),
                                   QStringLiteral("Build IDs"), QStringLiteral("Fingerprint"),
                                   QStringLiteral("Crashing thread registers"), QStringLiteral("Other threads")};
        qsizetype at = -1;
        for (const QString& part : order) {
            const qsizetype next = out.indexOf(part);
            QVERIFY2(next > at, qPrintable(part));
            at = next;
        }

        // An abort message keeps its head and its tail, where ART puts a
        // reference-table dump's Summary.
        const QString longAbort = QStringLiteral("JNI ERROR (app bug): global reference table overflow")
            + QString(2000, QLatin1Char('.')) + QStringLiteral("Summary: 51200 of java.lang.Class");
        const QString abortOut = CrashHandler::summarizeTombstone(pb::len(14, longAbort.toUtf8()), 3500).text;
        QVERIFY(abortOut.contains(QStringLiteral("JNI ERROR (app bug)")));
        QVERIFY(abortOut.contains(QStringLiteral("Summary: 51200 of java.lang.Class")));

        // Within budget, with the cut said rather than silent. Other threads and
        // GWP-ASan stacks give way; the crashing thread does not.
        QByteArray crowded = proto;
        for (int i = 0; i < 200; ++i)
            crowded += pb::threadEntry(1000 + i, "Worker", {pb::frame(0x700, "QEventLoop::exec", qt)});
        const CrashHandler::TombstoneSummary cut = CrashHandler::summarizeTombstone(crowded, 3500);
        QVERIFY(cut.text.size() <= 3500);
        QVERIFY(cut.hasWholeCrashingThread);
        QVERIFY(cut.text.endsWith(QStringLiteral("(tombstone summary cut at 3500 chars)\n")));

        QList<QByteArray> deep;
        for (int i = 0; i < 30; ++i)
            deep << pb::frame(0x1000 + i, "QtPrivate::QSlotObjectBase::call(QObject*, void**, some long template tail)", qt, "bbbb");
        QByteArray heavyHeap = pb::u(3, 7) + pb::u(5, 8);
        for (int i = 0; i < 12; ++i)
            heavyHeap += pb::len(4, deep[i]) + pb::len(6, deep[i]);
        const QByteArray heavy = pb::u(6, 42) + pb::len(10, signal)
            + pb::len(15, pb::len(1, "[GWP-ASan]: Use After Free") + pb::len(2, pb::len(3, heavyHeap)))
            + pb::len(16, pb::u(1, 42) + pb::len(2, [&] {
                  QByteArray t = pb::u(1, 42);
                  for (const auto& f : deep.mid(0, 24))  // a Qt event-loop depth, ~125 chars a frame
                      t += pb::len(4, f);
                  return t;
              }()));
        // The free stack is in nothing else the report carries; the crashing
        // thread gives way instead, and so keeps the handler's backtrace.
        const CrashHandler::TombstoneSummary heavyOut = CrashHandler::summarizeTombstone(heavy, 3500);
        QVERIFY(heavyOut.text.size() <= 3500);
        const qsizetype freedAt = heavyOut.text.indexOf(QStringLiteral("Freed by thread 8:"));
        QVERIFY(freedAt > 0);
        const QString freed = heavyOut.text.mid(freedAt);
        QVERIFY2(freed.indexOf(QStringLiteral("#09 ")) > 0
                     && freed.indexOf(QStringLiteral("#09 ")) < freed.indexOf(QStringLiteral("Crashing thread")),
                 qPrintable(heavyOut.text));
        QVERIFY(!heavyOut.hasWholeCrashingThread);

        // The crashing thread is whole only if every frame is there.
        QList<QByteArray> tooDeep = deep + deep;
        QByteArray tooDeepThread = pb::u(1, 42);
        for (const auto& f : tooDeep)
            tooDeepThread += pb::len(4, f);
        QVERIFY(!CrashHandler::summarizeTombstone(pb::u(6, 42) + pb::len(16, pb::u(1, 42) + pb::len(2, tooDeepThread)), 99999)
                     .hasWholeCrashingThread);
        QVERIFY(!CrashHandler::summarizeTombstone(pb::u(6, 7) + crashingThread, 3500).hasWholeCrashingThread);
        // ...and only if the unwinder left no note that it stopped short.
        const CrashHandler::TombstoneSummary noted = CrashHandler::summarizeTombstone(
            pb::u(6, 42) + pb::len(16, pb::u(1, 42) + pb::len(2, pb::u(1, 42) + pb::len(4, deep[0])
                                                              + pb::len(7, "Unwind stopped: max frames"))), 3500);
        QVERIFY(noted.text.contains(QStringLiteral("(unwinder: Unwind stopped: max frames)")));
        QVERIFY(!noted.hasWholeCrashingThread);

        // Ahead of the handler's own ART capture, so a head-anchored slice keeps it.
        const QString artHeading = QString::fromLatin1(CrashHandler::kArtCaptureHeading);
        const QString btHeading = QString::fromLatin1(CrashHandler::kBacktraceHeading);
        const QString log = QStringLiteral("Signal: 6\n\n") + artHeading + QStringLiteral("\nx\n\n")
                          + btHeading + QStringLiteral("1 frames):\n");
        const QString merged = CrashHandler::insertTombstoneSection(log, out);
        QVERIFY(merged.indexOf(QStringLiteral("Android tombstone")) < merged.indexOf(artHeading));

        // A crash log that would overrun the server's slice gives up its own
        // backtrace, which duplicates the tombstone, and nothing after it.
        const QString fullLog = QStringLiteral("Signal: 11 (SIGSEGV)\nTid: 42\n\n%1\n%2\n\n%3 30 frames):\n%4\n\n%5\nlast line\n\n%6\n")
            .arg(artHeading, QString(4000, QLatin1Char('a')), btHeading, QString(3500, QLatin1Char('b')),
                 QString::fromLatin1(CrashHandler::kLogcatTailHeading), QString::fromLatin1(CrashHandler::kReportEnd));
        QCOMPARE(CrashHandler::insertTombstoneSummary(log, summary), merged);  // under budget: untouched
        QVERIFY(fullLog.size() + cut.text.size() > CrashHandler::kCrashLogBudget);
        const QString fitted = CrashHandler::insertTombstoneSummary(fullLog, cut);
        QVERIFY(fitted.size() <= CrashHandler::kCrashLogBudget);
        QVERIFY(fitted.contains(QStringLiteral("Backtrace: omitted to fit the report")));
        QVERIFY(!fitted.contains(QString(100, QLatin1Char('b'))));
        QVERIFY(fitted.contains(QString(4000, QLatin1Char('a'))));
        QVERIFY(fitted.contains(QString::fromLatin1(CrashHandler::kLogcatTailHeading) + QStringLiteral("\nlast line")));
        QVERIFY(fitted.endsWith(QString::fromLatin1(CrashHandler::kReportEnd) + QLatin1Char('\n')));

        // ...but only when the summary has the stack it claims to replace.
        const CrashHandler::TombstoneSummary noStack{cut.text, false, 42, 11};
        QCOMPARE(CrashHandler::insertTombstoneSummary(fullLog, noStack),
                 CrashHandler::insertTombstoneSection(fullLog, noStack.text));

        // ...of this crash. A second fault while the report was written is the
        // one debuggerd saw, and is said to be.
        for (const QString& other : {QString(fullLog).replace(QStringLiteral("Tid: 42"), QStringLiteral("Tid: 51")),
                                     QString(fullLog).replace(QStringLiteral("Signal: 11"), QStringLiteral("Signal: 6"))}) {
            const QString labelled = CrashHandler::insertTombstoneSummary(other, cut);
            QVERIFY2(labelled.contains(QStringLiteral("(NOT the crash above: signal 11 on thread 42 here")),
                     qPrintable(labelled.left(400)));
            QVERIFY(labelled.contains(QString(3500, QLatin1Char('b'))));
        }
    }

    // Qt's own text (qv4mm.cpp:800) becomes a [Memory] WARN, so a subsystem scan at
    // minLevel WARN finds it, and one bug's whole missed subtree cannot flood the log.
    void gcCrossValidation_missIsAMemoryWarning()
    {
        const QString qt = QStringLiteral("Cross Validation Error on chunk 3 on bitmap piece 1 and bit 40 "
                                          "With type \"FunctionObject\"");
        QVERIFY(!BtLogFilter::gcCrossValidationLine(QStringLiteral("Cross Validation passed"), 1));
        const auto first = BtLogFilter::gcCrossValidationLine(qt, 1);
        QVERIFY(first);
        QVERIFY(BtLogFilter::gcCrossValidationLine(qt, BtLogFilter::kMaxGcMissLines)->contains(qt));
        const auto capped = BtLogFilter::gcCrossValidationLine(qt, BtLogFilter::kMaxGcMissLines + 1);
        QVERIFY(capped && !capped->contains(qt));
        QVERIFY(!BtLogFilter::gcCrossValidationLine(qt, BtLogFilter::kMaxGcMissLines + 2));

        writeFile(logPath(), {});
        WebDebugLogger logger(logPath());
        logger.handleMessage(QtWarningMsg, *first);
        QFile f(logPath());
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString written = QString::fromUtf8(f.readAll());
        QVERIFY2(written.contains(QStringLiteral("WARN  [Memory][QmlGc] Incremental GC mark differs from full mark after stack rescan")),
                 qPrintable(written));
        QVERIFY(written.contains(qt));
    }

    void gcCrossValidation_reportsLaterBatchesAndPageAtFirstMiss()
    {
        const auto previous = qInstallMessageHandler(gcTestHandler);
        BtLogFilter::install();
        BtLogFilter::setGcPageStack(QStringLiteral("idlePage > postShotReviewPage"));
        const QString qt = QStringLiteral("Cross Validation Error on chunk 3 on bitmap piece 1 and bit 40 With type \"Function\"");
        for (int i = 0; i < 25; ++i)
            qDebug().noquote() << qt;
        BtLogFilter::setGcPageStack(QStringLiteral("idlePage"));
        QCoreApplication::processEvents();
        const QString first = gcTestMessages.join(QLatin1Char('\n'));
        gcTestMessages.clear();
        qDebug().noquote() << qt;
        QCoreApplication::processEvents();
        const QString second = gcTestMessages.join(QLatin1Char('\n'));
        qInstallMessageHandler(previous);
        gcTestMessages.clear();

        QVERIFY(first.contains(QStringLiteral("pages=idlePage > postShotReviewPage")));
        QVERIFY(first.contains(QStringLiteral("differences=25")));
        QVERIFY(first.contains(QStringLiteral("Function")));
        QVERIFY(second.contains(QStringLiteral("pages=idlePage")));
        QVERIFY(second.contains(QStringLiteral("differences=1")));
    }

    // It is read from a file the system hands over; nothing about it is trusted.
    void tombstone_malformedInputIsReportedNotTrusted()
    {
        const QByteArray head = pb::len(2, "fingerprint") + pb::u(6, 42);
        const QByteArray whole = head
            + pb::threadEntry(42, "main", {pb::frame(0x10, "f", "/data/app/x/libDecenza_arm64-v8a.so")});
        // Every cut inside the thread entry; a cut between fields is a valid message.
        for (qsizetype n = head.size() + 1; n < whole.size(); ++n) {
            const CrashHandler::TombstoneSummary out = CrashHandler::summarizeTombstone(whole.left(n), 3500);
            QVERIFY2(out.text.contains(QStringLiteral("could not be parsed")), qPrintable(QString::number(n)));
            QVERIFY(out.text.contains(QStringLiteral("Fingerprint: fingerprint")));  // what was read before it
            QVERIFY(!out.hasWholeCrashingThread);
            // Nothing claimed about what the parse never reached.
            QVERIFY(out.text.contains(QStringLiteral("(the crashing thread was not read before the parse failed)")));
            QVERIFY(!out.text.contains(QStringLiteral("(the tombstone has no threads)")));
            QVERIFY(out.text.contains(QStringLiteral("(no signal info read before the parse failed)")));
        }
        QVERIFY(CrashHandler::summarizeTombstone(QByteArray(64, char(0xff)), 3500)
                    .text.contains(QStringLiteral("could not be parsed")));
        const QString empty = CrashHandler::summarizeTombstone(QByteArray(), 3500).text;
        QVERIFY(empty.contains(QStringLiteral("(no signal info in the tombstone)")));
        QVERIFY(empty.contains(QStringLiteral("(the tombstone has no threads)")));
    }

#ifdef Q_OS_MACOS
    // A frame must name its image and give the unslid address, since atos and
    // llvm-symbolizer read the dSYM, not the process. Expected from the header's
    // own __TEXT, which starts at the header; it cannot catch a missing slide when
    // the slide is 0 (lldb disables ASLR by default).
    void crashBacktrace_addressIsInTheImageFile()
    {
        char out[256];
        CrashHandler::describeCodeAddress(reinterpret_cast<void*>(&codeInThisBinary), out, sizeof(out));
        const QString described = QString::fromLatin1(out);
        const qsizetype space = described.lastIndexOf(QLatin1Char(' '));

        QCOMPARE(described.left(space), QFileInfo(QCoreApplication::applicationFilePath()).fileName());
        bool ok = false;
        const quint64 fileAddress = described.mid(space + 1).toULongLong(&ok, 16);
        QVERIFY(ok);
        const segment_command_64* text = nullptr;
        auto* cmd = reinterpret_cast<const load_command*>(&_mh_execute_header + 1);
        for (uint32_t c = 0; c < _mh_execute_header.ncmds && !text; ++c) {
            if (cmd->cmd == LC_SEGMENT_64
                && qstrcmp(reinterpret_cast<const segment_command_64*>(cmd)->segname, "__TEXT") == 0)
                text = reinterpret_cast<const segment_command_64*>(cmd);
            cmd = reinterpret_cast<const load_command*>(reinterpret_cast<const char*>(cmd) + cmd->cmdsize);
        }
        QVERIFY(text);
        QCOMPARE(fileAddress, text->vmaddr + (quintptr(&codeInThisBinary) - quintptr(&_mh_execute_header)));

        // Just below the header is the executable's __PAGEZERO, which maps nothing.
        CrashHandler::describeCodeAddress(
            reinterpret_cast<void*>(quintptr(&_mh_execute_header) - 0x1000), out, sizeof(out));
        QVERIFY(QByteArray(out).contains("(not in a loaded image)"));
    }
#endif
};

QTEST_GUILESS_MAIN(tst_WebDebugLogger)
#include "tst_webdebuglogger.moc"
