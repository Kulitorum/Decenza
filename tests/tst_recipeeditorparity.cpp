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

// A-Flow frame roles, exactly as proc set_profile_index assigns them
// (code.tcl:171-190): the 9-frame layout when the list is longer than 8, the
// legacy 6-frame one otherwise. Roles are positional — prep indexes, it never
// pattern-matches — so this must not "find" a frame by name.
struct AFlowRoles {
    const QList<ProfileFrame>& f;
    bool nine;
    explicit AFlowRoles(const QList<ProfileFrame>& frames)
        : f(frames), nine(frames.size() > 8) {}

    const ProfileFrame& filling()      const { return f[nine ? 1 : 0]; }
    const ProfileFrame& soaking()      const { return f[nine ? 2 : 1]; }
    const ProfileFrame& pause()        const { return f[4]; }   // 9-frame only
    const ProfileFrame& rampUp()       const { return f[nine ? 5 : 2]; }
    const ProfileFrame& rampDown()     const { return f[nine ? 6 : 3]; }
    const ProfileFrame& pouringStart() const { return f[nine ? 7 : 4]; }
    const ProfileFrame& pouring()      const { return f[nine ? 8 : 5]; }
};

// What proc prep (code.tcl:193-238) would set, computed from the frames alone.
struct AFlowExpected {
    double fillTemperature{}, fillFlow{};
    double soakSeconds{}, soakPressure{}, soakVolume{}, soakWeight{};
    double rampUpDownSeconds{};
    double pourFlow{}, pourPressure{}, pourTemperature{};
    bool rampDownEnabled{}, flowExtractionUp{}, secondFillEnabled{};

    static AFlowExpected fromFrames(const AFlowRoles& r, qsizetype frameCount) {
        AFlowExpected e;
        e.fillTemperature = r.filling().temperature;
        e.fillFlow        = r.filling().flow;
        e.soakSeconds     = round1(r.soaking().seconds);
        e.soakPressure    = r.soaking().pressure;
        e.soakVolume      = r.soaking().volume;
        e.soakWeight      = r.soaking().exitWeight;
        // the SUM of both ramp frames, rounded to an integer
        e.rampUpDownSeconds = std::round(r.rampUp().seconds + r.rampDown().seconds);
        e.pourFlow        = round1(r.pouringStart().flow);   // Flow Start, not extraction
        e.pourPressure    = r.rampUp().pressure;
        e.pourTemperature = r.rampUp().temperature;
        // Toggles, stored nowhere — derived from structure every load.
        e.rampDownEnabled   = r.rampDown().seconds > 0;
        e.flowExtractionUp  = r.pouring().flow > e.pourFlow;
        e.secondFillEnabled = frameCount > 8 && r.pause().seconds > 0;
        return e;
    }
};

