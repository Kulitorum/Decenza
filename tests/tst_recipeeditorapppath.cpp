// Recipe editing through the path a USER actually takes.
//
// Companion to tst_recipeeditorparity, which tests RecipeGenerator and
// RecipeAnalyzer directly. That is one layer below the app, and the difference
// turned out to matter twice over:
//
//   * Profile::regenerateFromRecipe() restores volume/exitWeight from the old
//     frames after generating (issue #331), so findings measured against the
//     generator alone overstate what a user sees.
//   * ProfileManager::getOrConvertRecipeParams() repairs editorType for an
//     A-Flow title before the editor is populated, so calling RecipeAnalyzer
//     directly produces a failure mode that cannot happen in the app.
//
// Both corrections were only visible from here. This file therefore drives the
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
    // Load a plugin .tcl through Decenza's own reader, then install it via the
    // same JSON entry point the app uses. Keeps the fixture authentic while
    // exercising the real load path.
    static bool installProfile(McpTestFixture& f, const QString& tclPath) {
        const Profile p = Profile::loadFromTclString(readFile(tclPath));
        if (p.steps().isEmpty()) return false;
        return f.profileManager.loadProfileFromJson(
            QString::fromUtf8(QJsonDocument(p.toJsonObject()).toJson(QJsonDocument::Compact)));
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

        if (!wrong.isEmpty())
            QEXPECT_FAIL("", qPrintable(QStringLiteral("AF-1/AF-3/AF-4 in the UI: %1")
                                        .arg(wrong.join(QStringLiteral("; ")))), Continue);
        QVERIFY2(wrong.isEmpty(),
                 qPrintable(QStringLiteral("A-Flow / default-%1: the editor misreports the "
                                           "profile:\n  %2")
                            .arg(name, wrong.join(QStringLiteral("\n  ")))));
    }

    void aflowEditorReportsTheCorrectEditorType_data() { aflowEditorShowsTheProfilesRealParameters_data(); }

    void aflowEditorReportsTheCorrectEditorType() {
        // getOrConvertRecipeParams repairs editorType from the title. Pinning it
        // because tst_recipeeditorparity's direct-RecipeAnalyzer path does NOT,
        // and that difference produced a 9-frame -> 3-frame artefact there. If
        // this repair ever goes away, that artefact becomes real.
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

        if (!wrong.isEmpty())
            QEXPECT_FAIL("", qPrintable(QStringLiteral("edit misplaced: %1")
                                        .arg(wrong.join(QStringLiteral("; ")))), Continue);
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
        QVERIFY2(qAbs(endFlow - startFlow) < 0.05,
                 qPrintable(QStringLiteral("extraction flow drifted across 3 saves: "
                                           "%1 -> %2 (AF-1 compounding)")
                            .arg(num(startFlow), num(endFlow))));
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
