#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>

#include "mocks/McpTestFixture.h"
#include "ble/protocol/de1characteristics.h"
#include "profile/recipeparams.h"

using namespace DE1::Characteristic;

// Forward declaration — implemented in mcptools_profiles.cpp
class ProfileManager;
class McpToolRegistry;
class Settings;
void registerProfileTools(McpToolRegistry* registry, ProfileManager* profileManager, Settings* settings);

// Test MCP profile tools against ProfileManager + MockTransport.
// Critical regression: profiles_edit_params must trigger BLE upload (PR #561).

class tst_McpToolsProfiles : public QObject {
    Q_OBJECT

private:
    // Load a minimal D-Flow profile into the fixture's ProfileManager
    static void loadDFlowProfile(McpTestFixture& f, const QString& title = "D-Flow / Test") {
        QJsonObject json;
        json["title"] = title;
        json["author"] = "test";
        json["notes"] = "";
        json["beverage_type"] = "espresso";
        json["version"] = "2";
        json["legacy_profile_type"] = "settings_2c";
        json["target_weight"] = 36.0;
        json["target_volume"] = 0.0;
        json["espresso_temperature"] = 93.0;
        json["maximum_pressure"] = 12.0;
        json["maximum_flow"] = 6.0;
        json["minimum_pressure"] = 0.0;
        RecipeParams recipe;
        recipe.editorType = EditorType::DFlow;
        recipe.targetWeight = 36.0;
        recipe.fillTemperature = 93.0;
        recipe.pourTemperature = 93.0;
        recipe.pourFlow = 2.0;
        json["recipe"] = recipe.toJson();

        // Build a single preinfusion + pour frame
        QJsonArray steps;
        QJsonObject frame1;
        frame1["name"] = "fill";
        frame1["temperature"] = 93.0;
        frame1["sensor"] = "coffee";
        frame1["pump"] = "flow";
        frame1["transition"] = "fast";
        frame1["pressure"] = 6.0;
        frame1["flow"] = 4.0;
        frame1["seconds"] = 25.0;
        frame1["volume"] = 0.0;
        frame1["exit"] = QJsonObject{{"type", "pressure"}, {"condition", "over"}, {"value", 4.0}};
        frame1["limiter"] = QJsonObject{{"value", 0.0}, {"range", 0.6}};
        steps.append(frame1);

        // A D-Flow profile is ALWAYS three frames — Filling / Infusing / Pouring —
        // because that is what the plugin's `prep` indexes (0/1/2, no pattern
        // matching). A two-frame fixture is not a D-Flow profile, and since
        // parameters are now derived from the frames it has no pour frame to
        // read from.
        QJsonObject frameSoak;
        frameSoak["name"] = "infuse";
        frameSoak["temperature"] = 93.0;
        frameSoak["sensor"] = "coffee";
        frameSoak["pump"] = "pressure";
        frameSoak["transition"] = "fast";
        frameSoak["pressure"] = 3.0;
        frameSoak["flow"] = 8.0;
        frameSoak["seconds"] = 20.0;
        frameSoak["volume"] = 100.0;
        frameSoak["weight"] = 4.0;
        frameSoak["exit"] = QJsonObject{{"type", "pressure"}, {"condition", "over"}, {"value", 3.0}};
        frameSoak["limiter"] = QJsonObject{{"value", 0.0}, {"range", 0.6}};
        steps.append(frameSoak);

        QJsonObject frame2;
        frame2["name"] = "pour";
        frame2["temperature"] = 93.0;
        frame2["sensor"] = "coffee";
        frame2["pump"] = "flow";
        frame2["transition"] = "smooth";
        frame2["pressure"] = 6.0;
        frame2["flow"] = 2.0;
        frame2["seconds"] = 60.0;
        frame2["volume"] = 0.0;
        frame2["exit"] = QJsonObject{{"type", "pressure"}, {"condition", "over"}, {"value", 11.0}};
        frame2["limiter"] = QJsonObject{{"value", 0.0}, {"range", 0.6}};
        steps.append(frame2);

        json["steps"] = steps;
        json["number_of_preinfuse_frames"] = 1;

        QString jsonStr = QJsonDocument(json).toJson(QJsonDocument::Compact);
        f.profileManager.loadProfileFromJson(jsonStr);
        emit f.device.profileUploaded(true, QString());
    }

