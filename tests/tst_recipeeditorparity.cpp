// Recipe-editor parity: Decenza's D-Flow and A-Flow implementations against the
// upstream de1app plugins that define them.
//
//   D-Flow  https://github.com/Damian-AU/D_Flow_Espresso_Profile   @ 7f3c9726
//   A-Flow  https://github.com/Jan3kJ/A_Flow                       @ e1a4d871
//   (both submodules of de1app @ fe5cf40c)
//
// ORACLE DISCIPLINE. Every expected value below traces to a plugin proc or to a
// profile the plugin itself ships. Nothing is derived from Decenza's own code or
// from Decenza's built-in JSONs — those are the subject, not the reference. Where
// the two disagree the plugin is right by definition and the difference is a
// finding. The transcribed rules live in
// openspec/changes/verify-recipe-editor-parity/reference.md with line citations;
// read that before changing an expectation here.
//
// The suite cannot run Tcl, so each rule is transcribed. That is the weak point:
// a transcription error yields a test that passes against the wrong oracle. It is
// checked twice over — once against the plugin source, and once against the
// plugin's stock profiles, which are those rules already executed. An expectation
// no shipped profile exercises is marked TRANSCRIPTION-ONLY.
//
// FIXTURES. A-Flow's five stock profiles come from the plugin's own profiles/
// directory (vendored, byte-identical, all 9 frames). de1app's de1plus/profiles/
// copies are a stale 6-frame snapshot missing default-light entirely (de1app
// issue #350) and must never be used as the reference. D-Flow ships no .tcl at
// all; its three profiles are extracted from plugin.tcl by
// tools/extract_dflow_profiles.py — see that fixture dir's README.

#include <QtTest>
#include <QFile>
#include <QTextStream>

#include "../src/profile/profile.h"
#include "../src/profile/profileframe.h"
#include "../src/profile/recipeparams.h"
#include "../src/profile/recipegenerator.h"
#include "../src/profile/recipeanalyzer.h"

namespace {

QString readFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QTextStream(&f).readAll();
}

// round_to_one_digits, de1app's rounding helper.
double round1(double v) { return std::round(v * 10.0) / 10.0; }

QString num(double v) { return QString::number(v, 'g', 6); }

// Every field-level difference between two frame lists, as `<i> <name> <field>: a -> b`.
// Collecting rather than asserting is deliberate: a field-by-field assertion stops at
// the first mismatch, so each fix reveals another and the real extent stays hidden.
QStringList frameDivergences(const QList<ProfileFrame>& before, const QList<ProfileFrame>& after) {
    QStringList out;
    const qsizetype n = qMin(before.size(), after.size());
    for (qsizetype i = 0; i < n; ++i) {
        const ProfileFrame& a = before[i];
        const ProfileFrame& b = after[i];
        auto cmp = [&](const char* field, double x, double y) {
            if (qAbs(x - y) >= 0.05)
                out << QStringLiteral("%1 %2 %3: %4 -> %5")
                       .arg(i).arg(a.name, QString::fromLatin1(field), num(x), num(y));
        };
        auto cmpStr = [&](const char* field, const QString& x, const QString& y) {
            if (x != y)
                out << QStringLiteral("%1 %2 %3: %4 -> %5")
                       .arg(i).arg(a.name, QString::fromLatin1(field), x, y);
        };
        cmp("temperature",       a.temperature,       b.temperature);
        cmp("seconds",           a.seconds,           b.seconds);
        cmp("pressure",          a.pressure,          b.pressure);
        cmp("flow",              a.flow,              b.flow);
        cmp("volume",            a.volume,            b.volume);
        cmp("exitWeight",        a.exitWeight,        b.exitWeight);
        cmp("maxFlowOrPressure", a.maxFlowOrPressure, b.maxFlowOrPressure);
        cmp("exitPressureOver",  a.exitPressureOver,  b.exitPressureOver);
        cmp("exitFlowOver",      a.exitFlowOver,      b.exitFlowOver);
        cmp("exitFlowUnder",     a.exitFlowUnder,     b.exitFlowUnder);
        cmpStr("pump",           a.pump,              b.pump);
        cmpStr("transition",     a.transition,        b.transition);
        cmpStr("sensor",         a.sensor,            b.sensor);
        cmpStr("name",           a.name,              b.name);
    }
    return out;
}

} // namespace

