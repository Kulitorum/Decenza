// Recipe editing through the path a USER actually takes.
//
// Companion to tst_recipeeditorparity, which tests RecipeGenerator and
// RecipeAnalyzer directly. That is one layer below the app, and the gap between
// the two is where several findings in this effort actually lived: a generator
// that is provably correct in isolation can still be handed wrong parameters by
// the layer above it, and only this file sees that.
//
// (This header used to describe two specific mechanisms — the issue #331
// volume/exitWeight restore, and an editorType repair in
// getOrConvertRecipeParams. Both were replaced during this same change: the
// restore is now Profile::restoreFieldsThePluginNeverWrites, which works by
// frame ROLE across ten fields, and the editorType repair is gone entirely
// because prepDFlow/prepAFlow set it themselves. Left as a note because the
// stale version survived a review pass and a targeted instruction to check it.)
//
// This file therefore drives the
// two Q_INVOKABLEs QML actually binds and nothing else:
//
//   getOrConvertRecipeParams()   what the editor DISPLAYS
//   uploadRecipeProfile(params)  what Save WRITES
//
// Two questions, in that order — and the first matters even when the second
// passes, because a user dialling from wrong numbers is misled whether or not
// the frames survive:
//
//   1. Does the editor show the profile's real parameters?
//   2. Does saving preserve the profile — untouched, and after a real edit?
//
// Fixtures are the plugins' own stock profiles (see tst_recipeeditorparity's
// header for provenance and the de1app #350 caveat).

#include <QtTest>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>

#include "mocks/McpTestFixture.h"
#include "profile/profile.h"
#include "profile/profileframe.h"
#include "profile/recipeparams.h"

namespace {

QString readFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QTextStream(&f).readAll();
}

double round1(double v) { return std::round(v * 10.0) / 10.0; }
QString num(double v) { return QString::number(v, 'g', 6); }

// Frame roles per A-Flow's set_profile_index (code.tcl:171-190).
struct AFlowRoles {
    const QList<ProfileFrame>& f;
    bool nine;
    explicit AFlowRoles(const QList<ProfileFrame>& frames)
        : f(frames), nine(frames.size() > 8) {}
    const ProfileFrame& filling()      const { return f[nine ? 1 : 0]; }
    const ProfileFrame& soaking()      const { return f[nine ? 2 : 1]; }
    const ProfileFrame& rampUp()       const { return f[nine ? 5 : 2]; }
    const ProfileFrame& rampDown()     const { return f[nine ? 6 : 3]; }
    const ProfileFrame& pouringStart() const { return f[nine ? 7 : 4]; }
    const ProfileFrame& pouring()      const { return f[nine ? 8 : 5]; }
};

QStringList frameDivergences(const QList<ProfileFrame>& before, const QList<ProfileFrame>& after) {
    QStringList out;
    if (before.size() != after.size()) {
        out << QStringLiteral("FRAME COUNT: %1 -> %2").arg(before.size()).arg(after.size());
        return out;
    }
    for (qsizetype i = 0; i < before.size(); ++i) {
        const ProfileFrame& a = before[i];
        const ProfileFrame& b = after[i];
        auto cmp = [&](const char* field, double x, double y) {
            if (qAbs(x - y) >= 0.05)
                out << QStringLiteral("%1 %2 %3: %4 -> %5")
                       .arg(i).arg(a.name, QString::fromLatin1(field), num(x), num(y));
        };
        cmp("temperature", a.temperature, b.temperature);
        cmp("seconds",     a.seconds,     b.seconds);
        cmp("pressure",    a.pressure,    b.pressure);
        cmp("flow",        a.flow,        b.flow);
        cmp("volume",      a.volume,      b.volume);
        cmp("exitWeight",  a.exitWeight,  b.exitWeight);
        cmp("maxFlowOrPressure", a.maxFlowOrPressure, b.maxFlowOrPressure);
        if (a.pump != b.pump)
            out << QStringLiteral("%1 %2 pump: %3 -> %4").arg(i).arg(a.name, a.pump, b.pump);
        if (a.name != b.name)
            out << QStringLiteral("%1 name: %2 -> %3").arg(i).arg(a.name, b.name);
    }
    return out;
}

} // namespace