    // Load a minimal advanced profile
    static void loadAdvancedProfile(McpTestFixture& f, const QString& title = "Test Advanced") {
        QJsonObject json;
        json["title"] = title;
        json["author"] = "test";
        json["notes"] = "";
        json["beverage_type"] = "espresso";
        json["version"] = "2";
        json["legacy_profile_type"] = "settings_2c";
        json["target_weight"] = 36.0;
        json["target_volume"] = 0.0;
        json["espresso_temperature"] = 93.0;
        json["maximum_pressure"] = 12.0;
        json["maximum_flow"] = 6.0;
        json["minimum_pressure"] = 0.0;
        json["number_of_preinfuse_frames"] = 1;

        QJsonObject frame;
        frame["name"] = "preinfusion";
        frame["temperature"] = 93.0;
        frame["sensor"] = "coffee";
        frame["pump"] = "flow";
        frame["transition"] = "fast";
        frame["pressure"] = 1.0;
        frame["flow"] = 4.0;
        frame["seconds"] = 20.0;
        frame["volume"] = 0.0;
        frame["exit"] = QJsonObject{{"type", "pressure"}, {"condition", "over"}, {"value", 4.0}};
        frame["limiter"] = QJsonObject{{"value", 0.0}, {"range", 0.6}};
        json["steps"] = QJsonArray{frame};

        QString jsonStr = QJsonDocument(json).toJson(QJsonDocument::Compact);
        f.profileManager.loadProfileFromJson(jsonStr);
        emit f.device.profileUploaded(true, QString());
    }

private slots:
    // `dose` has always been an accepted edit_params field. It used to write
    // RecipeParams::dose, which lived in the recipe block and was read by nothing.
    // With that field gone it must not fall through to the unrecognised-key path —
    // reporting IGNORED is the one outcome the redirect exists to prevent.
    void editParamsDoseWritesTheRecommendedDose() {
        McpTestFixture f;
        registerProfileTools(&f.registry, &f.profileManager, nullptr);
        loadDFlowProfile(f, "D-Flow / Dose");

        const QJsonObject r = f.callTool("profiles_edit_params",
                                         QJsonObject{{"dose", 20.5}, {"confirmed", true}});
        QVERIFY2(!r.contains("ignoredFields"),
                 qPrintable(QStringLiteral("dose was reported ignored: ")
                            + QJsonDocument(r).toJson(QJsonDocument::Compact)));
        QVERIFY(r.value("success").toBool());
        QCOMPARE(f.profileManager.profileRecommendedDose(), 20.5);
        QVERIFY2(f.profileManager.profileHasRecommendedDose(),
                 "a dose was stored without enabling it, so nothing would read it");
    }

    // `dose` is the one spelling on EVERY editor type (dose-source-precedence).
    // The advanced branch used to accept `recommended_dose` too, straight
    // through the profile map, which is where the two-spelling collision came
    // from. Removing the second spelling retires the collision; adjudicating it
    // was the previous attempt, and it stored a dose with the recommendation
    // left DISABLED — `dose` is set-and-enable, `recommended_dose` set-only.
    void doseAppliesOnAnAdvancedProfile() {
        McpTestFixture f;
        registerProfileTools(&f.registry, &f.profileManager, nullptr);
        loadAdvancedProfile(f, "Advanced Dose");
        QCOMPARE(f.profileManager.currentEditorType(), QString("advanced"));

        const QJsonObject r = f.callTool("profiles_edit_params",
                                         QJsonObject{{"dose", 20.5}, {"confirmed", true}});

        QVERIFY(r.value("success").toBool());
        QVERIFY2(!r.contains("retiredFields"), "nothing retired was sent");
        QCOMPARE(f.profileManager.profileRecommendedDose(), 20.5);
        QVERIFY2(f.profileManager.profileHasRecommendedDose(),
                 "a dose was stored on an advanced profile without enabling it");
    }

