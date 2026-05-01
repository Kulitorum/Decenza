// Qt Test wrapper around shot_eval --validate so Qt Creator's Tests panel
// surfaces the corpus regression alongside the other unit tests. Qt Creator
// auto-discovers Q_OBJECT-derived test binaries; the bare add_test()
// CTest entry didn't get listed, so a Mac developer running "Run all tests"
// from Qt Creator would silently miss this.
//
// Implementation: spawn shot_eval as a subprocess and assert exit code 0.
// Output is forwarded to QFAIL on mismatch so Qt Creator's failure pane
// shows the offending corpus line.
//
// Equivalent CLI:
//   shot_eval --validate tests/data/shots/manifest.json
//
// CI continues to run the same check via CTest (the registered test name
// is preserved). This is purely about making it visible in Qt Creator.

#include <QtTest>
#include <QProcess>
#include <QFileInfo>
#include <QDir>

class TstShotCorpus : public QObject
{
    Q_OBJECT
private slots:
    void corpus_validates_against_manifest();
};

void TstShotCorpus::corpus_validates_against_manifest()
{
    // Resolve paths relative to the source tree, not the build dir, so the
    // test runs identically whether invoked by Qt Creator or by ctest.
    const QString sourceDir = QStringLiteral(TST_SHOTCORPUS_SOURCE_DIR);
    const QString manifestPath = sourceDir + QStringLiteral("/data/shots/manifest.json");
    const QString shotEvalPath = QStringLiteral(TST_SHOTCORPUS_SHOT_EVAL);

    QVERIFY2(QFileInfo::exists(manifestPath),
             qPrintable(QStringLiteral("manifest not found: ") + manifestPath));
    QVERIFY2(QFileInfo::exists(shotEvalPath),
             qPrintable(QStringLiteral("shot_eval binary not found: ") + shotEvalPath
                + QStringLiteral(" — build the shot_eval target first")));

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(shotEvalPath,
               { QStringLiteral("--validate"), manifestPath });
    QVERIFY2(proc.waitForStarted(),
             qPrintable(QStringLiteral("failed to start shot_eval: ")
                + proc.errorString()));
    QVERIFY2(proc.waitForFinished(60000),
             "shot_eval --validate timed out after 60s");

    const QByteArray output = proc.readAllStandardOutput();
    const int exitCode = proc.exitCode();

    if (exitCode != 0) {
        QFAIL(qPrintable(QStringLiteral("shot_eval --validate failed (exit %1):\n%2")
                            .arg(exitCode).arg(QString::fromUtf8(output))));
    }
}

QTEST_GUILESS_MAIN(TstShotCorpus)
#include "tst_shotcorpus.moc"
