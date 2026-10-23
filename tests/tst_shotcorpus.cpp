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
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>

class TstShotCorpus : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }
    void corpus_validates_against_manifest();
    void legacy_settling_respects_beverage_type_data();
    void legacy_settling_respects_beverage_type();
};

void TstShotCorpus::legacy_settling_respects_beverage_type_data()
{
    QTest::addColumn<QString>("beverageType");
    QTest::addColumn<bool>("expectsOverRise");
    QTest::newRow("filter") << QStringLiteral("filter") << false;
    QTest::newRow("pourover") << QStringLiteral("pourover") << false;
    QTest::newRow("tea") << QStringLiteral("tea") << false;
    QTest::newRow("tea-portafilter") << QStringLiteral("tea_portafilter") << false;
    QTest::newRow("espresso") << QStringLiteral("espresso") << true;
    QTest::newRow("unknown-defaults-to-espresso") << QStringLiteral("unknown") << true;
}

void TstShotCorpus::legacy_settling_respects_beverage_type()
{
    QFETCH(QString, beverageType);
    QFETCH(bool, expectsOverRise);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString log = QStringLiteral("[SAW] Stop triggered: weight= 200 flow= 6\n"
                                 "[SAW] Starting settling (max 10s) - current weight: 200 g\n");
    for (double weight : {205.0, 210.0, 215.0, 219.0, 222.0})
        log += QStringLiteral("[SAW] Settling: \"%1\" g delta: \"1\" avg: \"%1\" drift: \"1\"\n").arg(weight);
    const QJsonObject shot{
        {QStringLiteral("profile"), QJsonObject{{QStringLiteral("beverage_type"), beverageType}}},
        {QStringLiteral("meta"), QJsonObject{{QStringLiteral("out"), 222.0}}},
        {QStringLiteral("app"), QJsonObject{{QStringLiteral("data"),
            QJsonObject{{QStringLiteral("debug_log"), log}}}}}};
    QFile file(dir.filePath(QStringLiteral("drain.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray json = QJsonDocument(shot).toJson();
    QCOMPARE(file.write(json), json.size());
    file.close();

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(QStringLiteral(TST_SHOTCORPUS_SHOT_EVAL), {QStringLiteral("--settling"), file.fileName()});
    QVERIFY(proc.waitForStarted());
    QVERIFY(proc.waitForFinished(60000));
    QCOMPARE(proc.exitStatus(), QProcess::NormalExit);
    QCOMPARE(proc.exitCode(), 0);
    const QString output = QString::fromUtf8(proc.readAllStandardOutput());
    QCOMPARE(output.contains(QStringLiteral("over-rise")), expectsOverRise);
    QCOMPARE(output.contains(QStringLiteral("clean settle")), !expectsOverRise);
}

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
