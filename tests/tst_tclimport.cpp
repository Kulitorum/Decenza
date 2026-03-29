#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>

#include "profile/profile.h"
#include "profile/profileframe.h"

// Test TCL profile import by loading every de1app .tcl profile through
// Profile::loadFromTclString(), verifying parsing succeeds, and round-tripping
// through JSON (toJson → fromJson) to catch serialization fidelity issues.
//
// Expected behavior derived from de1app profile.tcl and de1app stock profiles.
// These are integration tests against REAL profile files, not hand-crafted strings.

static const QString DE1APP_PROFILES_DIR =
    QStringLiteral(DE1APP_PROFILES_PATH);

class tst_TclImport : public QObject {
    Q_OBJECT

private:
    static QString readFile(const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return QString();
        return QTextStream(&file).readAll();
    }

private slots:

    // ==========================================
    // Data-driven: one row per TCL profile
    // ==========================================

    void importTclProfile_data() {
        QTest::addColumn<QString>("filePath");
        QTest::addColumn<QString>("fileName");

        QDir dir(DE1APP_PROFILES_DIR);
        if (!dir.exists()) {
            QSKIP("de1app profiles dir not found — skipping TCL import tests");
        }

        QStringList tclFiles = dir.entryList({"*.tcl"}, QDir::Files, QDir::Name);
        if (tclFiles.isEmpty()) {
            QSKIP("No .tcl profiles found in de1app profiles dir");
        }

        for (const QString& fileName : tclFiles) {
            QString fullPath = dir.absoluteFilePath(fileName);
            QTest::newRow(qPrintable(fileName)) << fullPath << fileName;
        }
    }

    void importTclProfile() {
        QFETCH(QString, filePath);
        QFETCH(QString, fileName);

        // Load TCL content
        QString content = readFile(filePath);
        QVERIFY2(!content.isEmpty(), qPrintable("Failed to read: " + filePath));

        // Parse through Decenza's TCL importer
        Profile profile = Profile::loadFromTclString(content);

        // Basic sanity: title must be non-empty
        QVERIFY2(!profile.title().isEmpty(),
                 qPrintable("Empty title after parsing: " + fileName));

        // Profile type must be valid
        QString type = profile.profileType();
        QVERIFY2(type == "settings_2a" || type == "settings_2b" ||
                 type == "settings_2c" || type == "settings_2c2" || type.isEmpty(),
                 qPrintable("Unexpected profileType '" + type + "' in: " + fileName));

        // Must have at least 1 frame (even simple profiles generate frames)
        QVERIFY2(profile.steps().size() > 0,
                 qPrintable("No frames after parsing: " + fileName));

        // Frame count must not exceed DE1 hardware limit
        QVERIFY2(profile.steps().size() <= Profile::MAX_FRAMES,
                 qPrintable(QString("Too many frames (%1 > %2) in: %3")
                            .arg(profile.steps().size()).arg(Profile::MAX_FRAMES).arg(fileName)));

        // preinfuseFrameCount must be in valid range
        QVERIFY2(profile.preinfuseFrameCount() >= 0 &&
                 profile.preinfuseFrameCount() <= profile.steps().size(),
                 qPrintable(QString("preinfuseFrameCount=%1 out of range [0,%2] in: %3")
                            .arg(profile.preinfuseFrameCount()).arg(profile.steps().size()).arg(fileName)));

        // Each frame must have valid properties
        for (qsizetype i = 0; i < profile.steps().size(); ++i) {
            const ProfileFrame& frame = profile.steps()[i];
            QVERIFY2(frame.pump == "pressure" || frame.pump == "flow",
                     qPrintable(QString("Invalid pump '%1' in frame %2 of: %3")
                                .arg(frame.pump).arg(i).arg(fileName)));
            QVERIFY2(frame.sensor == "coffee" || frame.sensor == "water",
                     qPrintable(QString("Invalid sensor '%1' in frame %2 of: %3")
                                .arg(frame.sensor).arg(i).arg(fileName)));
            QVERIFY2(frame.transition == "fast" || frame.transition == "smooth",
                     qPrintable(QString("Invalid transition '%1' in frame %2 of: %3")
                                .arg(frame.transition).arg(i).arg(fileName)));
            QVERIFY2(frame.temperature >= 0 && frame.temperature <= 120,
                     qPrintable(QString("Temperature %1 out of range in frame %2 of: %3")
                                .arg(frame.temperature).arg(i).arg(fileName)));
            QVERIFY2(frame.seconds >= 0,
                     qPrintable(QString("Negative seconds %1 in frame %2 of: %3")
                                .arg(frame.seconds).arg(i).arg(fileName)));
        }
    }

    // ==========================================
    // JSON round-trip: TCL → JSON → parse → compare
    // ==========================================

    void jsonRoundTrip_data() {
        QTest::addColumn<QString>("filePath");
        QTest::addColumn<QString>("fileName");

        QDir dir(DE1APP_PROFILES_DIR);
        if (!dir.exists()) {
            QSKIP("de1app profiles dir not found — skipping round-trip tests");
        }

        QStringList tclFiles = dir.entryList({"*.tcl"}, QDir::Files, QDir::Name);
        for (const QString& fileName : tclFiles) {
            QTest::newRow(qPrintable(fileName)) << dir.absoluteFilePath(fileName) << fileName;
        }
    }