    void aRetiredDoseSpellingIsReportedNotApplied() {
        McpTestFixture f;
        registerProfileTools(&f.registry, &f.profileManager, nullptr);
        loadAdvancedProfile(f, "Advanced Retired");
        // Not pre-set through setCurrentProfileRecommendedDose: that marks the
        // profile modified, which would make the "did the rejected edit dirty
        // it?" assertion below vacuous.
        const double doseBefore = f.profileManager.profileRecommendedDose();
        QVERIFY2(!qFuzzyCompare(1.0 + doseBefore, 1.0 + 22.0),
                 "the value under test matches the loaded one — the assertion proves nothing");
        QVERIFY2(!f.profileManager.isProfileModified(),
                 "a freshly loaded profile is already modified — fix the fixture, not the check");

        const QJsonObject r = f.callTool(
            "profiles_edit_params",
            QJsonObject{{"recommended_dose", 22.0}, {"has_recommended_dose", true},
                        {"confirmed", true}});

        // Silently applying them is what this replaces — on the advanced branch
        // the map loop would otherwise still push both straight onto the profile.
        QCOMPARE(f.profileManager.profileRecommendedDose(), doseBefore);
        const QJsonArray retired = r.value("retiredFields").toArray();
        QCOMPARE(retired.size(), 2);
        QVERIFY(retired.contains(QJsonValue(QStringLiteral("recommended_dose"))));
        QVERIFY(retired.contains(QJsonValue(QStringLiteral("has_recommended_dose"))));

        // Every key in the call was retired, so NOTHING was applied. Reporting
        // success here — with a message telling the caller to profiles_save —
        // would be a lie that also dirties the loaded profile, because the
        // upload runs on the way out.
        QVERIFY2(!r.value("success").toBool(),
                 "a call that changed nothing reported success");
        QVERIFY2(r.value("error").toString().contains(QStringLiteral("'dose'")),
                 "the caller was told a field was retired without being told what replaced it");
        QVERIFY2(!f.profileManager.isProfileModified(),
                 "a fully rejected edit dirtied the loaded profile");
    }

    // A rejected spelling must not silence the rest of the call, and the caveat
    // has to reach `message` — a client that reads only success + message would
    // otherwise be told a clean "Profile updated" for a call that dropped an
    // argument.
    void aRetiredSpellingIsReportedOnRecipeEditorsToo() {
        McpTestFixture f;
        registerProfileTools(&f.registry, &f.profileManager, nullptr);
        loadDFlowProfile(f, "D-Flow / Retired Reported");

        const QJsonObject r = f.callTool(
            "profiles_edit_params",
            QJsonObject{{"dose", 20.5}, {"recommended_dose", 22.0}, {"confirmed", true}});

        QVERIFY(r.value("success").toBool());
        QCOMPARE(r.value("retiredFields").toArray().size(), 1);
        QVERIFY2(r.value("message").toString().contains(QStringLiteral("recommended_dose")),
                 "the retired spelling was reported only in a sibling key");
    }