class tst_RecipeEditorParity : public QObject {
    Q_OBJECT

private:
    static QString dflowDir() { return QStringLiteral(DFLOW_PLUGIN_PROFILES_PATH); }
    static QString aflowDir() { return QStringLiteral(DE1APP_PROFILES_PATH); }

    static Profile loadDFlow(const QString& file) {
        return Profile::loadFromTclString(readFile(dflowDir() + "/" + file));
    }
    static Profile loadAFlow(const QString& file) {
        return Profile::loadFromTclString(readFile(aflowDir() + "/" + file));
    }

private slots:

    void initTestCase() {
        QTest::failOnWarning();
    }

    // ==================================================================
    // 1. Fixture integrity — the oracle must be the oracle
    // ==================================================================

    void dflowFixturesArePresent_data() {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("title");
        QTest::newRow("default")   << "D-Flow____default.tcl"   << "D-Flow / default";
        QTest::newRow("Q")         << "D-Flow____Q.tcl"         << "D-Flow / Q";
        QTest::newRow("La Pavoni") << "D-Flow____La_Pavoni.tcl" << "D-Flow / La Pavoni";
    }

    void dflowFixturesArePresent() {
        QFETCH(QString, file);
        QFETCH(QString, title);
        const Profile p = loadDFlow(file);
        QCOMPARE(p.title(), title);
        // D-Flow is a 3-frame editor: Filling, Infusing, Pouring. prep reads
        // indices 0/1/2 with no detection, so a different count means the
        // fixture is not what the plugin writes.
        QCOMPARE(p.steps().size(), qsizetype(3));
        QVERIFY2(p.malformedValues().isEmpty(),
                 qPrintable("fixture unparseable: " + p.malformedValues().join(", ")));
    }

    void aflowFixturesAreTheNineFrameOnes_data() {
        QTest::addColumn<QString>("file");
        for (const char* n : {"dark", "light", "like-dflow", "medium", "very-dark"})
            QTest::newRow(n) << QStringLiteral("A-Flow____default-%1.tcl").arg(n);
    }

    void aflowFixturesAreTheNineFrameOnes() {
        // The guard that keeps de1app #350's stale snapshot from becoming the
        // oracle. Those copies have 6 frames; the plugin's have 9. Verifying
        // against 6 would produce a suite that passes against the wrong source.
        QFETCH(QString, file);
        const Profile p = loadAFlow(file);
        QVERIFY2(!p.title().isEmpty(), qPrintable("missing fixture: " + file));
        QCOMPARE(p.steps().size(), qsizetype(9));
        QVERIFY(p.title().startsWith(QStringLiteral("A-Flow /")));
    }

    // ==================================================================
    // 2. D-Flow — extraction (task 2.1)
    //
    // proc prep, plugin.tcl:194-209. Fixed indices 0/1/2, no detection.
    // ==================================================================

    void dflowExtractionMatchesPrep_data() {
        QTest::addColumn<QString>("file");
        QTest::newRow("default")   << "D-Flow____default.tcl";
        QTest::newRow("Q")         << "D-Flow____Q.tcl";
        QTest::newRow("La Pavoni") << "D-Flow____La_Pavoni.tcl";
    }