    void jsonRoundTrip() {
        QFETCH(QString, filePath);
        QFETCH(QString, fileName);

        QString content = readFile(filePath);
        QVERIFY(!content.isEmpty());

        // TCL → Profile
        Profile original = Profile::loadFromTclString(content);
        QVERIFY(!original.title().isEmpty());

        // Profile → JSON → Profile
        QJsonDocument json = original.toJson();
        Profile roundTripped = Profile::fromJson(json);

        // Compare key fields
        QCOMPARE(roundTripped.title(), original.title());
        QCOMPARE(roundTripped.profileType(), original.profileType());
        QCOMPARE(roundTripped.targetWeight(), original.targetWeight());
        QCOMPARE(roundTripped.targetVolume(), original.targetVolume());
        QCOMPARE(roundTripped.steps().size(), original.steps().size());
        QCOMPARE(roundTripped.preinfuseFrameCount(), original.preinfuseFrameCount());

        // Compare each frame
        for (qsizetype i = 0; i < original.steps().size(); ++i) {
            const ProfileFrame& orig = original.steps()[i];
            const ProfileFrame& rt = roundTripped.steps()[i];

            QVERIFY2(orig.pump == rt.pump,
                     qPrintable(QString("Frame %1 pump mismatch in %2: '%3' vs '%4'")
                                .arg(i).arg(fileName).arg(orig.pump).arg(rt.pump)));
            QVERIFY2(qAbs(orig.pressure - rt.pressure) < 0.01,
                     qPrintable(QString("Frame %1 pressure mismatch in %2: %3 vs %4")
                                .arg(i).arg(fileName).arg(orig.pressure).arg(rt.pressure)));
            QVERIFY2(qAbs(orig.flow - rt.flow) < 0.01,
                     qPrintable(QString("Frame %1 flow mismatch in %2: %3 vs %4")
                                .arg(i).arg(fileName).arg(orig.flow).arg(rt.flow)));
            QVERIFY2(qAbs(orig.temperature - rt.temperature) < 0.01,
                     qPrintable(QString("Frame %1 temperature mismatch in %2: %3 vs %4")
                                .arg(i).arg(fileName).arg(orig.temperature).arg(rt.temperature)));
            QVERIFY2(qAbs(orig.seconds - rt.seconds) < 0.01,
                     qPrintable(QString("Frame %1 seconds mismatch in %2: %3 vs %4")
                                .arg(i).arg(fileName).arg(orig.seconds).arg(rt.seconds)));
            QVERIFY2(orig.exitIf == rt.exitIf,
                     qPrintable(QString("Frame %1 exitIf mismatch in %2: %3 vs %4")
                                .arg(i).arg(fileName).arg(orig.exitIf).arg(rt.exitIf)));
            QVERIFY2(qAbs(orig.exitWeight - rt.exitWeight) < 0.01,
                     qPrintable(QString("Frame %1 exitWeight mismatch in %2: %3 vs %4")
                                .arg(i).arg(fileName).arg(orig.exitWeight).arg(rt.exitWeight)));
            QVERIFY2(qAbs(orig.maxFlowOrPressure - rt.maxFlowOrPressure) < 0.01,
                     qPrintable(QString("Frame %1 maxFlowOrPressure mismatch in %2: %3 vs %4")
                                .arg(i).arg(fileName).arg(orig.maxFlowOrPressure).arg(rt.maxFlowOrPressure)));
        }
    }

    // ==========================================
    // Specific profile oracle tests
    // ==========================================

    void defaultProfileOracle() {
        // de1app default.tcl: simple pressure profile (settings_2a)
        QString content = readFile(DE1APP_PROFILES_DIR + "/default.tcl");
        if (content.isEmpty()) QSKIP("default.tcl not found");

        Profile p = Profile::loadFromTclString(content);
        QVERIFY(!p.title().isEmpty());
        QCOMPARE(p.profileType(), QString("settings_2a"));
        QVERIFY(p.steps().size() >= 2);  // Generates frames from scalar params
    }

    void bloomingEspressoOracle() {
        // Blooming espresso: advanced profile with multiple exit conditions
        QString content = readFile(DE1APP_PROFILES_DIR + "/Blooming espresso.tcl");
        if (content.isEmpty()) QSKIP("Blooming espresso.tcl not found");

        Profile p = Profile::loadFromTclString(content);
        QVERIFY(!p.title().isEmpty());
        QVERIFY(p.profileType().startsWith("settings_2c"));
        QVERIFY(p.steps().size() >= 4);
    }

    void simplePressureProfileOracle() {
        // Classic Italian espresso: settings_2a (simple pressure)
        QString content = readFile(DE1APP_PROFILES_DIR + "/Classic Italian espresso.tcl");
        if (content.isEmpty()) QSKIP("Classic Italian espresso.tcl not found");

        Profile p = Profile::loadFromTclString(content);
        QCOMPARE(p.profileType(), QString("settings_2a"));
        QVERIFY(p.steps().size() >= 2);
    }

    void aFlowProfileOracle() {
        // A-Flow default-medium: advanced profile stored with 6 frames in TCL
        // (de1app's A-Flow plugin dynamically adds 3 more via update_A-Flow,
        // but the TCL file only contains the base 6 frames from advanced_shot)
        QString content = readFile(DE1APP_PROFILES_DIR + "/A-Flow____default-medium.tcl");
        if (content.isEmpty()) QSKIP("A-Flow____default-medium.tcl not found");

        Profile p = Profile::loadFromTclString(content);
        QVERIFY(!p.title().isEmpty());
        QVERIFY(p.profileType().startsWith("settings_2c"));
        // TCL stores the base frames; A-Flow plugin adds more at runtime
        QVERIFY(p.steps().size() >= 5);
    }
};

QTEST_GUILESS_MAIN(tst_TclImport)
#include "tst_tclimport.moc"