    // `dose` is the one key read straight as a double rather than through
    // toVariant(), and QJsonValue::toDouble() answers 0 for anything that is not
    // a number — which setCurrentProfileRecommendedDose reads as "clear the
    // recommendation". A stringified number is safe (normalizeArguments coerces
    // "18" off the schema type, asserted below so the two layers stay honest
    // about which one is doing the work); an unparseable one is not, and used to
    // delete the profile's dose and report success.
    void aNonNumericDoseIsRejectedRatherThanClearingTheRecommendation() {
        McpTestFixture f;
        registerProfileTools(&f.registry, &f.profileManager, nullptr);
        loadDFlowProfile(f, "D-Flow / String Dose");
        f.profileManager.setCurrentProfileRecommendedDose(19.0);

        const QJsonObject r = f.callTool(
            "profiles_edit_params",
            QJsonObject{{"dose", QStringLiteral("heavy")}, {"confirmed", true}});

        QVERIFY2(!r.value("success").toBool(), "an unparseable dose was accepted");
        QCOMPARE(f.profileManager.profileRecommendedDose(), 19.0);
        QVERIFY(f.profileManager.profileHasRecommendedDose());

        // A numeric string still works — the registry normalises it first.
        const QJsonObject ok = f.callTool(
            "profiles_edit_params",
            QJsonObject{{"dose", QStringLiteral("18")}, {"confirmed", true}});
        QVERIFY(ok.value("success").toBool());
        QCOMPARE(f.profileManager.profileRecommendedDose(), 18.0);
    }

    // Out of range is legal input handled by clamping, but the caller has to be
    // told — echoing success with no note reads as "stored 150".
    void anOutOfRangeDoseIsClampedAndSaidSo() {
        McpTestFixture f;
        registerProfileTools(&f.registry, &f.profileManager, nullptr);
        loadDFlowProfile(f, "D-Flow / Clamped Dose");

        const QJsonObject r = f.callTool(
            "profiles_edit_params", QJsonObject{{"dose", 150.0}, {"confirmed", true}});

        QVERIFY(r.value("success").toBool());
        QCOMPARE(f.profileManager.profileRecommendedDose(), 100.0);
        QCOMPARE(r.value("adjustedFields").toArray().size(), 1);
        QVERIFY(r.value("adjustedNote").toString().contains(QStringLiteral("100")));
    }

    // The retired names losing does not cost `dose` its effect in the same call.
    void doseStillAppliesAlongsideARetiredSpelling() {
        McpTestFixture f;
        registerProfileTools(&f.registry, &f.profileManager, nullptr);
        loadDFlowProfile(f, "D-Flow / One Spelling");

        const QJsonObject r = f.callTool(
            "profiles_edit_params",
            QJsonObject{{"dose", 20.5}, {"recommended_dose", 22.0}, {"confirmed", true}});

        QCOMPARE(f.profileManager.profileRecommendedDose(), 20.5);
        QVERIFY(f.profileManager.profileHasRecommendedDose());
        QVERIFY2(!r.contains("ignoredFields"),
                 "the retired spelling leaked into the unrecognised-field path");
    }

    void doseOfZeroClearsTheRecommendation() {
        // Tests the rule where it lives rather than through the MCP round-trip: an
        // earlier version drove this via profiles_edit_params and did not
        // discriminate — it passed with the zero-clearing removed, so it was
        // asserting nothing. The MCP path for `dose` is covered by
        // editParamsDoseWritesTheRecommendedDose above.
        //
        // Storing 0 with the flag on would be a recommendation of zero grams, which
        // reaches dialing_get_context and the AI advisor and contradicts the .tcl
        // importer's reading of de1app's 0 as "not set".
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / Zero Dose");

        f.profileManager.setCurrentProfileRecommendedDose(19.0);
        QVERIFY(f.profileManager.profileHasRecommendedDose());
        QCOMPARE(f.profileManager.profileRecommendedDose(), 19.0);

        f.profileManager.setCurrentProfileRecommendedDose(0.0);
        QVERIFY2(!f.profileManager.profileHasRecommendedDose(),
                 "a dose of 0 was stored as a recommendation of zero grams");
    }