    void dflowExtractionMatchesPrep() {
        QFETCH(QString, file);
        const Profile p = loadDFlow(file);
        QCOMPARE(p.steps().size(), qsizetype(3));

        // What prep would set, read straight off the frames by index.
        const ProfileFrame& filling = p.steps()[0];
        const ProfileFrame& soaking = p.steps()[1];
        const ProfileFrame& pouring = p.steps()[2];

        const double expectFillTemp    = filling.temperature;
        const double expectSoakSeconds = round1(soaking.seconds);
        const double expectSoakPress   = soaking.pressure;
        const double expectSoakVolume  = soaking.volume;
        const double expectSoakWeight  = soaking.exitWeight;
        const double expectPourFlow    = round1(pouring.flow);
        const double expectPourPress   = pouring.maxFlowOrPressure;  // NOT pouring.pressure
        const double expectPourTemp    = pouring.temperature;

        const RecipeParams got = RecipeAnalyzer::extractRecipeParams(p);

        QCOMPARE(got.fillTemperature, expectFillTemp);
        QCOMPARE(got.infuseTime,      expectSoakSeconds);
        QCOMPARE(got.infusePressure,  expectSoakPress);
        QCOMPARE(got.infuseVolume,    expectSoakVolume);
        QCOMPARE(got.infuseWeight,    expectSoakWeight);
        QCOMPARE(got.pourFlow,        expectPourFlow);
        QCOMPARE(got.pourTemperature, expectPourTemp);

        // prep takes pour pressure from the LIMITER, never from the pressure
        // setpoint. The pour frame's `pressure` is vestigial in D-Flow (the
        // plugin's own profiles carry 4.8 there and never update it), so
        // reading it would yield a number that looks plausible and is wrong.
        QCOMPARE(got.pourPressure, expectPourPress);
    }

    void dflowPourPressureIsTheLimiterNotTheSetpoint() {
        // Called out separately because the two fields differ in every stock
        // D-Flow profile, so a mix-up is silent in a round-trip test but
        // changes what the machine brews.
        const Profile p = loadDFlow("D-Flow____La_Pavoni.tcl");
        const ProfileFrame& pouring = p.steps()[2];
        QVERIFY2(!qFuzzyCompare(pouring.pressure, pouring.maxFlowOrPressure),
                 "fixture no longer distinguishes the two fields — test is toothless");

        const RecipeParams got = RecipeAnalyzer::extractRecipeParams(p);
        QCOMPARE(got.pourPressure, pouring.maxFlowOrPressure);
    }

    // ==================================================================
    // 3. D-Flow — generation (tasks 2.2, 2.3)
    //
    // proc update_D-Flow, plugin.tcl:332-360.
    // ==================================================================

    void dflowGenerationMatchesUpdate_data() { dflowExtractionMatchesPrep_data(); }

    void dflowGenerationMatchesUpdate() {
        QFETCH(QString, file);
        const Profile source = loadDFlow(file);
        const RecipeParams params = RecipeAnalyzer::extractRecipeParams(source);

        const QList<ProfileFrame> got = RecipeGenerator::generateFrames(params);
        QCOMPARE(got.size(), qsizetype(3));

        // update_D-Flow writes exactly these, and nothing else.
        QCOMPARE(got[0].temperature, params.fillTemperature);   // filling(temperature)
        QCOMPARE(got[0].pressure,    params.infusePressure);    // filling(pressure) = SOAK pressure

        QCOMPARE(got[1].temperature, params.pourTemperature);   // soaking(temperature) = POUR temp
        QCOMPARE(got[1].pressure,    params.infusePressure);
        QCOMPARE(got[1].seconds,     params.infuseTime);
        QCOMPARE(got[1].volume,      params.infuseVolume);

        QCOMPARE(got[2].temperature,       params.pourTemperature);
        QCOMPARE(got[2].flow,              params.pourFlow);
        QCOMPARE(got[2].maxFlowOrPressure, params.pourPressure);
    }

    void dflowSoakTemperatureComesFromPourNotFill() {
        // The sharpest D-Flow/A-Flow divergence: D-Flow's soak frame takes the
        // POUR temperature (plugin.tcl:345), A-Flow's takes the FILL temperature
        // (code.tcl:251). A swap survives every round-trip test, because both
        // sides of the swap round-trip — it only shows up against the plugin.
        RecipeParams params;
        params.editorType = EditorType::DFlow;
        params.fillTemperature = 84.0;
        params.pourTemperature = 94.0;

        const QList<ProfileFrame> frames = RecipeGenerator::generateFrames(params);
        QCOMPARE(frames.size(), qsizetype(3));
        QCOMPARE(frames[0].temperature, 84.0);   // filling  <- fill
        QCOMPARE(frames[1].temperature, 94.0);   // soaking  <- POUR
        QCOMPARE(frames[2].temperature, 94.0);   // pouring  <- pour
    }