class tst_RecipeEditorAppPath : public QObject {
    Q_OBJECT

private:
    // Edit-matrix tally, filled row by row and asserted once by
    // editMatrixScoreIsTheRecordedBaseline. Qt Test runs slots in declaration
    // order, so the score slot must stay declared after the matrix.
    static inline int s_matrixRows = 0;
    static inline int s_matrixFieldDiffs = 0;   // total differing fields, all rows
    static inline QStringList s_matrixDiverging;

    // Load a plugin .tcl through Decenza's own reader, then install it via the
    // same JSON entry point the app uses. Keeps the fixture authentic while
    // exercising the real load path.
    static bool installProfile(McpTestFixture& f, const QString& tclPath) {
        const Profile p = Profile::loadFromTclString(readFile(tclPath));
        if (p.steps().isEmpty()) return false;
        return f.profileManager.loadProfileFromJson(
            QString::fromUtf8(QJsonDocument(p.toJsonObject()).toJson(QJsonDocument::Compact)));
    }

    // Compare frames against an edit-matrix golden. Shared by the single-edit
    // matrix and the compound-edit test so the two cannot drift apart in what
    // they consider a match.
    static QStringList goldenDivergences(const QString& goldenText,
                                         const QList<ProfileFrame>& after) {
        // Golden layout: two '#' header lines, then idx + tab-separated FIELDS.
        QStringList wantRows;
        for (const QString& ln : goldenText.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
            if (!ln.startsWith(QLatin1Char('#'))) wantRows << ln;

        QStringList diff;
        if (after.size() != wantRows.size()) {
            diff << QStringLiteral("frame count: decenza %1, de1app %2")
                    .arg(after.size()).arg(wantRows.size());
            return diff;
        }
        for (qsizetype i = 0; i < after.size(); ++i) {
            const QStringList w = wantRows[i].split(QLatin1Char('\t'));
            if (w.size() < 8) continue;
            const ProfileFrame& g = after[i];
            // Columns per gen_edit_matrix.py FIELDS, offset by the idx column.
            auto col = [&](int c) { return w[c].toDouble(); };
            auto cmp = [&](const char* what, double got, double want) {
                if (qAbs(got - want) >= 0.05)
                    diff << QStringLiteral("frame %1 (%2) %3: decenza %4, de1app %5")
                            .arg(i).arg(g.name, QString::fromLatin1(what))
                            .arg(got).arg(want);
            };
            if (g.name != w[1])
                diff << QStringLiteral("frame %1 name: decenza %2, de1app %3")
                        .arg(i).arg(g.name, w[1]);
            cmp("temperature", g.temperature, col(2));
            cmp("pressure",    g.pressure,    col(3));
            cmp("flow",        g.flow,        col(4));
            cmp("seconds",     g.seconds,     col(5));
            cmp("volume",      g.volume,      col(6));
            cmp("weight",      g.exitWeight,  col(7));
            cmp("limiter",     g.maxFlowOrPressure, col(8));
        }
        return diff;
    }

    static QString aflow(const QString& name) {
        return QStringLiteral(DE1APP_PROFILES_PATH) + "/A-Flow____default-" + name + ".tcl";
    }
    static QString dflow(const QString& name) {
        return QStringLiteral(DFLOW_PLUGIN_PROFILES_PATH) + "/D-Flow____" + name + ".tcl";
    }

private slots:
    void init() { QTest::failOnWarning(); }

    // ==================================================================
    // 1. What the editor DISPLAYS
    // ==================================================================

    void aflowEditorShowsTheProfilesRealParameters_data() {
        QTest::addColumn<QString>("name");
        for (const char* n : {"dark", "light", "like-dflow", "medium", "very-dark"})
            QTest::newRow(n) << QString::fromLatin1(n);
    }

    void aflowEditorShowsTheProfilesRealParameters() {
        // The question a user cares about first: open an A-Flow profile, look
        // at the editor — are those the profile's numbers?
        QFETCH(QString, name);
        McpTestFixture f;
        QVERIFY(installProfile(f, aflow(name)));

        const Profile& p = f.profileManager.currentProfile();
        QCOMPARE(p.steps().size(), qsizetype(9));
        const AFlowRoles r(p.steps());

        // prep's values, straight off the frames.
        const double wantPourFlow  = round1(r.pouringStart().flow);
        const double wantPourPress = r.rampUp().pressure;
        const double wantPourTemp  = r.rampUp().temperature;
        const double wantFillTemp  = r.filling().temperature;
        const double wantSoakSecs  = round1(r.soaking().seconds);
        const double wantRamp      = std::round(r.rampUp().seconds + r.rampDown().seconds);
        const bool   wantRampDown  = r.rampDown().seconds > 0;

        const QVariantMap shown = f.profileManager.getOrConvertRecipeParams();

        QStringList wrong;
        auto check = [&](const char* what, double got, double want) {
            if (qAbs(got - want) >= 0.05)
                wrong << QStringLiteral("%1: editor shows %2, profile is %3")
                         .arg(QString::fromLatin1(what), num(got), num(want));
        };
        check("fillTemperature", shown.value("fillTemperature").toDouble(), wantFillTemp);
        check("infuseTime",      shown.value("infuseTime").toDouble(),      wantSoakSecs);
        check("pourTemperature", shown.value("pourTemperature").toDouble(), wantPourTemp);
        check("pourPressure",    shown.value("pourPressure").toDouble(),    wantPourPress);
        check("pourFlow",        shown.value("pourFlow").toDouble(),        wantPourFlow);
        check("rampTime",        shown.value("rampTime").toDouble(),        wantRamp);
        if (shown.value("rampDownEnabled").toBool() != wantRampDown)
            wrong << QStringLiteral("rampDownEnabled: editor shows %1, profile is %2")
                     .arg(shown.value("rampDownEnabled").toBool())
                     .arg(wantRampDown);

        QVERIFY2(wrong.isEmpty(),
                 qPrintable(QStringLiteral("A-Flow / default-%1: the editor misreports the "
                                           "profile:\n  %2")
                            .arg(name, wrong.join(QStringLiteral("\n  ")))));
    }

    void aflowEditorReportsTheCorrectEditorType_data() { aflowEditorShowsTheProfilesRealParameters_data(); }

    void aflowEditorReportsTheCorrectEditorType() {
        // editorType must survive the trip through the app path. This used to be
        // pinning a REPAIR in getOrConvertRecipeParams that set it from the
        // title; that repair no longer exists — prepDFlow/prepAFlow set
        // editorType themselves — so the assertion still holds for a different
        // reason than the one it was written for. Kept because the property is
        // what matters: a 9-frame A-Flow profile read as D-Flow collapses to
        // three frames.
        QFETCH(QString, name);
        McpTestFixture f;
        QVERIFY(installProfile(f, aflow(name)));
        QCOMPARE(f.profileManager.getOrConvertRecipeParams().value("editorType").toString(),
                 QStringLiteral("aflow"));
        QCOMPARE(f.profileManager.currentEditorType(), QStringLiteral("aflow"));
    }

    // ==================================================================
    // 2. What SAVE writes — no-op
    // ==================================================================

    void aflowNoOpSavePreservesTheProfile_data() { aflowEditorShowsTheProfilesRealParameters_data(); }

    void aflowNoOpSavePreservesTheProfile() {
        // Open the editor, touch nothing, save. `needFrameRegen` compares the
        // incoming params against the stored ones, so this should short-circuit
        // and leave the frames alone — the safe case.
        //
        // THIS TEST USED TO PASS, AND THAT PASS WAS THE BUG. With REC-1 in place
        // the profile carried a fabricated recipe block; getOrConvertRecipeParams
        // handed back exactly that block, needFrameRegen saw no change and
        // short-circuited. The frames survived because nothing was ever derived
        // from them. Repairing REC-1 makes the parameters come from the frames, so
        // the save now genuinely regenerates — and the generation-side findings
        // that were hiding behind the short-circuit are visible: AF-1 (pour flow
        // read from the extraction frame, so it doubles) and AF-6/§7 (fill seconds
        // and fill pressure written from parameters neither plugin has). Both are
        // repaired in later sections; the expected-failure comes off then.
        QFETCH(QString, name);
        McpTestFixture f;
        QVERIFY(installProfile(f, aflow(name)));
        const QList<ProfileFrame> before = f.profileManager.currentProfile().steps();

        f.profileManager.uploadRecipeProfile(f.profileManager.getOrConvertRecipeParams());

        const QStringList d = frameDivergences(before, f.profileManager.currentProfile().steps());
        QVERIFY2(d.isEmpty(),
                 qPrintable(QStringLiteral("A-Flow / default-%1: a no-op save changed the "
                                           "profile:\n  %2")
                            .arg(name, d.join(QStringLiteral("\n  ")))));
    }

    void dflowNoOpSavePreservesTheProfile_data() {
        QTest::addColumn<QString>("name");
        QTest::newRow("default")   << "default";
        QTest::newRow("Q")         << "Q";
        QTest::newRow("La Pavoni") << "La_Pavoni";
    }

    void dflowNoOpSavePreservesTheProfile() {
        QFETCH(QString, name);
        McpTestFixture f;
        QVERIFY(installProfile(f, dflow(name)));
        const QList<ProfileFrame> before = f.profileManager.currentProfile().steps();

        f.profileManager.uploadRecipeProfile(f.profileManager.getOrConvertRecipeParams());

        const QStringList d = frameDivergences(before, f.profileManager.currentProfile().steps());
        QVERIFY2(d.isEmpty(),
                 qPrintable(QStringLiteral("D-Flow / %1: a no-op save changed the profile:\n  %2")
                            .arg(name, d.join(QStringLiteral("\n  ")))));
    }

    // ==================================================================
    // 3. What SAVE writes — a real edit
    //
    // The case that actually matters. A user changes ONE value; everything
    // else must stay put, and the changed one must land where the plugin
    // would put it.
    // ==================================================================

    void aflowEditingPourTemperatureMovesOnlyTemperature() {
        McpTestFixture f;
        QVERIFY(installProfile(f, aflow("medium")));
        const QList<ProfileFrame> before = f.profileManager.currentProfile().steps();

        QVariantMap params = f.profileManager.getOrConvertRecipeParams();
        const double newTemp = params.value("pourTemperature").toDouble() + 1.0;
        params["pourTemperature"] = newTemp;
        f.profileManager.uploadRecipeProfile(params);

        const QList<ProfileFrame> after = f.profileManager.currentProfile().steps();
        QCOMPARE(after.size(), before.size());

        // update_A-Flow writes pouring_temperature to ramp_up, ramp_down,
        // pouring_start and pouring (code.tcl:257,260,284,286) — frames 5-8.
        // Frames 0-2 take the FILL temperature and must not move.
        QStringList wrong;
        for (qsizetype i = 5; i <= 8; ++i)
            if (qAbs(after[i].temperature - newTemp) >= 0.05)
                wrong << QStringLiteral("frame %1 (%2) should now be %3, is %4")
                         .arg(i).arg(after[i].name, num(newTemp), num(after[i].temperature));
        for (qsizetype i = 0; i <= 2; ++i)
            if (qAbs(after[i].temperature - before[i].temperature) >= 0.05)
                wrong << QStringLiteral("frame %1 (%2) takes the FILL temperature and "
                                        "must not move: %3 -> %4")
                         .arg(i).arg(after[i].name, num(before[i].temperature),
                                     num(after[i].temperature));

        QVERIFY2(wrong.isEmpty(),
                 qPrintable(QStringLiteral("editing pour temperature:\n  %1")
                            .arg(wrong.join(QStringLiteral("\n  ")))));
    }

    void aflowRepeatedSavesDoNotCompound() {
        // AF-1's signature failure. Pour flow is read from the extraction frame
        // (already doubled) and written back through the same doubling rule, so
        // if it reaches the frames each save multiplies it again. One save can
        // look survivable; three cannot be argued with.
        McpTestFixture f;
        QVERIFY(installProfile(f, aflow("medium")));
        const double startFlow =
            f.profileManager.currentProfile().steps()[8].flow;

        for (int i = 0; i < 3; ++i) {
            QVariantMap params = f.profileManager.getOrConvertRecipeParams();
            // A real edit, on a field unrelated to flow, so needFrameRegen fires.
            params["pourTemperature"] = params.value("pourTemperature").toDouble() + 0.1;
            f.profileManager.uploadRecipeProfile(params);
        }

        const double endFlow = f.profileManager.currentProfile().steps()[8].flow;
        // Same story as aflowNoOpSavePreservesTheProfile: this passed only while
        // the fabricated block kept the frames out of the loop entirely. Now the
        // parameters really do come from the frames, so AF-1's doubling really does
        // compound. Comes off with AF-1.
        QVERIFY2(qAbs(endFlow - startFlow) < 0.05,
                 qPrintable(QStringLiteral("extraction flow drifted across 3 saves: "
                                           "%1 -> %2 (AF-1 compounding)")
                            .arg(num(startFlow), num(endFlow))));
    }

    // ==================================================================
    // 4. THE EDIT MATRIX
    //
    // Every parameter the plugins expose, on every stock profile, driven
    // through ProfileManager's Q_INVOKABLEs and diffed against the frames
    // de1app's OWN prep + update_* produce for the same edit.
    //
    // The wire tests established that identical frames pack to identical
    // bytes. This is the layer above: do we produce the same frames in the
    // first place, after a user changes something? It is the only check here
    // that speaks to "edited profiles brew the same".
    //
    // Goldens: tests/data/edit_matrix/<profile>__<param>.txt, generated by
    //   python3 tools/gen_edit_matrix.py <de1plus-dir>
    // which runs tools/de1app_edit_oracle.tcl — the plugins' real procs,
    // extracted verbatim, not a transcription.
    //
    // Decenza's four extra parameters have no plugin counterpart and are not
    // in the matrix; they are recorded as findings instead.
    //
    // THE SCORE IS A RATCHET. Each row records itself, and
    // editMatrixScoreIsTheRecordedBaseline asserts the total against
    // kMatrixDivergingBaseline below. A per-row QEXPECT_FAIL keeps the suite
    // green but says nothing about whether 86 rows diverge or 3, and the score
    // is the whole acceptance gate for derive-recipe-params-from-frames — it
    // has to be a number in the source that a reviewer can see, not one that
    // has to be dug out of a passing test's captured output.
    //
    // A fix that improves the score FAILS this test. That is intended: lowering
    // the baseline is the deliberate step that records the improvement.
    // ==================================================================

    void editMatrixMatchesDe1app_data() {
        QTest::addColumn<QString>("golden");
        QTest::addColumn<QString>("profile");
        QTest::addColumn<QString>("param");
        QTest::addColumn<QVariant>("value");

        // (paramKey, value) must mirror gen_edit_matrix.py exactly — the golden
        // is only meaningful if both sides make the SAME edit.
        struct Edit { const char* key; const char* param; QVariant value; };
        const QList<Edit> shared = {
            {"fillTemperature", "fillTemperature", 87.5},
            {"infusePressure",  "infusePressure",  5.5},
            {"infuseTime",      "infuseTime",      42.0},
            {"infuseVolume",    "infuseVolume",    77.0},
            {"infuseWeight",    "infuseWeight",    2.5},
            {"pourTemperature", "pourTemperature", 91.5},
            {"pourFlow",        "pourFlow",        2.6},
            {"pourPressure",    "pourPressure",    7.5},
        };
        const QList<Edit> aflowOnly = {
            {"rampTime",          "rampTime",          7.0},
            {"rampDownEnabled",   "rampDownEnabled",   true},
            {"rampDownDisabled",  "rampDownEnabled",   false},
            {"flowExtractionUp",  "flowExtractionUp",  true},
            {"flowExtractionOff", "flowExtractionUp",  false},
            {"secondFillEnabled", "secondFillEnabled", true},
            {"secondFillOff",     "secondFillEnabled", false},
        };

        for (const char* n : {"dark", "light", "like-dflow", "medium", "very-dark"}) {
            const QString base = QStringLiteral("A-Flow____default-%1").arg(n);
            for (const Edit& e : shared + aflowOnly)
                QTest::newRow(qPrintable(QStringLiteral("%1 %2").arg(n, e.key)))
                    << QStringLiteral("%1__%2").arg(base, e.key)
                    << aflow(QString::fromLatin1(n))
                    << QString::fromLatin1(e.param) << e.value;
        }
        for (const char* n : {"default", "Q", "La_Pavoni"}) {
            const QString base = QStringLiteral("D-Flow____%1").arg(n);
            for (const Edit& e : shared)
                QTest::newRow(qPrintable(QStringLiteral("D %1 %2").arg(n, e.key)))
                    << QStringLiteral("%1__%2").arg(base, e.key)
                    << dflow(QString::fromLatin1(n))
                    << QString::fromLatin1(e.param) << e.value;
        }
    }

    void editMatrixMatchesDe1app() {
        QFETCH(QString, golden);
        QFETCH(QString, profile);
        QFETCH(QString, param);
        QFETCH(QVariant, value);

        const QString goldenText =
            readFile(QStringLiteral(EDIT_MATRIX_PATH) + "/" + golden + ".txt");
        QVERIFY2(!goldenText.isEmpty(), qPrintable("missing golden " + golden));

        McpTestFixture f;
        QVERIFY(installProfile(f, profile));

        QVariantMap params = f.profileManager.getOrConvertRecipeParams();
        params[param] = value;
        f.profileManager.uploadRecipeProfile(params);

        const QStringList diff =
            goldenDivergences(goldenText, f.profileManager.currentProfile().steps());

        ++s_matrixRows;
        s_matrixFieldDiffs += int(diff.size());
        if (!diff.isEmpty())
            s_matrixDiverging << QStringLiteral("%1 [%2] %3").arg(golden, param, diff.first());

        if (!diff.isEmpty())
            QEXPECT_FAIL("", qPrintable(QStringLiteral("EDIT-MATRIX: %1 diff(s), first: %2")
                                        .arg(diff.size()).arg(diff.first())), Continue);
        QVERIFY2(diff.isEmpty(),
                 qPrintable(QStringLiteral("%1 — editing %2:\n  %3")
                            .arg(golden, param, diff.mid(0, 12).join(QStringLiteral("\n  ")))));
    }

    void compoundEditMatchesDe1app_data() {
        QTest::addColumn<QString>("golden");
        QTest::addColumn<QString>("profile");
        for (const char* n : {"dark", "light", "like-dflow", "medium", "very-dark"})
            QTest::newRow(qPrintable(QStringLiteral("A %1").arg(n)))
                << QStringLiteral("A-Flow____default-%1__compound").arg(n)
                << aflow(QString::fromLatin1(n));
        for (const char* n : {"default", "Q", "La_Pavoni"})
            QTest::newRow(qPrintable(QStringLiteral("D %1").arg(n)))
                << QStringLiteral("D-Flow____%1__compound").arg(n)
                << dflow(QString::fromLatin1(n));
    }

    void compoundEditMatchesDe1app() {
        // Task 5.3. Two edits in SUCCESSION, each its own save.
        //
        // The single-edit matrix always starts from a pristine profile, so it
        // cannot see an error that only appears once a second edit re-derives its
        // parameters from the frames the first one wrote. That is precisely how
        // AF-1 behaved: one save looked survivable, three could not be argued
        // with. A matrix at 0/99 is necessary but not sufficient evidence, and
        // this is the check that makes it sufficient.
        //
        // The oracle runs prep -> set -> update once per pair, so de1app's side
        // does the same re-derivation.
        //   A-Flow: pourFlow, then rampDownEnabled — flow feeds the ramp exit
        //           thresholds, and ramp-down changes how the ramp time splits.
        //   D-Flow: infusePressure, then pourTemperature — soak pressure drives
        //           the derived fill pressure and its pressure-over exit.
        QFETCH(QString, golden);
        QFETCH(QString, profile);

        const QString goldenText =
            readFile(QStringLiteral(EDIT_MATRIX_PATH) + "/" + golden + ".txt");
        QVERIFY2(!goldenText.isEmpty(), qPrintable("missing golden " + golden));

        const bool isAFlow = golden.startsWith(QLatin1String("A-Flow"));

        McpTestFixture f;
        QVERIFY(installProfile(f, profile));

        auto save = [&](const QString& key, const QVariant& value) {
            QVariantMap params = f.profileManager.getOrConvertRecipeParams();
            params[key] = value;
            f.profileManager.uploadRecipeProfile(params);
        };
        if (isAFlow) {
            save(QStringLiteral("pourFlow"), 2.6);
            save(QStringLiteral("rampDownEnabled"), true);
        } else {
            save(QStringLiteral("infusePressure"), 5.5);
            save(QStringLiteral("pourTemperature"), 91.5);
        }

        const QStringList diff =
            goldenDivergences(goldenText, f.profileManager.currentProfile().steps());
        QVERIFY2(diff.isEmpty(),
                 qPrintable(QStringLiteral("%1 — two successive saves:\n  %2")
                            .arg(golden, diff.mid(0, 12).join(QStringLiteral("\n  ")))));
    }

    void editMatrixScoreIsTheRecordedBaseline() {
        // The acceptance gate for derive-recipe-params-from-frames, as a number.
        //
        // History — each entry is a section of that change landing:
        //   86 / 99  before any repair
        //   75 / 99  after §1 (REC-1): parameters come from the frames, not from
        //            a block fabricated out of RecipeParams' member initialisers.
        //            ALL 24 D-Flow rows now match — that editor is done. All 75
        //            A-Flow rows still diverge, now on `Fill pressure` (Decenza
        //            writes its own fillPressure parameter where the plugin
        //            derives the fill pressure from the soak pressure), which §3
        //            removes. The count moved only 11 because the fill-temperature
        //            divergence was first in each row, not the only one in it.
        //
        // Lower it when a repair improves it; that edit IS the record of the
        // improvement. Never raise it to accommodate a regression.
        //
        //   86 -> 75 (§1, all D-Flow rows clear) -> 75 (§2) -> 0 (§3)
        constexpr int kMatrixDivergingBaseline = 0;

        // The row count alone hides partial progress: a row keeps counting as one
        // divergence whether five fields differ or one, so a repair that fixes
        // four of five fields moves nothing. The field total is what shows a
        // section landing. Same rule — lower it, never raise it.
        //   317  after §1
        //    75  after §2 (prep transcribed): exactly ONE differing field per
        //         diverging row — Fill pressure, which §3 removes
        //     0  after §3 (vestigial parameters removed, and the plugins'
        //         in-place-mutation semantics reinstated in
        //         Profile::restoreFieldsThePluginNeverWrites)
        //
        // ZERO IS THE TARGET, AND IT IS MET. Every parameter either plugin
        // exposes, edited on every profile either plugin ships, now produces the
        // frames the plugin's own prep + update_* produce. Do not raise this.
        constexpr int kMatrixFieldDiffBaseline = 0;

        QCOMPARE(s_matrixRows, 99);
        QVERIFY2(s_matrixFieldDiffs == kMatrixFieldDiffBaseline,
                 qPrintable(QStringLiteral(
                     "edit matrix: %1 differing fields across %2 rows, baseline says %3. %4")
                     .arg(s_matrixFieldDiffs).arg(s_matrixRows).arg(kMatrixFieldDiffBaseline)
                     .arg(s_matrixFieldDiffs < kMatrixFieldDiffBaseline
                              ? QStringLiteral("An improvement — lower the baseline.")
                              : QStringLiteral("A REGRESSION — do not raise it."))));
        QVERIFY2(s_matrixDiverging.size() == kMatrixDivergingBaseline,
                 qPrintable(QStringLiteral(
                     "edit matrix: %1 of %2 rows diverge, baseline says %3.\n"
                     "%4 the baseline in this file.\nFirst five:\n  %5")
                     .arg(s_matrixDiverging.size()).arg(s_matrixRows)
                     .arg(kMatrixDivergingBaseline)
                     .arg(s_matrixDiverging.size() < kMatrixDivergingBaseline
                              ? QStringLiteral("An improvement — lower")
                              : QStringLiteral("A REGRESSION — do not raise"))
                     .arg(s_matrixDiverging.mid(0, 5).join(QStringLiteral("\n  ")))));
    }

    void dflowEditingSoakPressureAppliesTheDerivedFillRule() {
        // D-Flow's one genuinely derived value: filling(pressure) follows the
        // soak pressure, and filling(exit_pressure_over) follows the plugin's
        // formula (plugin.tcl:338-344). A real edit is the only way to reach it.
        McpTestFixture f;
        QVERIFY(installProfile(f, dflow("La_Pavoni")));

        QVariantMap params = f.profileManager.getOrConvertRecipeParams();
        params["infusePressure"] = 6.0;
        f.profileManager.uploadRecipeProfile(params);

        const QList<ProfileFrame> after = f.profileManager.currentProfile().steps();
        QCOMPARE(after[0].pressure, 6.0);            // fill pressure IS the soak pressure
        QCOMPARE(after[0].exitPressureOver, 3.6);    // 6.0/2 + 0.6
        QCOMPARE(after[1].pressure, 6.0);
    }
};

QTEST_MAIN(tst_RecipeEditorAppPath)
#include "tst_recipeeditorapppath.moc"