    void getParamsReportsTheDoseWithItsFlag() {
        // Every profile holds 18 g whether one was set or not, so a bare figure
        // would tell an AI there is a recommendation when there is not.
        McpTestFixture f;
        registerProfileTools(&f.registry, &f.profileManager, nullptr);
        loadDFlowProfile(f, "D-Flow / Dose Read");

        QJsonObject r = f.callTool("profiles_get_params", QJsonObject{});
        QVERIFY(r.contains("recommendedDoseG"));
        QVERIFY2(r.contains("hasRecommendedDose"),
                 "the dose was reported without the flag saying whether it is real");
        QCOMPARE(r.value("hasRecommendedDose").toBool(), false);

        f.callTool("profiles_edit_params", QJsonObject{{"dose", 21.0}, {"confirmed", true}});
        r = f.callTool("profiles_get_params", QJsonObject{});
        QCOMPARE(r.value("hasRecommendedDose").toBool(), true);
        QCOMPARE(r.value("recommendedDoseG").toDouble(), 21.0);
    }

    // The pair is reported on the ADVANCED branch too. It used to be skipped
    // there, on the grounds that the profile-JSON spread already carried
    // `recommended_dose` / `has_recommended_dose` and those were the spellings
    // its edit path accepted. No editor type accepts either raw name now, so
    // the reason is gone and the asymmetry with it.
    void getParamsReportsTheDoseOnAdvancedToo() {
        McpTestFixture f;
        registerProfileTools(&f.registry, &f.profileManager, nullptr);
        loadAdvancedProfile(f, "Advanced Dose Read");
        f.profileManager.setCurrentProfileRecommendedDose(20.0);

        const QJsonObject r = f.callTool("profiles_get_params", QJsonObject{});
        QCOMPARE(r.value("editorType").toString(), QString("advanced"));
        QCOMPARE(r.value("hasRecommendedDose").toBool(), true);
        QCOMPARE(r.value("recommendedDoseG").toDouble(), 20.0);
    }

    void init() { QTest::failOnWarning(); }

    // ===== profiles_list =====

    void profilesListReturnsArray()
    {
        McpTestFixture f;
        registerProfileTools(&f.registry, &f.profileManager, &f.settings);

        QJsonObject result = f.callTool("profiles_list", {});
        QVERIFY(result.contains("profiles"));
        QVERIFY(result["profiles"].isArray());
        QVERIFY(result.contains("count"));
    }

    // ===== profiles_get_active =====

    void profilesGetActiveReturnsFilename()
    {
        McpTestFixture f;
        registerProfileTools(&f.registry, &f.profileManager, &f.settings);
        loadDFlowProfile(f);

        QJsonObject result = f.callTool("profiles_get_active", {});
        QVERIFY(!result.contains("error"));
        QVERIFY(result.contains("filename"));
        QVERIFY(result.contains("targetWeightG"));
        QCOMPARE(result["targetWeightG"].toDouble(), 36.0);
    }

    // ===== profiles_get_params =====

    void profilesGetParamsReturnsDFlowFields()
    {
        McpTestFixture f;
        registerProfileTools(&f.registry, &f.profileManager, &f.settings);
        loadDFlowProfile(f);

        QJsonObject result = f.callTool("profiles_get_params", {});
        QCOMPARE(result["editorType"].toString(), QString("dflow"));
        QVERIFY(result.contains("fillTemperature"));
        QVERIFY(result.contains("pourFlow"));
        QVERIFY(result.contains("targetWeight"));
    }

    void profilesGetParamsReturnsAdvancedFields()
    {
        McpTestFixture f;
        registerProfileTools(&f.registry, &f.profileManager, &f.settings);
        loadAdvancedProfile(f);

        QJsonObject result = f.callTool("profiles_get_params", {});
        QCOMPARE(result["editorType"].toString(), QString("advanced"));
        QVERIFY(result.contains("steps"));
    }

    // ===== profiles_edit_params — PR #561 regression test =====