    void dflowDerivedFillExitPressure_data() {
        QTest::addColumn<double>("soakPressure");
        QTest::addColumn<double>("expectedExit");
        // plugin.tcl:338-344 —
        //   soak < 2.8       -> exit = soak
        //   otherwise        -> exit = round_to_one_digits(soak / 2 + 0.6)
        //   then             -> exit = max(exit, 1.2)
        QTest::newRow("below threshold")   << 2.0  << 2.0;
        QTest::newRow("at threshold")      << 2.8  << 2.0;    // 2.8/2 + 0.6
        QTest::newRow("typical 3 bar")     << 3.0  << 2.1;    // 3.0/2 + 0.6
        QTest::newRow("6 bar (D-Flow / Q)") << 6.0 << 3.6;    // 6.0/2 + 0.6
        QTest::newRow("floor applies")     << 0.5  << 1.2;    // below the 1.2 floor
        QTest::newRow("floor boundary")    << 1.2  << 1.2;
    }

    void dflowDerivedFillExitPressure() {
        QFETCH(double, soakPressure);
        QFETCH(double, expectedExit);

        RecipeParams params;
        params.editorType = EditorType::DFlow;
        params.infusePressure = soakPressure;

        const QList<ProfileFrame> frames = RecipeGenerator::generateFrames(params);
        QCOMPARE(frames[0].pressure, soakPressure);              // fill pressure IS the soak pressure
        QCOMPARE(frames[0].exitPressureOver, expectedExit);
    }

    // ==================================================================
    // 4. D-Flow — round-trip stability (task 2.5)
    // ==================================================================

    void dflowRoundTripIsAFixedPoint_data() { dflowExtractionMatchesPrep_data(); }

    void dflowRoundTripIsAFixedPoint() {
        // The property users actually depend on: open a profile, change
        // nothing, save, and it is still the same profile. This is the exact
        // operation that corrupts profiles today.
        //
        // Every field is compared and every divergence collected before any
        // assertion fires. Asserting field-by-field turns this into whack-a-
        // mole — the first failure masks the rest, so each fix reveals a new
        // one and the true size of the problem is never visible at once.
        QFETCH(QString, file);
        const Profile source = loadDFlow(file);

        const RecipeParams params = RecipeAnalyzer::extractRecipeParams(source);
        const QList<ProfileFrame> regenerated = RecipeGenerator::generateFrames(params);
        QCOMPARE(regenerated.size(), source.steps().size());

        const QStringList divergences = frameDivergences(source.steps(), regenerated);

        // Known findings as (frame index, field) pairs, so a NEW divergence
        // fails loudly while these stand. Keyed on the field rather than the
        // literal before/after values — those differ per profile, and pinning
        // them would make the list a transcription of current behaviour rather
        // than a statement about which fields are known-wrong.
        //
        //   DF-1  filling(volume)            forced to 100
        //   DF-2  filling(weight)            forced to 5 g   [shot-affecting]
        //   DF-3  filling(exit_pressure_over) recomputed      [UPSTREAM, not ours]
        //   DF-4  soaking(exit_pressure_over) rewritten
        //   DF-5  pouring(volume)            forced to 0
        //
        // DF-3 is not a Decenza defect. Decenza applies update_D-Flow's derived
        // rule faithfully; two of the plugin's three stock profiles were authored
        // with values that rule would not produce (default ships 1.5 where the
        // rule gives 2.1, Q ships 3.0 where it gives 3.6). La Pavoni matches
        // exactly, which is what proves the rule is transcribed right rather than
        // the transcription being wrong in three places. Those blobs are written
        // literally by write_*_profile / set_Dflow_default and only recomputed
        // when a user edits — so de1app itself makes the same change on first
        // edit, and de1app is not a round-trip fixed point on those two either.
        static const QList<QPair<int, QString>> knownDivergentFields = {
            {0, QStringLiteral("volume")},            // DF-1
            {0, QStringLiteral("exitWeight")},        // DF-2
            {0, QStringLiteral("exitPressureOver")},  // DF-3 (upstream)
            {1, QStringLiteral("exitPressureOver")},  // DF-4
            {2, QStringLiteral("volume")},            // DF-5
        };

        QStringList unexpected;
        for (const QString& d : divergences) {
            // `<index> <name> <field>: before -> after`
            const int idx = d.section(QLatin1Char(' '), 0, 0).toInt();
            const QString field = d.section(QLatin1Char(':'), 0, 0).section(QLatin1Char(' '), -1);
            if (!knownDivergentFields.contains({idx, field})) unexpected << d;
        }

        QVERIFY2(unexpected.isEmpty(),
                 qPrintable(QStringLiteral("%1: unrecorded divergence(s):\n  %2")
                            .arg(file, unexpected.join(QStringLiteral("\n  ")))));

        // The known ones still have to fail, or the gate would quietly claim
        // parity Decenza does not have (design D7).
        if (!divergences.isEmpty())
            QEXPECT_FAIL("", qPrintable(QStringLiteral("DF-1..DF-5: %1")
                                        .arg(divergences.join(QStringLiteral("; ")))), Continue);
        QVERIFY2(divergences.isEmpty(),
                 qPrintable(QStringLiteral("%1 is not a round-trip fixed point:\n  %2")
                            .arg(file, divergences.join(QStringLiteral("\n  ")))));
    }

