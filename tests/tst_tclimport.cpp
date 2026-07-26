#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include "profile/de1apptclfields.h"
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
    void init() { QTest::failOnWarning(); }

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

    // ==========================================
    // Compare TCL test profiles against built-in JSONs
    // Every TCL in tests/data/de1app_profiles must be identical to its built-in
    // ==========================================

    void compareWithBuiltin_data() {
        QTest::addColumn<QString>("tclPath");
        QTest::addColumn<QString>("fileName");

        QDir dir(DE1APP_PROFILES_DIR);
        if (!dir.exists())
            QSKIP("de1app profiles dir not found");

        for (const QString& f : dir.entryList({"*.tcl"}, QDir::Files, QDir::Name))
            QTest::newRow(qPrintable(f)) << dir.absoluteFilePath(f) << f;
    }

    void compareWithBuiltin() {
        QFETCH(QString, tclPath);
        QFETCH(QString, fileName);

        QFile f(tclPath);
        QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable("Cannot read: " + tclPath));
        Profile tcl = Profile::loadFromTclString(QTextStream(&f).readAll());
        QVERIFY2(!tcl.title().isEmpty(), qPrintable("Empty title: " + fileName));

        // Same mapping the app uses — a local copy would let the test look for a
        // different file than ProfileManager writes.
        const QString builtinPath = ":/profiles/" + Profile::titleToFilename(tcl.title()) + ".json";
        QVERIFY2(QFile::exists(builtinPath),
                 qPrintable("No built-in JSON for '" + tcl.title() + "' (tried " + builtinPath + ")"));

        Profile builtin = Profile::loadFromFile(builtinPath);
        QVERIFY2(builtin.isValid(), qPrintable("Invalid built-in JSON: " + builtinPath));

        // Same report profile_sync prints, from the same function — the tool and
        // the gate must not be able to disagree about what "different" means.
        const QString report = Profile::frameDiffReport(tcl, builtin);
        QVERIFY2(report.isEmpty(),
                 qPrintable("\n=== compareProfiles mismatch: " + tcl.title() + " ===\n" + report));
    }

    // ==========================================
    // The type-dependent field-selection rule (De1AppTcl)
    // ==========================================

    void dualSpelledFieldRule_data() {
        QTest::addColumn<QString>("canonical");
        QTest::addColumn<QString>("profileType");
        QTest::addColumn<QString>("expectedTclKey");

        // settings_2a/2b: de1app's pressure_to_advanced_list / flow_to_advanced_list
        // OVERWRITE the _advanced fields from their plain counterparts before the
        // v2 converter runs, so the plain spelling is authoritative.
        for (const QString& simple : {QStringLiteral("settings_2a"), QStringLiteral("settings_2b")}) {
            QTest::newRow(qPrintable(simple + "/target_weight"))
                << "target_weight" << simple << "final_desired_shot_weight";
            QTest::newRow(qPrintable(simple + "/target_volume"))
                << "target_volume" << simple << "final_desired_shot_volume";
            QTest::newRow(qPrintable(simple + "/max_pressure_range"))
                << "maximum_pressure_range_advanced" << simple << "maximum_pressure_range_default";
            QTest::newRow(qPrintable(simple + "/max_flow_range"))
                << "maximum_flow_range_advanced" << simple << "maximum_flow_range_default";
        }

        // settings_2c/2c2: settings_to_advanced_list does NOT overwrite, so the
        // _advanced spelling is authoritative.
        for (const QString& adv : {QStringLiteral("settings_2c"), QStringLiteral("settings_2c2")}) {
            QTest::newRow(qPrintable(adv + "/target_weight"))
                << "target_weight" << adv << "final_desired_shot_weight_advanced";
            QTest::newRow(qPrintable(adv + "/target_volume"))
                << "target_volume" << adv << "final_desired_shot_volume_advanced";
            QTest::newRow(qPrintable(adv + "/max_pressure_range"))
                << "maximum_pressure_range_advanced" << adv << "maximum_pressure_range_advanced";
            QTest::newRow(qPrintable(adv + "/max_flow_range"))
                << "maximum_flow_range_advanced" << adv << "maximum_flow_range_advanced";
        }

        // Single-spelled fields resolve to themselves on both branches.
        QTest::newRow("2a/espresso_pressure")
            << "espresso_pressure" << "settings_2a" << "espresso_pressure";
        QTest::newRow("2c/espresso_pressure")
            << "espresso_pressure" << "settings_2c" << "espresso_pressure";
    }

    void dualSpelledFieldRule() {
        QFETCH(QString, canonical);
        QFETCH(QString, profileType);
        QFETCH(QString, expectedTclKey);
        QCOMPARE(De1AppTcl::tclKeyFor(canonical, profileType), expectedTclKey);
    }

    void unknownCanonicalKeyResolvesToNothing() {
        // A caller asking for a field the map does not cover must get an empty
        // key, not a plausible-looking guess.
        QVERIFY(De1AppTcl::tclKeyFor("not_a_profile_field", "settings_2a").isEmpty());
    }

    void uncoveredKeysAreReported() {
        // The specific defect that once measured a 338-row drift as 4 rows: a
        // de1app key the field map does not mention must surface as uncompared,
        // never be silently dropped from the comparison.
        const QString tcl = QStringLiteral(
            "profile_title {Coverage probe}\n"
            "settings_profile_type settings_2a\n"
            "espresso_pressure 9.0\n"
            "some_future_de1app_field 3\n"
            "advanced_shot {}\n");

        const QStringList uncovered = De1AppTcl::uncoveredTclKeys(tcl);
        QVERIFY2(uncovered.contains("some_future_de1app_field"),
                 qPrintable("uncovered = " + uncovered.join(", ")));
        QVERIFY(!uncovered.contains("espresso_pressure"));    // compared
        QVERIFY(!uncovered.contains("settings_profile_type")); // declared non-scalar
        QVERIFY(!uncovered.contains("profile_title"));         // declared non-scalar
    }

    void profileNotesProseIsNotReadAsAScalar() {
        // extractValue() used to search the whole file and take the first hit in
        // FORM order (braced, then quoted, then bare) rather than position
        // order, so prose in profile_notes beat the real assignment. This exact
        // file imported with maximum_flow 6 — the machine got the note, not the
        // profile. profile_notes is free text and profiles arrive from
        // Visualizer and shared files, so it is reachable input.
        const QString tcl = QStringLiteral(
            "profile_title {Notes probe}\n"
            "settings_profile_type settings_2c\n"
            "profile_notes {I tested this at maximum_flow 6 and it was great}\n"
            "maximum_flow 2.5\n"
            "espresso_temperature 93.0\n"
            "advanced_shot {{exit_if 0 flow 2.0 temperature 93.0 name x pressure 6.0 "
            "sensor coffee pump flow seconds 25 volume 0}}\n");

        QCOMPARE(De1AppTcl::extractValue(tcl, "maximum_flow"), QStringLiteral("2.5"));
        QCOMPARE(Profile::loadFromTclString(tcl).maximumFlow(), 2.5);
    }

    void keysInsideBracedValuesAreNotProfileKeys() {
        // advanced_shot and a multi-line profile_notes both contain words that
        // would read as top-level assignments if the scan ignored brace depth.
        const QString tcl = QStringLiteral(
            "profile_title {Depth probe}\n"
            "profile_notes {first line\n"
            "temperature 93 looks like a key but is prose\n"
            "}\n"
            "settings_profile_type settings_2c\n");
        const QStringList keys = De1AppTcl::assignedTclKeys(tcl);
        QVERIFY(keys.contains("profile_title"));
        QVERIFY(keys.contains("settings_profile_type"));
        QVERIFY2(!keys.contains("temperature"), qPrintable("keys = " + keys.join(", ")));
    }

    // ==========================================
    // Per-scalar import fidelity
    //
    // Every value below is deliberately different from Profile's own default,
    // so a scalar the reader skips shows up as the default rather than passing
    // by coincidence. That is exactly how 338 mismatches went unnoticed:
    // espresso_pressure read 9.2 on all 23 affected profiles because 9.2 is the
    // fromJson default, not because any file said 9.2.
    // ==========================================

    void scalarFidelity_data() {
        QTest::addColumn<QString>("fileName");
        QTest::addColumn<QString>("field");
        QTest::addColumn<double>("expected");

        // settings_2a WITH a stored advanced_shot — the case the old gate skipped
        // entirely, because m_steps was non-empty.
        const QString a = QStringLiteral("Classic Italian espresso.tcl");
        QTest::newRow("2a/espresso_pressure")      << a << "espressoPressure"        << 9.0;   // default 9.2
        QTest::newRow("2a/preinfusion_time")       << a << "preinfusionTime"         << 8.0;   // default 5
        QTest::newRow("2a/espresso_hold_time")     << a << "espressoHoldTime"        << 35.0;  // default 10
        QTest::newRow("2a/espresso_decline_time")  << a << "espressoDeclineTime"     << 0.0;   // default 25
        QTest::newRow("2a/pressure_end")           << a << "pressureEnd"             << 6.0;   // default 4
        QTest::newRow("2a/preinfusion_flow_rate")  << a << "preinfusionFlowRate"     << 8.0;   // default 4
        QTest::newRow("2a/preinfusion_stop_press") << a << "preinfusionStopPressure" << 4.0;
        // Flow-editor scalars on a PRESSURE profile: de1app writes the whole
        // block on every profile, and reading them only in the settings_2b
        // branch lost them here.
        QTest::newRow("2a/flow_profile_hold")         << a << "flowProfileHold"        << 1.8;  // default 2.0
        QTest::newRow("2a/flow_profile_hold_time")    << a << "flowProfileHoldTime"    << 0.0;  // default 8
        QTest::newRow("2a/flow_profile_decline")      << a << "flowProfileDecline"     << 1.0;  // default 1.2
        QTest::newRow("2a/flow_profile_decline_time") << a << "flowProfileDeclineTime" << 23.0; // default 17
        QTest::newRow("2a/minimum_pressure")          << a << "minimumPressure"        << 6.0;
        // Dual-spelled: the plain spelling wins on 2a, though _advanced disagrees
        // (final_desired_shot_weight_advanced is 60, maximum_*_range_advanced 0.6).
        QTest::newRow("2a/target_weight")          << a << "targetWeight"                  << 36.0;
        QTest::newRow("2a/max_pressure_range_adv") << a << "maximumPressureRangeAdvanced"  << 0.9;
        QTest::newRow("2a/max_flow_range_adv")     << a << "maximumFlowRangeAdvanced"      << 1.0;
        QTest::newRow("2a/max_pressure_range_def") << a << "maximumPressureRangeDefault"   << 0.9;
        QTest::newRow("2a/max_flow_range_def")     << a << "maximumFlowRangeDefault"       << 1.0;

        // settings_2b, also carrying a stored advanced_shot.
        const QString b = QStringLiteral("Flow profile for milky drinks.tcl");
        QTest::newRow("2b/flow_profile_hold")      << b << "flowProfileHold"     << 1.2;  // default 2.0
        QTest::newRow("2b/flow_profile_decline")   << b << "flowProfileDecline"  << 1.0;  // default 1.2
        QTest::newRow("2b/espresso_pressure")      << b << "espressoPressure"    << 8.6;  // pressure scalar on a FLOW profile
        QTest::newRow("2b/preinfusion_time")       << b << "preinfusionTime"     << 20.0;
        QTest::newRow("2b/target_weight")          << b << "targetWeight"        << 36.0; // _advanced says 42
        QTest::newRow("2b/max_pressure_range_adv") << b << "maximumPressureRangeAdvanced" << 0.9;
        QTest::newRow("2b/max_flow_range_adv")     << b << "maximumFlowRangeAdvanced"     << 1.0;

        // settings_2c — the opposite branch of the same rule.
        const QString c = QStringLiteral("Blooming allonge.tcl");
        QTest::newRow("2c/flow_profile_decline")   << c << "flowProfileDecline" << 3.5;  // default 1.2
        QTest::newRow("2c/flow_profile_hold")      << c << "flowProfileHold"    << 4.5;  // default 2.0
        QTest::newRow("2c/espresso_pressure")      << c << "espressoPressure"   << 8.6;
        QTest::newRow("2c/espresso_decline_time")  << c << "espressoDeclineTime"<< 18.0;
        QTest::newRow("2c/preinfusion_flow_rate")  << c << "preinfusionFlowRate"<< 4.5;
        // Here the _advanced spelling wins: plain says 32 / 160 / 0.9 / 1.0.
        QTest::newRow("2c/target_weight")          << c << "targetWeight" << 135.0;
        QTest::newRow("2c/target_volume")          << c << "targetVolume" << 180.0;
        QTest::newRow("2c/max_pressure_range_adv") << c << "maximumPressureRangeAdvanced" << 0.6;
        QTest::newRow("2c/max_flow_range_adv")     << c << "maximumFlowRangeAdvanced"     << 0.6;
    }

    void scalarFidelity() {
        QFETCH(QString, fileName);
        QFETCH(QString, field);
        QFETCH(double, expected);

        const QString content = readFile(DE1APP_PROFILES_DIR + "/" + fileName);
        if (content.isEmpty()) QSKIP(qPrintable(fileName + " not found"));
        const Profile p = Profile::loadFromTclString(content);

        const QHash<QString, double> actual = {
            {"espressoPressure",              p.espressoPressure()},
            {"preinfusionTime",               p.preinfusionTime()},
            {"preinfusionFlowRate",           p.preinfusionFlowRate()},
            {"preinfusionStopPressure",       p.preinfusionStopPressure()},
            {"espressoHoldTime",              p.espressoHoldTime()},
            {"espressoDeclineTime",           p.espressoDeclineTime()},
            {"pressureEnd",                   p.pressureEnd()},
            {"flowProfileHold",               p.flowProfileHold()},
            {"flowProfileHoldTime",           p.flowProfileHoldTime()},
            {"flowProfileDecline",            p.flowProfileDecline()},
            {"flowProfileDeclineTime",        p.flowProfileDeclineTime()},
            {"minimumPressure",               p.minimumPressure()},
            {"targetWeight",                  p.targetWeight()},
            {"targetVolume",                  p.targetVolume()},
            {"maximumPressureRangeAdvanced",  p.maximumPressureRangeAdvanced()},
            {"maximumFlowRangeAdvanced",      p.maximumFlowRangeAdvanced()},
            {"maximumPressureRangeDefault",   p.maximumPressureRangeDefault()},
            {"maximumFlowRangeDefault",       p.maximumFlowRangeDefault()},
        };
        QVERIFY2(actual.contains(field), qPrintable("test bug: unmapped field " + field));
        QVERIFY2(qAbs(actual.value(field) - expected) < 0.001,
                 qPrintable(QString("%1.%2: got %3, expected %4")
                                .arg(fileName, field)
                                .arg(actual.value(field)).arg(expected)));
    }

    void hiddenFlagSurvivesImport() {
        // profile_hide drives de1app's and reaprime's profile lists. Decenza's
        // own list filters through SettingsApp::isHiddenProfile() instead, so
        // this is inert locally and still has to be right on the way out.
        const QString hiddenTcl = readFile(DE1APP_PROFILES_DIR + "/Flow profile for milky drinks.tcl");
        if (hiddenTcl.isEmpty()) QSKIP("fixture not found");
        QCOMPARE(Profile::loadFromTclString(hiddenTcl).toJson().object().value("hidden").toString(),
                 QStringLiteral("1"));

        const QString visibleTcl = readFile(DE1APP_PROFILES_DIR + "/Classic Italian espresso.tcl");
        if (visibleTcl.isEmpty()) QSKIP("fixture not found");
        QCOMPARE(Profile::loadFromTclString(visibleTcl).toJson().object().value("hidden").toString(),
                 QStringLiteral("0"));
    }

    void de1appFlowEditorAliasesSurviveImport() {
        // flow_profile_preinfusion / _preinfusion_time are NOT aliases of
        // preinfusion_flow_rate / preinfusion_time — they are de1app's flow
        // editor's own values, and they differ here (4.2/6 against 8.0/8).
        // Profile does not model them, so they ride through as passthrough keys;
        // dropping them would silently reset de1app's flow editor.
        const QString content = readFile(DE1APP_PROFILES_DIR + "/Classic Italian espresso.tcl");
        if (content.isEmpty()) QSKIP("fixture not found");
        const QJsonObject obj = Profile::loadFromTclString(content).toJson().object();
        QCOMPARE(obj.value("flow_profile_preinfusion").toString(), QStringLiteral("4.2"));
        QCOMPARE(obj.value("flow_profile_preinfusion_time").toString(), QStringLiteral("6"));
        // ...while the pressure editor's own values stay separate.
        QCOMPARE(profileJsonToDouble(obj.value("preinfusion_flow_rate")), 8.0);
        QCOMPARE(profileJsonToDouble(obj.value("preinfusion_time")), 8.0);
    }

    void simpleProfileFramesFollowTheScalars() {
        // Steam_only.tcl is settings_2a and stores an advanced_shot at
        // 82/80/72 °C while its espresso_temperature is 0. de1app never reads
        // that array for a simple profile — pressure_to_advanced_list opens
        // with `set temp_advanced(advanced_shot) {}` and rebuilds from the
        // scalars — so it brews the 0. Importing the stored frames instead is
        // the difference between our steam and de1app's.
        const QString content = readFile(DE1APP_PROFILES_DIR + "/Steam_only.tcl");
        if (content.isEmpty()) QSKIP("Steam_only.tcl not found");

        // The fixture must keep contradicting itself, or this proves nothing.
        QCOMPARE(De1AppTcl::extractValue(content, "espresso_temperature"), QStringLiteral("0"));
        QVERIFY(content.contains(QStringLiteral("advanced_shot {{")));

        const Profile p = Profile::loadFromTclString(content);
        QCOMPARE(p.profileType(), QStringLiteral("settings_2a"));
        QVERIFY(!p.steps().isEmpty());
        QCOMPARE(p.espressoTemperature(), 0.0);
        for (const ProfileFrame& f : p.steps()) {
            QVERIFY2(qFuzzyIsNull(f.temperature),
                     qPrintable(QString("frame '%1' at %2 °C — stored frames were kept")
                                    .arg(f.name).arg(f.temperature)));
        }
    }

    void advancedProfileKeepsItsStoredFrames() {
        // The other half of the rule: settings_2c goes through
        // settings_to_advanced_list, which does NOT rebuild advanced_shot.
        const QString content = readFile(DE1APP_PROFILES_DIR + "/Blooming espresso.tcl");
        if (content.isEmpty()) QSKIP("fixture not found");
        const Profile p = Profile::loadFromTclString(content);
        QVERIFY(p.profileType().startsWith("settings_2c"));
        // Verbatim from the .tcl: 97.5 °C preinfusion, then a 90 °C pause.
        QCOMPARE(p.steps().first().temperature, 97.5);
        QCOMPARE(p.steps().at(2).temperature, 90.0);
    }

    void malformedScalarRefusesRatherThanSubstitutes_data() {
        QTest::addColumn<QString>("line");
        QTest::addColumn<QString>("expectedKey");
        // Each of these silently became a number that changes the shot. The
        // likeliest real source is a locale decimal comma from a
        // European-authored profile.
        QTest::newRow("maximum_pressure comma")
            << "maximum_pressure 9,5" << "maximum_pressure";          // was 0 = limiter OFF
        QTest::newRow("maximum_flow expr")
            << "maximum_flow [expr {$x}]" << "maximum_flow";          // was 0 = limiter OFF
        QTest::newRow("tank temperature comma")
            << "tank_desired_water_temperature 88,5" << "tank_desired_water_temperature";
        // settings_2c reads the _advanced spelling — the type rule decides which
        // key is even consulted, so the plain one would not be read at all.
        QTest::newRow("target weight n/a")
            << "final_desired_shot_weight_advanced n/a" << "final_desired_shot_weight_advanced";
        QTest::newRow("preset garbage")
            << "espresso_temperature_1 ninetythree" << "espresso_temperature_1"; // fabricated 0 °C
        QTest::newRow("read_only garbage")
            << "read_only yes" << "read_only";                        // was 0 = editable
    }

    void malformedScalarRefusesRatherThanSubstitutes() {
        QFETCH(QString, line);
        QFETCH(QString, expectedKey);

        // qWarning is the intended diagnostic here, so init()'s failOnWarning
        // would otherwise fail the test for doing its job.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*"));

        const QString tcl = QStringLiteral(
                                "profile_title {Malformed probe}\n"
                                "settings_profile_type settings_2c\n"
                                "espresso_temperature 93.0\n"
                                "advanced_shot {{exit_if 0 flow 2.0 temperature 93.0 name pour "
                                "pressure 6.0 sensor coffee pump flow seconds 25 volume 0}}\n")
                            + line + QLatin1Char('\n');

        const Profile p = Profile::loadFromTclString(tcl);

        QVERIFY2(p.malformedValues().filter(expectedKey).size() > 0,
                 qPrintable("not recorded; malformedValues = " + p.malformedValues().join(", ")));
        // Invalid means every existing import path refuses it, without each one
        // needing its own check — the same contract as an unknown step key.
        QVERIFY2(!p.isValid(), "profile with an uninterpretable value must not be valid");
        QVERIFY2(!p.validationErrors().filter(expectedKey).isEmpty(),
                 "the user-facing error must name the offending key");
    }

    void malformedFrameValueRefusesRatherThanSubstitutes() {
        // The frame-level twin. `pressure ninebar` would become a 0-bar frame,
        // and 0 bar is a legal frame, so nothing downstream could notice.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*"));
        const Profile p = Profile::loadFromTclString(QStringLiteral(
            "profile_title {Bad frame}\n"
            "settings_profile_type settings_2c\n"
            "espresso_temperature 93.0\n"
            "advanced_shot {{exit_if 0 flow 2.0 temperature 93.0 name pour "
            "pressure ninebar sensor coffee pump pressure seconds 25 volume 0}}\n"));

        QVERIFY2(!p.malformedValues().filter("pressure").isEmpty(),
                 qPrintable("malformedValues = " + p.malformedValues().join(", ")));
        QVERIFY(!p.isValid());
    }

    void absentValueIsNotMalformed() {
        // The distinction the whole change rests on: de1app omits the axis a
        // frame's pump does not drive, and omits scalars it has no opinion on.
        // Absent must stay ordinary, or every stock profile becomes invalid.
        const Profile p = Profile::loadFromTclString(QStringLiteral(
            "profile_title {Sparse but valid}\n"
            "settings_profile_type settings_2c\n"
            "espresso_temperature 93.0\n"
            "advanced_shot {{exit_if 0 temperature 93.0 name pour pressure 6.0 "
            "sensor coffee pump pressure seconds 25 volume 0}}\n"));

        QVERIFY2(p.malformedValues().isEmpty(),
                 qPrintable("absent keys flagged as malformed: " + p.malformedValues().join(", ")));
        QVERIFY(p.isValid());
    }

    void simpleProfileIgnoresBadValuesInDiscardedFrames() {
        // A settings_2a profile throws its stored advanced_shot away, so a bad
        // value inside it cannot affect the shot and must not refuse the
        // profile. The scalars, which DO decide the shot, still count.
        const Profile p = Profile::loadFromTclString(QStringLiteral(
            "profile_title {Simple with junk frames}\n"
            "settings_profile_type settings_2a\n"
            "espresso_temperature 92.0\n"
            "preinfusion_time 10\n"
            "espresso_hold_time 20\n"
            "espresso_pressure 9.0\n"
            "advanced_shot {{exit_if 0 flow 2.0 temperature 93.0 name pour "
            "pressure ninebar sensor coffee pump pressure seconds 25 volume 0}}\n"));

        QVERIFY2(p.malformedValues().isEmpty(),
                 qPrintable("discarded frames still flagged: " + p.malformedValues().join(", ")));
        QVERIFY(p.isValid());
    }

    void everyStockProfileParsesCleanly() {
        // The guard on the guard: if the strictness above were wrong, the whole
        // shipped corpus would go invalid. Data-driven rows can vanish silently;
        // this one cannot.
        QDir dir(DE1APP_PROFILES_DIR);
        if (!dir.exists()) QSKIP("de1app profiles dir not found");
        const QStringList files = dir.entryList({"*.tcl"}, QDir::Files, QDir::Name);
        QVERIFY2(files.size() >= 80, "corpus unexpectedly small");
        for (const QString& f : files) {
            const Profile p = Profile::loadFromTclString(readFile(dir.absoluteFilePath(f)));
            QVERIFY2(p.malformedValues().isEmpty(),
                     qPrintable(f + ": " + p.malformedValues().join(", ")));
        }
    }

    void rewritingABuiltinFromItsSourceDropsNothing_data() {
        QTest::addColumn<QString>("tclPath");
        QDir dir(DE1APP_PROFILES_DIR);
        if (!dir.exists()) QSKIP("de1app profiles dir not found");
        for (const QString& f : dir.entryList({"*.tcl"}, QDir::Files, QDir::Name))
            QTest::newRow(qPrintable(f)) << dir.absoluteFilePath(f);
    }

    void rewritingABuiltinFromItsSourceDropsNothing() {
        // A built-in is supposed to BE its de1app profile, so profile_sync
        // REPLACES rather than merges. That is only safe while a rewrite drops
        // nothing — this asserts it for every shipped profile instead of taking
        // it on trust.
        QFETCH(QString, tclPath);

        const QString content = readFile(tclPath);
        QVERIFY(!content.isEmpty());
        const Profile tcl = Profile::loadFromTclString(content);

        const QString builtinPath = ":/profiles/" + Profile::titleToFilename(tcl.title()) + ".json";
        if (!QFile::exists(builtinPath)) QSKIP("no built-in counterpart");
        QFile bf(builtinPath);
        QVERIFY(bf.open(QIODevice::ReadOnly));
        const QJsonObject existing = QJsonDocument::fromJson(bf.readAll()).object();

        QStringList lost = De1AppTcl::keysLostByRewrite(existing, tcl.toJsonObject());

        // `recipe` may disappear, and ONLY against this evidence: the .tcl source
        // carries no recipe data at all, so a block in the built-in cannot have
        // come from the source and is not being lost by rewriting from it. It was
        // fabricated by toJsonObject() from a default-constructed RecipeParams
        // because the TITLE looked like a recipe profile — finding REC-1, which is
        // how the five A-Flow built-ins came to carry byte-identical 88 °C / 25 s /
        // 4 g blocks matching none of their own frames.
        //
        // Conditional on hasRecipeParams(), never unconditional. An unconditional
        // exemption for `recipe` is exactly the blind spot this corpus exists to
        // close — it would stay green through a change that dropped a REAL block.
        if (!tcl.hasRecipeParams())
            lost.removeAll(QStringLiteral("recipe"));

        QVERIFY2(lost.isEmpty(),
                 qPrintable(tcl.title() + " would lose: " + lost.join(", ")));
    }

    // ==========================================
    // Built-in scalar parity gate
    //
    // Frames are compared by compareWithBuiltin above; this covers the
    // profile-level scalars, which nothing compared until now — which is how 338
    // mismatches across 82 profiles reached a shipped build.
    // ==========================================

    void builtinScalarParity_data() {
        QTest::addColumn<QString>("tclPath");

        QDir dir(DE1APP_PROFILES_DIR);
        if (!dir.exists()) QSKIP("de1app profiles dir not found");
        for (const QString& f : dir.entryList({"*.tcl"}, QDir::Files, QDir::Name))
            QTest::newRow(qPrintable(f)) << dir.absoluteFilePath(f);
    }

    void builtinScalarParity() {
        QFETCH(QString, tclPath);

        const QString content = readFile(tclPath);
        QVERIFY2(!content.isEmpty(), qPrintable("Cannot read: " + tclPath));

        const QString title = De1AppTcl::extractValue(content, "profile_title");
        QVERIFY2(!title.isEmpty(), qPrintable("No profile_title in: " + tclPath));

        const QString builtinPath = ":/profiles/" + Profile::titleToFilename(title) + ".json";
        QVERIFY2(QFile::exists(builtinPath),
                 qPrintable("No built-in JSON for '" + title + "' (tried " + builtinPath + ")"));

        QFile bf(builtinPath);
        QVERIFY(bf.open(QIODevice::ReadOnly));
        const QJsonObject builtin = QJsonDocument::fromJson(bf.readAll()).object();

        QString report;
        for (const De1AppTcl::ScalarDiff& d : De1AppTcl::compareScalars(content, builtin)) {
            report += QString("  %1 (tcl %2): TCL=%3 JSON=%4\n")
                          .arg(d.canonical, d.tclKey, d.tclValue, d.jsonValue);
        }
        QVERIFY2(report.isEmpty(),
                 qPrintable("\n=== scalar drift from de1app: " + title + " ===\n" + report));
    }

    void aFlowProfileOracle_data() {
        QTest::addColumn<QString>("fileName");
        QTest::newRow("dark")        << "A-Flow____default-dark.tcl";
        QTest::newRow("light")       << "A-Flow____default-light.tcl";
        QTest::newRow("like-dflow")  << "A-Flow____default-like-dflow.tcl";
        QTest::newRow("medium")      << "A-Flow____default-medium.tcl";
        QTest::newRow("very-dark")   << "A-Flow____default-very-dark.tcl";
    }

    void aFlowProfileOracle() {
        // A-Flow profiles from Jan3kJ/A_Flow ship with 9 frames directly in the
        // TCL (Pre Fill, Fill, Infuse, 2nd Fill, Pause, Pressure Up, Pressure
        // Decline, Flow Start, Flow Extraction) — the pre-2025-05 6-frame format
        // is obsolete.
        QFETCH(QString, fileName);
        QString content = readFile(DE1APP_PROFILES_DIR + "/" + fileName);
        if (content.isEmpty()) QSKIP(qPrintable(fileName + " not found"));

        Profile p = Profile::loadFromTclString(content);
        QVERIFY(!p.title().isEmpty());
        QVERIFY(p.title().startsWith("A-Flow"));
        QVERIFY(p.profileType().startsWith("settings_2c"));
        QCOMPARE(p.steps().size(), 9);
    }
};

QTEST_GUILESS_MAIN(tst_TclImport)
#include "tst_tclimport.moc"