    void editParamsDFlowTriggersBleUpload()
    {
        // The critical test: editing recipe params must write frames to BLE.
        // PR #561 was a regression where this path silently stopped uploading.
        McpTestFixture f;
        registerProfileTools(&f.registry, &f.profileManager, &f.settings);
        loadDFlowProfile(f);
        f.transport.clearWrites();

        QJsonObject args;
        args["targetWeight"] = 40.0;
        args["pourFlow"] = 2.5;
        QJsonObject result = f.callTool("profiles_edit_params", args);

        QVERIFY(result["success"].toBool());
        QCOMPARE(result["editorType"].toString(), QString("dflow"));

        // Verify BLE writes occurred (HEADER_WRITE + FRAME_WRITE)
        auto headerWrites = f.writesTo(HEADER_WRITE);
        auto frameWrites = f.writesTo(FRAME_WRITE);
        QVERIFY2(!headerWrites.isEmpty(), "profiles_edit_params must write shot header to BLE");
        QVERIFY2(!frameWrites.isEmpty(), "profiles_edit_params must write shot frames to BLE");
    }

    void editParamsAdvancedTriggersBleUpload()
    {
        McpTestFixture f;
        registerProfileTools(&f.registry, &f.profileManager, &f.settings);
        loadAdvancedProfile(f);
        f.transport.clearWrites();

        QJsonObject args;
        args["espresso_temperature"] = 95.0;
        QJsonObject result = f.callTool("profiles_edit_params", args);

        QVERIFY(result["success"].toBool());
        QCOMPARE(result["editorType"].toString(), QString("advanced"));

        auto headerWrites = f.writesTo(HEADER_WRITE);
        QVERIFY2(!headerWrites.isEmpty(), "profiles_edit_params (advanced) must write to BLE");
    }

    void editParamsUpdatesProfileState()
    {
        McpTestFixture f;
        registerProfileTools(&f.registry, &f.profileManager, &f.settings);
        loadDFlowProfile(f);

        QJsonObject args;
        args["targetWeight"] = 42.0;
        f.callTool("profiles_edit_params", args);

        // Verify the profile object was updated
        QCOMPARE(f.profileManager.profileTargetWeight(), 42.0);
        QVERIFY(f.profileManager.isProfileModified());
    }

    // ===== profiles_get_detail =====

    void profilesGetDetailRequiresFilename()
    {
        McpTestFixture f;
        registerProfileTools(&f.registry, &f.profileManager, &f.settings);

        QJsonObject result = f.callTool("profiles_get_detail", {{"filename", ""}});
        QVERIFY(result.contains("error"));
    }

    // ===== profiles_rename =====

    void renameRequiresFilename()
    {
        McpTestFixture f;
        registerProfileTools(&f.registry, &f.profileManager, &f.settings);

        QJsonObject result = f.callTool("profiles_rename", {{"filename", ""}, {"title", "New Name"}});
        QVERIFY(result.contains("error"));
        QVERIFY(!result.contains("success"));
    }

    void renameRequiresTitle()
    {
        McpTestFixture f;
        registerProfileTools(&f.registry, &f.profileManager, &f.settings);

        // Whitespace-only title trims to empty and must be rejected.
        QJsonObject result = f.callTool("profiles_rename", {{"filename", "some_profile"}, {"title", "   "}});
        QVERIFY(result.contains("error"));
        QVERIFY(!result.contains("success"));
    }

    void renameUnknownProfileReturnsError()
    {
        McpTestFixture f;
        registerProfileTools(&f.registry, &f.profileManager, &f.settings);

        QJsonObject result = f.callTool("profiles_rename",
                                        {{"filename", "definitely_not_a_real_profile"}, {"title", "New Name"}});
        QVERIFY(result.contains("error"));
        QVERIFY(result["error"].toString().contains("not found", Qt::CaseInsensitive));
    }
};

QTEST_MAIN(tst_McpToolsProfiles)
#include "tst_mcptools_profiles.moc"
