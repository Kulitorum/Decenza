#include <QCoreApplication>
#include <QJSEngine>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>
#include <QtTest>
#include <private/qv4engine_p.h>
#include <private/qv4mm_p.h>
#include "core/btlogfilter.h"
#include "core/gcdeferredabort.h"

static void testLog(QtMsgType, const QMessageLogContext&, const QString& message)
{
    fprintf(stderr, "%s\n", qPrintable(message));
    fflush(stderr);
}

// The child loads the test-only QtQml framework. Its test hooks are absent from
// the Android pre-release binary. No machine or Bluetooth device is involved.
class QtGcDiagnosticTests : public QObject {
    Q_OBJECT
private:
    static QByteArray runChild(const QByteArray &mode, QProcess::ExitStatus *status, int *code)
    {
        QProcess child;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("DYLD_FRAMEWORK_PATH"), QStringLiteral(DECENZA_TEST_QML_FRAMEWORK_DIR));
        env.insert(QStringLiteral("DECENZA_GC_TEST_MODE"),
                   QString::fromLatin1(mode == "deferred" ? QByteArray("mismatch") : mode));
        env.insert(QStringLiteral("QV4_MM_CROSS_VALIDATE_INCREMENTAL_GC"), QStringLiteral("1"));
        env.insert(QStringLiteral("QV4_GC_TIMELIMIT"), QStringLiteral("5"));
        child.setProcessEnvironment(env);
        child.setProcessChannelMode(QProcess::MergedChannels);
        QStringList args{QStringLiteral("--gc-child")};
        if (mode == "deferred") args.append(QStringLiteral("--abort-after-idle"));
        child.start(QCoreApplication::applicationFilePath(), args);
        if (!child.waitForStarted(10000) || !child.waitForFinished(30000)) {
            child.kill();
            child.waitForFinished();
            *status = QProcess::CrashExit;
            *code = -1;
            return QByteArrayLiteral("CHILD_DID_NOT_FINISH ") + child.readAll();
        }
        *status = child.exitStatus();
        *code = child.exitCode();
        return child.readAll();
    }

private slots:
    void normalAndRescannedMarksContinue()
    {
        for (const QByteArray mode : {QByteArrayLiteral("normal"), QByteArrayLiteral("rescan")}) {
            QProcess::ExitStatus status;
            int code;
            const QByteArray output = runChild(mode, &status, &code);
            QVERIFY2(status == QProcess::NormalExit && code == 0, output.constData());
            QVERIFY2(output.contains("GC_TEST_REACHED mode=" + mode), output.constData());
            QVERIFY2(!output.contains("POST_RESCAN_MISMATCH"), output.constData());
        }
    }

    void unrepairedMarkRecordsEvidenceAndContinues()
    {
        QProcess::ExitStatus status;
        int code;
        const QByteArray output = runChild(QByteArrayLiteral("mismatch"), &status, &code);
        QVERIFY2(status == QProcess::NormalExit && code == 0, output.constData());
        QVERIFY2(output.contains("GC_TEST_REACHED mode=mismatch"), output.constData());
        QVERIFY2(output.contains("POST_RESCAN_MISMATCH allocator="), output.constData());
        QVERIFY2(output.contains("MARK_PATH leafToRoot="), output.constData());
        QVERIFY2(output.contains("POST_RESCAN_MISMATCH differences="), output.constData());
        QVERIFY2(output.contains("DEFERRED_ABORT"), output.constData());
        QVERIFY2(output.contains("GC_TEST_CHILD_CONTINUED"), output.constData());
        QVERIFY2(output.contains("incomplete=0"), output.constData());
        QVERIFY2(!output.contains("rootStage=unknown"), output.constData());
        QVERIFY2(output.contains("root=Mark"), output.constData());
    }

    void mismatchWaitsForIdleBeforeAborting()
    {
        QProcess::ExitStatus status;
        int code;
        const QByteArray output = runChild(QByteArrayLiteral("deferred"), &status, &code);
        QVERIFY2(status == QProcess::CrashExit, output.constData());
        const auto pending = output.indexOf("GC_TEST_MISMATCH_DELIVERED");
        const auto active = output.indexOf("GC_TEST_ACTIVE_CONTINUED");
        const auto idle = output.indexOf("GC_TEST_IDLE");
        const auto aborted = output.indexOf("GC_TEST_ABORT ");
        QVERIFY2(pending >= 0 && active > pending && idle > active && aborted > idle, output.constData());
        QVERIFY2(output.contains("MARK_PATH leafToRoot="), output.constData());
        QVERIFY2(output.contains("GC_TEST_ABORT [Memory][QmlGc] DEFERRED_ABORT"), output.constData());
    }

    void tracingCannotRemoveValidatedMarks()
    {
        QProcess::ExitStatus status;
        int code;
        const QByteArray output = runChild(QByteArrayLiteral("trace-miss"), &status, &code);
        QVERIFY2(status == QProcess::NormalExit && code == 0, output.constData());
        QVERIFY2(output.contains("GC_TEST_REACHED mode=trace-miss"), output.constData());
        QVERIFY2(output.contains("GC_TEST_TRACE_MARK_PRESERVED"), output.constData());
        QVERIFY2(output.contains("GC_TEST_CHILD_CONTINUED"), output.constData());
    }


};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("--gc-child"))) {
        bool idle = false;
        bool mismatchDelivered = false;
        GcDeferredAbort::Coordinator guard([&] { return idle; }, [] {}, [](const QString& evidence) {
            qFatal("GC_TEST_ABORT %s", qPrintable(evidence));
        });
        if (app.arguments().contains(QStringLiteral("--abort-after-idle"))) {
            qInstallMessageHandler(testLog);
            BtLogFilter::install();
            BtLogFilter::setGcMismatchHandler([&](const QString& evidence) {
                mismatchDelivered = true;
                qInfo("GC_TEST_MISMATCH_DELIVERED");
                guard.mismatch(evidence);
            });
        }
        QJSEngine engine;
        const QJSValue root = engine.evaluate(QStringLiteral("({nested: {value: 42}})"));
        Q_UNUSED(root);
        for (int i = 0; i < 20; ++i) {
            engine.handle()->memoryManager->runGC();
            QCoreApplication::processEvents();
            if (app.arguments().contains(QStringLiteral("--abort-after-idle")) && mismatchDelivered) {
                qInfo("GC_TEST_ACTIVE_CONTINUED");
                QTimer::singleShot(100, &app, [&] {
                    idle = true;
                    qInfo("GC_TEST_IDLE");
                    guard.stateInfoReceived();
                });
                QTimer::singleShot(5000, &app, &QCoreApplication::quit);
                return app.exec() == 0 ? 2 : 3;
            }
        }
        qInfo("GC_TEST_CHILD_CONTINUED");
        return 0;
    }
    QtGcDiagnosticTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "tst_qtgc_diagnostics.moc"