// RecipeParams as prep would populate them — the CORRECT values, bypassing
// RecipeAnalyzer entirely. Generation must be testable without extraction in the
// path: a round-trip is extract-then-generate, so with extraction known broken
// (AF-1..AF-5) any round-trip failure could originate at either end, and
// generation defects would sit masked behind extraction ones.
RecipeParams paramsFromPrep(const AFlowExpected& e) {
    RecipeParams p;
    p.editorType       = EditorType::AFlow;   // carried by the title, per design D2
    p.fillTemperature  = e.fillTemperature;
    p.fillFlow         = e.fillFlow;
    p.infuseTime       = e.soakSeconds;
    p.infusePressure   = e.soakPressure;
    p.infuseVolume     = e.soakVolume;
    p.infuseWeight     = e.soakWeight;
    p.rampTime         = e.rampUpDownSeconds;
    p.pourFlow         = e.pourFlow;
    p.pourPressure     = e.pourPressure;
    p.pourTemperature  = e.pourTemperature;
    p.rampDownEnabled  = e.rampDownEnabled;
    p.flowExtractionUp = e.flowExtractionUp;
    p.secondFillEnabled = e.secondFillEnabled;
    return p;
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
    // The legacy 6-frame layout. de1app's stale distribution copy — see that
    // fixture dir's README: correct as the LEGACY case, never as the oracle.
    static Profile loadLegacyAFlow() {
        return Profile::loadFromTclString(
            readFile(QStringLiteral(AFLOW_LEGACY_PROFILES_PATH)
                     + "/A-Flow____default-medium.tcl"));
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
        //   DF-1  filling(volume)             forced to 100   -> MaxVol, cap moves
        //   DF-2  filling(weight)             forced to 5 g   -> app-side step skip
        //   DF-3  filling(exit_pressure_over) recomputed      -> RETIRED, not a defect
        //   DF-4  soaking(exit_pressure_over) rewritten       -> inert (exit_if 0)
        //   DF-5  pouring(volume)             forced to 0     -> MaxVol, cap DELETED
        //
        // All of DF-1/2/5 write `D-Flow / default`'s literal value. The generator
        // was written by reading one profile and generalising it; Q and La Pavoni
        // are the two that differ, and the two that get overwritten.
        //
        // What each field actually does, from de1app's own execution path:
        //   volume            -> packed unconditionally as MaxVol (binary.tcl:967).
        //                        Firmware contract, binary.tcl:1077: "Exit current
        //                        frame if the volume/weight exceeds this value.
        //                        0 means ignore." So DF-5 does not move a cap, it
        //                        removes one.
        //   weight            -> app-side profile_target; exceeding it calls
        //                        start_next_step (device_scale.tcl:1210,1254).
        //   exit_pressure_over-> reaches the machine only when exit_if == 1;
        //                        otherwise TriggerVal is 0 (binary.tcl:933,955).
        //                        The soak frame is exit_if 0, so DF-4 is inert.
        //
        // DF-3 is not a Decenza defect and is kept here only so it does not read
        // as an unexplained gap. Decenza applies update_D-Flow's derived rule
        // faithfully; two stock profiles carry authored values that rule would not
        // produce (default 1.5 vs 2.1, Q 3.0 vs 3.6) while La Pavoni matches
        // exactly — which is what proves the transcription right rather than wrong
        // in three places. Reading the profiles as intentional resolves it: the
        // shipped values are authored, and the derived rule is the fallback for
        // when the user changes soak pressure. de1app makes the same change on
        // first edit, and that is accepted upstream behaviour.
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

    // ==================================================================
    // 6. A-Flow — extraction (tasks 3.1, 3.2, 3.3, 3.4)
    //
    // proc prep, code.tcl:193-238, over set_profile_index's roles.
    // ==================================================================

    void aflowExtractionMatchesPrep_data() { aflowFixturesAreTheNineFrameOnes_data(); }

    void aflowExtractionMatchesPrep() {
        QFETCH(QString, file);
        const Profile p = loadAFlow(file);
        QCOMPARE(p.steps().size(), qsizetype(9));

        // 9-frame roles, straight from set_profile_index. prep indexes; it does
        // not pattern-match, so neither does the expectation.
        const AFlowRoles r(p.steps());
        const AFlowExpected want = AFlowExpected::fromFrames(r, /*frameCount=*/9);

        const RecipeParams got = RecipeAnalyzer::extractRecipeParams(p);

        QStringList wrong;
        auto check = [&](const char* what, double g, double w) {
            if (qAbs(g - w) >= 0.05)
                wrong << QStringLiteral("%1: got %2, prep gives %3")
                         .arg(QString::fromLatin1(what), num(g), num(w));
        };
        check("fillTemperature", got.fillTemperature, want.fillTemperature);
        check("fillFlow",        got.fillFlow,        want.fillFlow);
        check("infuseTime",      got.infuseTime,      want.soakSeconds);
        check("infusePressure",  got.infusePressure,  want.soakPressure);
        check("infuseVolume",    got.infuseVolume,    want.soakVolume);
        check("infuseWeight",    got.infuseWeight,    want.soakWeight);
        check("rampTime",        got.rampTime,        want.rampUpDownSeconds);
        check("pourFlow",        got.pourFlow,        want.pourFlow);
        check("pourPressure",    got.pourPressure,    want.pourPressure);
        check("pourTemperature", got.pourTemperature, want.pourTemperature);

        // FINDINGS AF-1 (pourFlow off by 2x) and AF-3 (rampTime not summed).
        if (!wrong.isEmpty())
            QEXPECT_FAIL("", qPrintable(QStringLiteral("AF-1/AF-3/AF-5: %1")
                                        .arg(wrong.join(QStringLiteral("; ")))), Continue);
        QVERIFY2(wrong.isEmpty(),
                 qPrintable(QStringLiteral("%1: extraction disagrees with prep:\n  %2")
                            .arg(file, wrong.join(QStringLiteral("\n  ")))));
    }

    void aflowTogglesAreDerivedFromFrameStructure_data() { aflowFixturesAreTheNineFrameOnes_data(); }

    void aflowTogglesAreDerivedFromFrameStructure() {
        // The three toggles are stored NOWHERE — prep computes them from the
        // frames every load (code.tcl:214-232). This is the property that makes
        // "recipe parameters cannot be recovered from frames" false for A-Flow,
        // so it is asserted directly rather than inferred from a round-trip.
        QFETCH(QString, file);
        const Profile p = loadAFlow(file);
        const AFlowRoles r(p.steps());
        const AFlowExpected want = AFlowExpected::fromFrames(r, 9);

        const RecipeParams got = RecipeAnalyzer::extractRecipeParams(p);

        QStringList wrong;
        auto checkBool = [&](const char* what, bool g, bool w) {
            if (g != w)
                wrong << QStringLiteral("%1: got %2, prep gives %3")
                         .arg(QString::fromLatin1(what),
                              g ? QStringLiteral("true") : QStringLiteral("false"),
                              w ? QStringLiteral("true") : QStringLiteral("false"));
        };
        checkBool("rampDownEnabled",   got.rampDownEnabled,   want.rampDownEnabled);
        checkBool("flowExtractionUp",  got.flowExtractionUp,  want.flowExtractionUp);
        checkBool("secondFillEnabled", got.secondFillEnabled, want.secondFillEnabled);

        // FINDINGS AF-2 (flowExtractionUp) and AF-4 (rampDownEnabled).
        if (!wrong.isEmpty())
            QEXPECT_FAIL("", qPrintable(QStringLiteral("AF-2/AF-4: %1")
                                        .arg(wrong.join(QStringLiteral("; ")))), Continue);
        QVERIFY2(wrong.isEmpty(),
                 qPrintable(QStringLiteral("%1: toggles disagree with prep:\n  %2")
                            .arg(file, wrong.join(QStringLiteral("\n  ")))));
    }

    void aflowVeryDarkHasRampDownEnabled() {
        // Task 3.4. Singled out because it is independently documented: the
        // plugin readme describes default-very-dark as "a profile with `Ramp
        // down` enabled". Its Pressure Decline frame carries a non-zero
        // duration, so prep derives true.
        //
        // Decenza's shipped a_flow_default_very_dark.json claims
        // rampDownEnabled: false — as do all five, from one identical block.
        // This asserts the frames, which is where the truth is.
        const Profile p = loadAFlow("A-Flow____default-very-dark.tcl");
        const AFlowRoles r(p.steps());
        QVERIFY2(r.rampDown().seconds > 0,
                 "fixture no longer has a non-zero decline — check the plugin");

        const RecipeParams got = RecipeAnalyzer::extractRecipeParams(p);
        QEXPECT_FAIL("", "AF-4: rampDownEnabled is never derived from the frames", Continue);
        QVERIFY2(got.rampDownEnabled,
                 "default-very-dark must extract rampDownEnabled = true "
                 "(plugin readme, and its Pressure Decline frame is non-zero)");
    }

    void aflowExtractionNeedsNoStoredRecipe_data() { aflowFixturesAreTheNineFrameOnes_data(); }

    void aflowExtractionNeedsNoStoredRecipe() {
        // Task 3.3. The .tcl fixtures carry no recipe block of any kind — no
        // de1app profile does. If extraction works from these, the frames alone
        // are sufficient, which is precisely what the plugins rely on.
        QFETCH(QString, file);
        const Profile p = loadAFlow(file);
        QVERIFY2(!readFile(aflowDir() + "/" + file).contains(QStringLiteral("recipe")),
                 "fixture unexpectedly carries a recipe key");

        const RecipeParams got = RecipeAnalyzer::extractRecipeParams(p);
        // Not defaults: a real profile's numbers came out.
        QVERIFY(got.pourPressure > 0.0);
        QVERIFY(got.infuseTime > 0.0);
    }

    // ==================================================================
    // 7. A-Flow — round-trip (task 4.6)
    // ==================================================================

    void aflowRoundTripIsAFixedPoint_data() { aflowFixturesAreTheNineFrameOnes_data(); }

    void aflowRoundTripIsAFixedPoint() {
        QFETCH(QString, file);
        const Profile source = loadAFlow(file);

        RecipeParams params = RecipeAnalyzer::extractRecipeParams(source);
        params.editorType = EditorType::AFlow;   // carried by the title, per D2
        const QList<ProfileFrame> regenerated = RecipeGenerator::generateFrames(params);

        QVERIFY2(regenerated.size() == source.steps().size(),
                 qPrintable(QStringLiteral("%1: frame count %2 -> %3")
                            .arg(file).arg(source.steps().size()).arg(regenerated.size())));

        const QStringList divergences = frameDivergences(source.steps(), regenerated);
        // FINDINGS AF-1..AF-5. Not one profile survives a no-op save. The flow
        // errors COMPOUND — pourFlow is read from the already-doubled extraction
        // frame and written back doubled again, so each save doubles it afresh.
        if (!divergences.isEmpty())
            QEXPECT_FAIL("", qPrintable(QStringLiteral("AF-1..AF-5: %1")
                                        .arg(divergences.join(QStringLiteral("; ")))), Continue);
        QVERIFY2(divergences.isEmpty(),
                 qPrintable(QStringLiteral("%1 is not a round-trip fixed point:\n  %2")
                            .arg(file, divergences.join(QStringLiteral("\n  ")))));
    }

    // ==================================================================
    // 8. A-Flow — generation, isolated from extraction (tasks 4.1-4.5)
    //
    // proc update_A-Flow, code.tcl:242-400. Params come from paramsFromPrep,
    // never from RecipeAnalyzer, so a failure here is a GENERATION defect and
    // cannot be a consequence of AF-1..AF-5.
    // ==================================================================

    void aflowGenerationRampSplit_data() {
        QTest::addColumn<double>("rampSeconds");
        QTest::addColumn<bool>("rampDownEnabled");
        QTest::addColumn<double>("expectUpSeconds");
        QTest::addColumn<double>("expectDownSeconds");
        // code.tcl:262-270. Integer division; the ODD remainder goes to the
        // DECLINE, not the ramp-up.
        QTest::newRow("off, 10")        << 10.0 << false << 10.0 << 0.0;
        QTest::newRow("off, 0")         <<  0.0 << false <<  0.0 << 0.0;
        QTest::newRow("on, even 10")    << 10.0 << true  <<  5.0 << 5.0;
        QTest::newRow("on, odd 5")      <<  5.0 << true  <<  2.0 << 3.0;
        QTest::newRow("on, odd 7")      <<  7.0 << true  <<  3.0 << 4.0;
        QTest::newRow("on, even 6")     <<  6.0 << true  <<  3.0 << 3.0;
    }

    void aflowGenerationRampSplit() {
        QFETCH(double, rampSeconds);
        QFETCH(bool, rampDownEnabled);
        QFETCH(double, expectUpSeconds);
        QFETCH(double, expectDownSeconds);

        RecipeParams p;
        p.editorType = EditorType::AFlow;
        p.rampTime = rampSeconds;
        p.rampDownEnabled = rampDownEnabled;

        const QList<ProfileFrame> f = RecipeGenerator::generateFrames(p);
        QCOMPARE(f.size(), qsizetype(9));
        QCOMPARE(f[5].seconds, expectUpSeconds);    // Pressure Up
        QCOMPARE(f[6].seconds, expectDownSeconds);  // Pressure Decline
    }

    void aflowGenerationExitThresholds_data() {
        QTest::addColumn<double>("pourFlow");
        QTest::addColumn<bool>("rampDownEnabled");
        QTest::newRow("flow 2, ramp off") << 2.0 << false;
        QTest::newRow("flow 2, ramp on")  << 2.0 << true;
        QTest::newRow("flow 3, ramp on")  << 3.0 << true;
        QTest::newRow("flow 1.8, off")    << 1.8 << false;
    }

    void aflowGenerationExitThresholds() {
        QFETCH(double, pourFlow);
        QFETCH(bool, rampDownEnabled);

        RecipeParams p;
        p.editorType = EditorType::AFlow;
        p.pourFlow = pourFlow;
        p.rampTime = 10.0;               // keeps ramp_up >= 1 so Flow Start stays off
        p.rampDownEnabled = rampDownEnabled;

        const QList<ProfileFrame> f = RecipeGenerator::generateFrames(p);

        // code.tcl:265,269 — doubled only when the decline is doing the rest.
        QCOMPARE(f[5].exitFlowOver, round1(rampDownEnabled ? pourFlow * 2 : pourFlow));
        // code.tcl:261
        QCOMPARE(f[6].exitFlowUnder, round1(pourFlow + 0.1));
    }

    void aflowGenerationFlowStartActivation_data() {
        QTest::addColumn<double>("rampSeconds");
        QTest::addColumn<bool>("expectActive");
        // code.tcl:274 — keyed on the POST-SPLIT ramp_up(seconds), not rampTime.
        QTest::newRow("ramp 10 -> up 10")   << 10.0 << false;
        QTest::newRow("ramp 1 -> up 1")     <<  1.0 << false;
        QTest::newRow("ramp 0 -> up 0")     <<  0.0 << true;
    }

    void aflowGenerationFlowStartActivation() {
        QFETCH(double, rampSeconds);
        QFETCH(bool, expectActive);

        RecipeParams p;
        p.editorType = EditorType::AFlow;
        p.pourFlow = 2.0;
        p.rampTime = rampSeconds;
        p.rampDownEnabled = false;

        const QList<ProfileFrame> f = RecipeGenerator::generateFrames(p);
        const ProfileFrame& flowStart = f[7];

        if (expectActive) {
            QCOMPARE(flowStart.seconds, 10.0);
            QCOMPARE(flowStart.exitFlowOver, round1(p.pourFlow - 0.1));
            QVERIFY(flowStart.exitIf);
            QCOMPARE(flowStart.exitType, QStringLiteral("flow_over"));
        } else {
            QCOMPARE(flowStart.seconds, 0.0);
        }
        // Written unconditionally (code.tcl:284-285).
        QCOMPARE(flowStart.flow, p.pourFlow);
    }

    void aflowGenerationExtractionFlow_data() {
        QTest::addColumn<double>("pourFlow");
        QTest::addColumn<bool>("flowUp");
        QTest::newRow("up, 2")   << 2.0 << true;
        QTest::newRow("flat, 2") << 2.0 << false;
        QTest::newRow("up, 1.8") << 1.8 << true;
    }

    void aflowGenerationExtractionFlow() {
        QFETCH(double, pourFlow);
        QFETCH(bool, flowUp);

        RecipeParams p;
        p.editorType = EditorType::AFlow;
        p.pourFlow = pourFlow;
        p.pourPressure = 9.5;
        p.flowExtractionUp = flowUp;

        const QList<ProfileFrame> f = RecipeGenerator::generateFrames(p);
        // code.tcl:287-291 — doubled when on, ZERO when off (not left alone).
        QCOMPARE(f[8].flow, flowUp ? round1(pourFlow * 2) : 0.0);
        // code.tcl:293
        QCOMPARE(f[8].maxFlowOrPressure, p.pourPressure);
    }

    void aflowGenerationToggleMatrix_data() {
        QTest::addColumn<bool>("rampDown");
        QTest::addColumn<bool>("flowUp");
        QTest::addColumn<bool>("secondFill");
        for (int i = 0; i < 8; ++i) {
            const bool rd = i & 1, fu = i & 2, sf = i & 4;
            QTest::newRow(qPrintable(QStringLiteral("rd=%1 fu=%2 sf=%3")
                                     .arg(rd).arg(fu).arg(sf))) << rd << fu << sf;
        }
    }

    void aflowGenerationToggleMatrix() {
        // Task 4.1. All 8 combinations against the transcribed rules at once,
        // so an interaction between toggles cannot hide behind a single-toggle
        // test passing.
        QFETCH(bool, rampDown);
        QFETCH(bool, flowUp);
        QFETCH(bool, secondFill);

        RecipeParams p;
        p.editorType = EditorType::AFlow;
        p.fillTemperature = 93.0;
        p.pourTemperature = 95.0;
        p.pourPressure = 10.0;
        p.pourFlow = 2.0;
        p.rampTime = 10.0;
        p.infusePressure = 3.0;
        p.infuseTime = 60.0;
        p.rampDownEnabled = rampDown;
        p.flowExtractionUp = flowUp;
        p.secondFillEnabled = secondFill;

        const QList<ProfileFrame> f = RecipeGenerator::generateFrames(p);
        QCOMPARE(f.size(), qsizetype(9));

        QStringList wrong;
        auto want = [&](const char* what, double got, double expect) {
            if (qAbs(got - expect) >= 0.05)
                wrong << QStringLiteral("%1: got %2, update_A-Flow gives %3")
                         .arg(QString::fromLatin1(what), num(got), num(expect));
        };

        want("preFill.temperature", f[0].temperature, p.fillTemperature);  // code.tcl:382
        want("fill.temperature",    f[1].temperature, p.fillTemperature);  // :250
        want("soak.temperature",    f[2].temperature, p.fillTemperature);  // :251 FILL, not pour
        want("soak.pressure",       f[2].pressure,    p.infusePressure);
        want("soak.seconds",        f[2].seconds,     p.infuseTime);

        // :372-380 — 15 s each when on, 0 when off.
        want("2ndFill.seconds", f[3].seconds, secondFill ? 15.0 : 0.0);
        want("pause.seconds",   f[4].seconds, secondFill ? 15.0 : 0.0);

        want("rampUp.temperature",   f[5].temperature, p.pourTemperature);
        want("rampUp.pressure",      f[5].pressure,    p.pourPressure);
        want("rampDown.temperature", f[6].temperature, p.pourTemperature);
        want("pourStart.temperature", f[7].temperature, p.pourTemperature);
        want("pourStart.flow",        f[7].flow,        p.pourFlow);
        want("pouring.temperature",   f[8].temperature, p.pourTemperature);
        want("pouring.flow",          f[8].flow,        flowUp ? round1(p.pourFlow * 2) : 0.0);
        want("pouring.limiter",       f[8].maxFlowOrPressure, p.pourPressure);

        QVERIFY2(wrong.isEmpty(),
                 qPrintable(QStringLiteral("rd=%1 fu=%2 sf=%3:\n  %4")
                            .arg(rampDown).arg(flowUp).arg(secondFill)
                            .arg(wrong.join(QStringLiteral("\n  ")))));
    }

    void aflowGenerationLeavesUnwrittenFieldsAlone_data() { aflowFixturesAreTheNineFrameOnes_data(); }

    void aflowGenerationLeavesUnwrittenFieldsAlone() {
        // Task 4.5, and the highest-yield check in the change: update_A-Flow
        // mutates in place, so every field it does not name survives. Generation
        // is fed prep's CORRECT parameters here, so anything that still differs
        // is generation writing a field the plugin leaves alone — not a
        // consequence of the extraction findings.
        QFETCH(QString, file);
        const Profile source = loadAFlow(file);
        const AFlowRoles r(source.steps());
        const RecipeParams p = paramsFromPrep(AFlowExpected::fromFrames(r, 9));

        const QList<ProfileFrame> got = RecipeGenerator::generateFrames(p);
        QCOMPARE(got.size(), source.steps().size());

        const QStringList divergences = frameDivergences(source.steps(), got);

        // FINDING AF-6 — filling(seconds) is written from `fillTimeout`, a
        // parameter A-Flow does not have. prep never reads filling(seconds) and
        // update_A-Flow never writes it, so the plugin preserves the profile's
        // own 15 s. Decenza writes fillTimeout, which here is RecipeParams'
        // default of 25.
        //
        // Worth reading alongside AF-5: through the real app path the same field
        // instead becomes 1 s, because RecipeAnalyzer reads it off the Pre Fill
        // frame. Two different wrong values for one field — 25 from generation,
        // 1 from extraction — which is why they are separate findings and why
        // testing generation with prep-correct params was necessary to see this
        // one at all.
        if (!divergences.isEmpty())
            QEXPECT_FAIL("", qPrintable(QStringLiteral("AF-6: %1")
                                        .arg(divergences.join(QStringLiteral("; ")))), Continue);
        QVERIFY2(divergences.isEmpty(),
                 qPrintable(QStringLiteral("%1: generation alters fields update_A-Flow "
                                           "never writes (params are prep-correct, so this "
                                           "is NOT an extraction artefact):\n  %2")
                            .arg(file, divergences.join(QStringLiteral("\n  ")))));
    }

    // ==================================================================
    // 9. A-Flow — the legacy 6-frame layout (tasks 5.1, 5.2, 5.3)
    //
    // proc set_profile_index, code.tcl:171-190. Still in the field: de1app's
    // distribution ships four A-Flow profiles at 6 frames and cannot
    // self-correct (issue #350), so anyone who installed those still has them.
    // ==================================================================

    void aflowLegacyFixtureIsSixFrames() {
        const Profile p = loadLegacyAFlow();
        QVERIFY2(!p.title().isEmpty(), "legacy fixture missing");
        QCOMPARE(p.steps().size(), qsizetype(6));
        // Roles by position, legacy branch — no pattern matching.
        QCOMPARE(p.steps()[0].name, QStringLiteral("Fill"));
        QCOMPARE(p.steps()[1].name, QStringLiteral("Infuse"));
        QCOMPARE(p.steps()[2].name, QStringLiteral("Pressure Up"));
        QCOMPARE(p.steps()[3].name, QStringLiteral("Pressure Decline"));
        QCOMPARE(p.steps()[4].name, QStringLiteral("Flow Start"));
        QCOMPARE(p.steps()[5].name, QStringLiteral("Flow Extraction"));
    }

    void aflowLegacyRolesResolveByLayout() {
        // Task 5.1. The whole point of set_profile_index: the SAME role sits at
        // a different index depending on frame count. Reading a 6-frame profile
        // with 9-frame indices lands `soaking` on 2nd Fill and shifts every
        // other role — the same class of error AF-5 already is.
        const Profile legacy = loadLegacyAFlow();
        const AFlowRoles r(legacy.steps());
        QVERIFY(!r.nine);
        QCOMPARE(r.filling().name,      QStringLiteral("Fill"));
        QCOMPARE(r.soaking().name,      QStringLiteral("Infuse"));
        QCOMPARE(r.rampUp().name,       QStringLiteral("Pressure Up"));
        QCOMPARE(r.rampDown().name,     QStringLiteral("Pressure Decline"));
        QCOMPARE(r.pouringStart().name, QStringLiteral("Flow Start"));
        QCOMPARE(r.pouring().name,      QStringLiteral("Flow Extraction"));

        const Profile modern = loadAFlow("A-Flow____default-medium.tcl");
        const AFlowRoles r9(modern.steps());
        QVERIFY(r9.nine);
        QCOMPARE(r9.filling().name, QStringLiteral("Fill"));
        QCOMPARE(r9.soaking().name, QStringLiteral("Infuse"));
    }

    void aflowLegacyExtractionMatchesPrep() {
        // Task 5.2. prep runs set_profile_index first, so it reads a 6-frame
        // profile correctly. Decenza has no layout concept at all.
        const Profile p = loadLegacyAFlow();
        const AFlowRoles r(p.steps());
        const AFlowExpected want = AFlowExpected::fromFrames(r, /*frameCount=*/6);

        const RecipeParams got = RecipeAnalyzer::extractRecipeParams(p);

        QStringList wrong;
        auto check = [&](const char* what, double g, double w) {
            if (qAbs(g - w) >= 0.05)
                wrong << QStringLiteral("%1: got %2, prep gives %3")
                         .arg(QString::fromLatin1(what), num(g), num(w));
        };
        check("fillTemperature", got.fillTemperature, want.fillTemperature);
        check("infuseTime",      got.infuseTime,      want.soakSeconds);
        check("infusePressure",  got.infusePressure,  want.soakPressure);
        check("rampTime",        got.rampTime,        want.rampUpDownSeconds);
        check("pourFlow",        got.pourFlow,        want.pourFlow);
        check("pourPressure",    got.pourPressure,    want.pourPressure);
        check("pourTemperature", got.pourTemperature, want.pourTemperature);

        // secondFillEnabled must be FALSE on a 6-frame profile — there is no
        // pause frame to consult (code.tcl:226 guards on llength > 8), and
        // indexing f[4] here would read Flow Start instead.
        if (got.secondFillEnabled)
            wrong << QStringLiteral("secondFillEnabled: got true, prep gives false "
                                    "(no pause frame exists in a 6-frame layout)");

        if (!wrong.isEmpty())
            // AF-1 again, not a new finding: pourFlow still read from the
            // extraction frame. Note what does NOT appear here — AF-5 is absent,
            // because the legacy layout has no Pre Fill frame for fillIndex = 0
            // to land on. Decenza is accidentally MORE correct on the old layout,
            // which is what you would expect from a 3-frame analyzer.
            QEXPECT_FAIL("", qPrintable(QStringLiteral("AF-1 (legacy path): %1")
                                        .arg(wrong.join(QStringLiteral("; ")))), Continue);
        QVERIFY2(wrong.isEmpty(),
                 qPrintable(QStringLiteral("legacy 6-frame extraction disagrees with prep:\n  %1")
                            .arg(wrong.join(QStringLiteral("\n  ")))));
    }

    void aflowLegacyUpgradeInsertsPluginFrames() {
        // Task 5.3. update_A-Flow synthesises pre_filling, 2nd_fill and pause
        // from literals (code.tcl:295-370) and emits 9 frames, so an edited
        // legacy profile comes back upgraded. Decenza always emits 9, so the
        // COUNT matches — this checks the inserted frames carry the plugin's
        // values rather than merely existing.
        const Profile legacy = loadLegacyAFlow();
        const AFlowRoles r(legacy.steps());
        const RecipeParams p = paramsFromPrep(AFlowExpected::fromFrames(r, 6));

        const QList<ProfileFrame> got = RecipeGenerator::generateFrames(p);
        QCOMPARE(got.size(), qsizetype(9));

        QStringList wrong;
        auto want = [&](const char* what, double g, double w) {
            if (qAbs(g - w) >= 0.05)
                wrong << QStringLiteral("%1: got %2, plugin literal is %3")
                         .arg(QString::fromLatin1(what), num(g), num(w));
        };
        auto wantStr = [&](const char* what, const QString& g, const QString& w) {
            if (g != w)
                wrong << QStringLiteral("%1: got %2, plugin literal is %3")
                         .arg(QString::fromLatin1(what), g, w);
        };

        // Pre Fill — literals at code.tcl:300-318, then :382 overrides the
        // temperature with the fill temperature.
        wantStr("preFill.name",    got[0].name, QStringLiteral("Pre Fill"));
        want("preFill.seconds",    got[0].seconds, 1.0);
        want("preFill.flow",       got[0].flow, 8.0);
        want("preFill.temperature", got[0].temperature, p.fillTemperature);
        want("preFill.limiter",    got[0].maxFlowOrPressure, 8.0);

        // 2nd Fill / Pause — literals at code.tcl:327-345 and :346-368.
        wantStr("2ndFill.name", got[3].name, QStringLiteral("2nd Fill"));
        want("2ndFill.flow",    got[3].flow, 8.0);
        want("2ndFill.limiter", got[3].maxFlowOrPressure, 3.0);
        wantStr("pause.name",   got[4].name, QStringLiteral("Pause"));
        want("pause.flow",      got[4].flow, 6.0);
        want("pause.pressure",  got[4].pressure, 1.0);
        want("pause.limiter",   got[4].maxFlowOrPressure, 1.0);

        // The legacy profile has no second fill, so both stay at zero seconds.
        want("2ndFill.seconds", got[3].seconds, 0.0);
        want("pause.seconds",   got[4].seconds, 0.0);

        QVERIFY2(wrong.isEmpty(),
                 qPrintable(QStringLiteral("legacy upgrade does not match the plugin's "
                                           "inserted frames:\n  %1")
                            .arg(wrong.join(QStringLiteral("\n  ")))));
    }

    // ==================================================================
    // 10. Inheritance — A-Flow is derived from D-Flow (tasks 6.1, 6.2)
    //
    // A-Flow's readme: "Profile Editor based on D-Flow ... Infuse parameters
    // are not changed compared to D-Flow. Only the fill step is different with
    // 8 ml/s flow." The inheritance is by lineage, not code — neither plugin
    // sources the other — so it has to be verified, never assumed.
    // ==================================================================

    void inheritedInfuseParametersBehaveIdentically() {
        // Task 6.1. The soak parameters A-Flow inherits unchanged must move the
        // same frame fields in both editors, so a regression in the shared half
        // fails in both rather than being masked in one.
        RecipeParams d;
        d.editorType = EditorType::DFlow;
        d.infusePressure = 4.0; d.infuseTime = 42.0; d.infuseVolume = 77.0; d.infuseWeight = 3.3;

        RecipeParams a = d;
        a.editorType = EditorType::AFlow;

        const QList<ProfileFrame> df = RecipeGenerator::generateFrames(d);
        const QList<ProfileFrame> af = RecipeGenerator::generateFrames(a);

        const ProfileFrame& dSoak = df[1];              // D-Flow: index 1
        const ProfileFrame& aSoak = af[2];              // A-Flow: index 2 (9-frame)

        QCOMPARE(dSoak.pressure,   d.infusePressure);
        QCOMPARE(aSoak.pressure,   a.infusePressure);
        QCOMPARE(dSoak.seconds,    d.infuseTime);
        QCOMPARE(aSoak.seconds,    a.infuseTime);
        QCOMPARE(dSoak.volume,     d.infuseVolume);
        QCOMPARE(aSoak.volume,     a.infuseVolume);
        QCOMPARE(dSoak.exitWeight, d.infuseWeight);
        QCOMPARE(aSoak.exitWeight, a.infuseWeight);
    }

    void documentedDivergencesHoldAndAreNotSwapped() {
        // Task 6.2. The sharpest divergence inheritance does NOT cover: the soak
        // frame's temperature. D-Flow takes it from the POUR temperature
        // (plugin.tcl:345), A-Flow from the FILL temperature (code.tcl:251).
        //
        // A swap here survives every round-trip test, because both sides of a
        // swap round-trip. It only shows against the plugins, which is the whole
        // argument for this suite existing.
        RecipeParams p;
        p.fillTemperature = 84.0;
        p.pourTemperature = 94.0;

        RecipeParams d = p; d.editorType = EditorType::DFlow;
        RecipeParams a = p; a.editorType = EditorType::AFlow;

        QCOMPARE(RecipeGenerator::generateFrames(d)[1].temperature, 94.0);  // POUR
        QCOMPARE(RecipeGenerator::generateFrames(a)[2].temperature, 84.0);  // FILL
    }

    // ==================================================================
    // 11. Parameters Decenza exposes that the plugins do not (tasks 7.1-7.3)
    //
    // Each gets a recorded verdict. An undeclared divergence is a defect by
    // the spec; a declared one has to state what it costs.
    // ==================================================================

    void decenzaOnlyParameter_fillTimeout() {
        // VERDICT: DEFECT. Neither plugin has a fill-time parameter. prep never
        // reads filling(seconds); update_* never writes it. Its only effect is
        // to overwrite a field the plugin preserves — AF-6 (A-Flow, 15 -> 25)
        // and, through the app path, AF-5 (15 -> 1).
        //
        // It round-trips by accident on D-Flow, because RecipeAnalyzer reads it
        // back off the same frame it wrote. That accident is why it went
        // unnoticed; it is not a justification.
        RecipeParams p;
        p.editorType = EditorType::AFlow;
        p.fillTimeout = 25.0;
        const double a = RecipeGenerator::generateFrames(p)[1].seconds;
        p.fillTimeout = 5.0;
        const double b = RecipeGenerator::generateFrames(p)[1].seconds;

        QVERIFY2(!qFuzzyCompare(a, b),
                 "fillTimeout no longer writes filling(seconds) — if it became "
                 "inert, retire the parameter and update this verdict");
        QCOMPARE(a, 25.0);
        QCOMPARE(b, 5.0);
    }

    void decenzaOnlyParameter_fillPressure() {
        // VERDICT: DEFECT for A-Flow, DEAD for D-Flow.
        //
        // D-Flow: createFillFrame writes filling(pressure) from infusePressure,
        // matching the plugin's derived rule — so fillPressure is never read on
        // this path at all. Dead weight, and misleading in the editor.
        //
        // A-Flow: update_A-Flow never writes filling(pressure); Decenza does.
        RecipeParams d;
        d.editorType = EditorType::DFlow;
        d.infusePressure = 3.0;
        d.fillPressure = 9.9;                       // deliberately absurd
        QCOMPARE(RecipeGenerator::generateFrames(d)[0].pressure, 3.0);  // ignored

        RecipeParams a;
        a.editorType = EditorType::AFlow;
        a.fillPressure = 7.0;
        QCOMPARE(RecipeGenerator::generateFrames(a)[1].pressure, 7.0);  // written
    }

    void decenzaOnlyParameter_fillFlow() {
        // VERDICT: PARTIAL — A-Flow reads it, neither writes it.
        //
        // prep DOES read Aflow_filling_flow (code.tcl:201), so it is a genuine
        // A-Flow parameter on the read side. But update_A-Flow never writes it
        // back — it is a round-trip carrier, not an editable value. Decenza
        // writes it, so an edit reaches a field the plugin leaves alone.
        // D-Flow has no fill-flow concept whatsoever.
        RecipeParams a;
        a.editorType = EditorType::AFlow;
        a.fillFlow = 6.0;
        QCOMPARE(RecipeGenerator::generateFrames(a)[1].flow, 6.0);
    }

    void decenzaOnlyParameter_infuseEnabled() {
        // VERDICT: EXTENSION, and a defensible one.
        //
        // Neither plugin has an infuse toggle — soaking(seconds) is always
        // written. Decenza's toggle collapses the frame to 0 s, which is exactly
        // how the plugins express "skip this step" elsewhere (2nd_fill, pause,
        // ramp_down all use seconds 0). So it produces a profile the plugins
        // read correctly and round-trip: a 0 s soak reads back as infuseTime 0.
        //
        // Cost: a user who disables infuse loses the stored soak duration,
        // because the frame no longer carries it. Worth stating in the UI.
        RecipeParams p;
        p.editorType = EditorType::DFlow;
        p.infuseTime = 60.0;
        p.infuseEnabled = false;
        QCOMPARE(RecipeGenerator::generateFrames(p)[1].seconds, 0.0);
        p.infuseEnabled = true;
        QCOMPARE(RecipeGenerator::generateFrames(p)[1].seconds, 60.0);
    }

    // ==================================================================
    // 12b. THE REAL SAVE PATH — Profile::regenerateFromRecipe()
    //
    // Everything above calls RecipeGenerator::generateFrames() directly. The
    // app does not: it goes through regenerateFromRecipe(), which afterwards
    // RESTORES volume and exitWeight from the old frames by name match, for
    // every frame except the infuse one (profile.cpp, citing issue #331).
    //
    // Those are exactly the fields DF-1, DF-2 and DF-5 are about. Testing the
    // generator alone therefore overstates them: it measures a layer the user
    // never reaches on its own.
    //
    // INCOMPLETE — READ BEFORE TRUSTING THESE RESULTS. This is one layer closer
    // to the app than the generator tests, and still not the app. Two things
    // sit between it and a real save, both discovered only after writing it:
    //
    //  1. ProfileManager::getOrConvertRecipeParams() FIXES editorType for an
    //     A-Flow title before the editor ever sees the params. Calling
    //     RecipeAnalyzer directly, as below, leaves editorType at DFlow — so
    //     the A-Flow rows here regenerate a 9-frame profile as a 3-frame
    //     D-Flow one. That is an ARTEFACT of this test, not a shipped bug.
    //  2. The real save short-circuits: `needFrameRegen` is false when no
    //     frame-affecting field changed, so a genuine no-op save does not
    //     regenerate at all and cannot corrupt anything.
    //
    // What that means for severity: a no-op open-and-save is safe, and the
    // findings bite only on a real edit — where the user is shown AF-1's
    // doubled pour flow and writes it back. Quantifying that needs a
    // ProfileManager-level test (getOrConvertRecipeParams -> edit -> save),
    // which does not exist yet. Do not cite these rows as app behaviour.
    // ==================================================================

    void realSavePathDFlow_data() { dflowExtractionMatchesPrep_data(); }

    void realSavePathDFlow() {
        QFETCH(QString, file);
        Profile p = loadDFlow(file);
        const QList<ProfileFrame> before = p.steps();

        // Exactly what a no-op edit-and-save does.
        p.setRecipeParams(RecipeAnalyzer::extractRecipeParams(p));
        p.regenerateFromRecipe();

        const QStringList divergences = frameDivergences(before, p.steps());

        // Only the fields regenerateFromRecipe does NOT restore can survive
        // here. If volume/exitWeight appear, the #331 restore has regressed.
        for (const QString& d : divergences) {
            QVERIFY2(!d.contains(QStringLiteral(" volume:")) &&
                     !d.contains(QStringLiteral(" exitWeight:")),
                     qPrintable(QStringLiteral("%1: the #331 passthrough restore no longer "
                                               "covers this: %2").arg(file, d)));
        }

        if (!divergences.isEmpty())
            QEXPECT_FAIL("", qPrintable(QStringLiteral("only DF-3/DF-4 survive the #331 restore: %1")
                                        .arg(divergences.join(QStringLiteral("; ")))), Continue);
        QVERIFY2(divergences.isEmpty(),
                 qPrintable(QStringLiteral("%1 not a fixed point through the REAL save path:\n  %2")
                            .arg(file, divergences.join(QStringLiteral("\n  ")))));
    }

    void realSavePathAFlow_data() { aflowFixturesAreTheNineFrameOnes_data(); }

    void realSavePathAFlow() {
        QFETCH(QString, file);
        Profile p = loadAFlow(file);
        const QList<ProfileFrame> before = p.steps();

        p.setRecipeParams(RecipeAnalyzer::extractRecipeParams(p));
        p.regenerateFromRecipe();

        const QStringList divergences = frameDivergences(before, p.steps());
        if (!divergences.isEmpty())
            QEXPECT_FAIL("", qPrintable(QStringLiteral("ARTEFACT (editorType not fixed up) + AF-1..AF-6: %1")
                                        .arg(divergences.join(QStringLiteral("; ")))), Continue);
        QVERIFY2(divergences.isEmpty(),
                 qPrintable(QStringLiteral("%1 not a fixed point through the REAL save path:\n  %2")
                            .arg(file, divergences.join(QStringLiteral("\n  ")))));
    }

    // ==================================================================
    // 12. Editor coverage (tasks 8.1, 8.2)
    // ==================================================================

    void editorSurfacesExactlyThePluginParameters() {
        // Task 8.1. RecipeEditorPage.qml binds:
        //
        //   fillTemperature                                     -> both plugins
        //   infusePressure, infuseTime, infuseVolume, infuseWeight -> both
        //   pourFlow, pourPressure, pourTemperature             -> both
        //   rampTime                                            -> A-Flow
        //   rampDownEnabled, flowExtractionUp, secondFillEnabled -> A-Flow
        //   targetWeight, targetVolume, dose                    -> profile level
        //
        // That is exactly the plugins' parameter set. The editor is CORRECT —
        // it exposes what the plugins expose and nothing more.
        //
        // Which makes the four Decenza-only parameters worse than "extensions":
        // fillFlow, fillPressure, fillTimeout and infuseEnabled appear NOWHERE
        // in the QML. No user can set them, no user chose them, and they still
        // reach the frames carrying RecipeParams' struct defaults. They are
        // vestigial fields with a live write path — see AF-6 / AF-5, where
        // fillTimeout rewrites filling(seconds) to 25 s or 1 s on every save of
        // every A-Flow profile without anyone touching a control.
        //
        // Practical consequence for repair: removing them costs no UI. That is
        // asserted here so the claim is checked rather than remembered.
        const QString qml = readFile(QStringLiteral(DECENZA_SOURCE_DIR)
                                     + "/qml/pages/RecipeEditorPage.qml");
        QVERIFY2(!qml.isEmpty(), "RecipeEditorPage.qml not found");

        for (const QString& bound : {QStringLiteral("recipe.fillTemperature"),
                                     QStringLiteral("recipe.infusePressure"),
                                     QStringLiteral("recipe.infuseTime"),
                                     QStringLiteral("recipe.infuseWeight"),
                                     QStringLiteral("recipe.pourFlow"),
                                     QStringLiteral("recipe.pourPressure"),
                                     QStringLiteral("recipe.pourTemperature"),
                                     QStringLiteral("recipe.rampTime"),
                                     QStringLiteral("recipe.rampDownEnabled"),
                                     QStringLiteral("recipe.flowExtractionUp"),
                                     QStringLiteral("recipe.secondFillEnabled")}) {
            QVERIFY2(qml.contains(bound),
                     qPrintable(bound + " is a plugin parameter but the editor "
                                        "no longer binds it"));
        }

        for (const QString& unbound : {QStringLiteral("recipe.fillFlow"),
                                       QStringLiteral("recipe.fillPressure"),
                                       QStringLiteral("recipe.fillTimeout"),
                                       QStringLiteral("recipe.infuseEnabled")}) {
            QVERIFY2(!qml.contains(unbound),
                     qPrintable(unbound + " is now bound in the editor. It has no "
                                          "plugin counterpart — either it became a "
                                          "deliberate extension (update the verdict "
                                          "in findings.md) or it is a mistake."));
        }
    }
};

QTEST_MAIN(tst_RecipeEditorParity)
#include "tst_recipeeditorparity.moc"