    // ==================================================================
    // 5. D-Flow — fields the plugin never writes (task 2.4)
    // ==================================================================

    void dflowLeavesFillTimingAlone() {
        // update_D-Flow writes filling(temperature), filling(pressure) and
        // filling(exit_pressure_over) — and nothing else in that frame. It
        // mutates the existing frame in place, so filling(seconds), (flow),
        // (volume) and (weight) survive whatever the profile carried.
        //
        // Decenza builds the frame from constants instead, so any field it
        // writes that the plugin does not is a value the plugin would have
        // preserved and Decenza overwrites on the first save.
        const Profile source = loadDFlow("D-Flow____La_Pavoni.tcl");
        const ProfileFrame& sourceFill = source.steps()[0];

        const RecipeParams params = RecipeAnalyzer::extractRecipeParams(source);
        const ProfileFrame regenFill = RecipeGenerator::generateFrames(params)[0];

        QVERIFY2(qAbs(sourceFill.seconds - regenFill.seconds) < 0.05,
                 qPrintable(QStringLiteral("filling(seconds) rewritten %1 -> %2; "
                                           "update_D-Flow never writes this field")
                            .arg(sourceFill.seconds).arg(regenFill.seconds)));
        QVERIFY2(qAbs(sourceFill.flow - regenFill.flow) < 0.05,
                 qPrintable(QStringLiteral("filling(flow) rewritten %1 -> %2; "
                                           "update_D-Flow never writes this field")
                            .arg(sourceFill.flow).arg(regenFill.flow)));
        // FINDING DF-1 — filling(volume) forced to a constant 100. The plugin
        // preserves whatever the profile carried; Q and La Pavoni both ship 60.
        QEXPECT_FAIL("", "DF-1: createFillFrame hardcodes volume = 100", Continue);
        QVERIFY2(qAbs(sourceFill.volume - regenFill.volume) < 0.05,
                 qPrintable(QStringLiteral("filling(volume) rewritten %1 -> %2")
                            .arg(sourceFill.volume).arg(regenFill.volume)));

        // FINDING DF-2 — filling(weight) forced to 5 g, and this one is
        // shot-affecting. createFillFrame hardcodes exitWeight = 5.0, commented
        // "matches de1app default: weight 5.00" — which generalises a value that
        // is only D-Flow / default's. Q and La Pavoni both ship weight 0.00,
        // meaning NO weight exit, and update_D-Flow never writes the field. So
        // Decenza imposes a 5 g app-side exit on a fill step whose author asked
        // for none, cutting the fill short.
        QEXPECT_FAIL("", "DF-2: createFillFrame hardcodes exitWeight = 5.0", Continue);
        QVERIFY2(qAbs(sourceFill.exitWeight - regenFill.exitWeight) < 0.05,
                 qPrintable(QStringLiteral("filling(weight) rewritten %1 -> %2")
                            .arg(sourceFill.exitWeight).arg(regenFill.exitWeight)));
    }
};

QTEST_MAIN(tst_RecipeEditorParity)
#include "tst_recipeeditorparity.moc"
