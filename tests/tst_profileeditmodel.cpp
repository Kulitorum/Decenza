#include <QtTest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaType>

#include "profile/profileeditmodel.h"
#include "profile/profileeditordraft.h"
#include "profile/profilegenerator.h"

class tst_ProfileEditModel : public QObject
{
    Q_OBJECT

private:
    static Profile dflowProfile()
    {
        ProfileParams params;
        params.editorType = EditorType::DFlow;
        params.applyEditorDefaults();
        // The editor is selected from the native profile title. Without the
        // D-Flow prefix this is an advanced profile, so the parameter editor
        // cannot safely regenerate it.
        Profile profile = ProfileGenerator::createProfile(params, QStringLiteral("D-Flow Draft test"));
        profile.setAuthor(QStringLiteral("Editor test"));
        profile.setProfileNotes(QStringLiteral("preserve notes"));
        return profile;
    }

    static Profile profileWithTitleAndType(const QString& title, const QString& profileType)
    {
        const QJsonObject source{
            {QStringLiteral("title"), title},
            {QStringLiteral("legacy_profile_type"), profileType},
            {QStringLiteral("target_weight"), QStringLiteral("36")},
            {QStringLiteral("steps"), QJsonArray{QJsonObject{
                {QStringLiteral("name"), QStringLiteral("Pour")},
                {QStringLiteral("pump"), QStringLiteral("flow")},
                {QStringLiteral("flow"), QStringLiteral("2")},
                {QStringLiteral("temperature"), QStringLiteral("93")}}}}};
        return Profile::fromJson(QJsonDocument(source));
    }

private slots:
    void init() { QTest::failOnWarning(); }
    void newSimpleDraftSettingsMatchItsFrames_data()
    {
        QTest::addColumn<QString>("editorType");
        QTest::newRow("pressure") << QStringLiteral("pressure");
        QTest::newRow("flow") << QStringLiteral("flow");
    }

    void newSimpleDraftSettingsMatchItsFrames()
    {
        QFETCH(QString, editorType);
        ProfileEditorDraft draft;
        QVERIFY(draft.createNewProfile(editorType, QStringLiteral("New test profile")));
        const QVariantMap before = draft.profile();
        const QVariantMap displayed = draft.getOrConvertProfileParams();
        const auto framesBefore = before.value(QStringLiteral("steps")).toList();
        QVERIFY(!framesBefore.isEmpty());
        QVERIFY(draft.applyParams(displayed));
        QCOMPARE(draft.profile().value(QStringLiteral("steps")).toList(), framesBefore);
        QCOMPARE(draft.profile().value(QStringLiteral("target_weight")),
                 before.value(QStringLiteral("target_weight")));
        QVERIFY(draft.sourceFilename().isEmpty());
    }

    void editorMapRoundTripKeepsCanonicalAndUnknownSourceFields()
    {
        QJsonObject source{{QStringLiteral("title"), QStringLiteral("Draft test")},
                           {QStringLiteral("author"), QStringLiteral("Editor test")},
                           {QStringLiteral("legacy_profile_type"), QStringLiteral("settings_2c")},
                           {QStringLiteral("target_weight"), 36.0},
                           {QStringLiteral("community_metadata"), QJsonObject{{QStringLiteral("owner"), QStringLiteral("upstream")}}},
                           {QStringLiteral("steps"), QJsonArray{QJsonObject{
                               {QStringLiteral("name"), QStringLiteral("Pour")},
                               {QStringLiteral("pump"), QStringLiteral("flow")},
                               {QStringLiteral("flow"), 2.0},
                               {QStringLiteral("temperature"), 93.0}}}}};
        const Profile original = Profile::fromJson(QJsonDocument(source));
        bool parsed = false;
        const QVariantMap map = ProfileEditModel::toVariantMap(original);
        const Profile roundTrip = ProfileEditModel::fromVariantMap(map, &parsed);

        QVERIFY(parsed);
        QCOMPARE(roundTrip.toJsonObject().value(QStringLiteral("community_metadata")),
                 source.value(QStringLiteral("community_metadata")));
        QCOMPARE(roundTrip.steps().size(), original.steps().size());
        QCOMPARE(roundTrip.steps().first().name, original.steps().first().name);
        QCOMPARE(roundTrip.steps().first().flow, original.steps().first().flow);
    }

    void qmlProjectionKeepsNumericFieldsNumeric()
    {
        const QVariantMap map = ProfileEditModel::toVariantMap(dflowProfile());
        QCOMPARE(map.value(QStringLiteral("target_weight")).metaType().id(), QMetaType::Double);
        QCOMPARE(map.value(QStringLiteral("target_volume")).metaType().id(), QMetaType::Double);
        QCOMPARE(map.value(QStringLiteral("espresso_temperature")).metaType().id(), QMetaType::Double);
        QCOMPARE(map.value(QStringLiteral("target_weight")).toDouble(), 50.0);
        QCOMPARE(map.value(QStringLiteral("steps")).toList().first().toMap()
                     .value(QStringLiteral("flow")).metaType().id(), QMetaType::Double);
        const QJsonDocument source = QJsonDocument::fromJson(
            map.value(QStringLiteral("_profileJson")).toString().toUtf8());
        QVERIFY(source.object().value(QStringLiteral("target_weight")).isString());
    }

    void editorTypeIsDerivedFromTitleOrProfileContent_data()
    {
        QTest::addColumn<QString>("title");
        QTest::addColumn<QString>("profileType");
        QTest::addColumn<QString>("expectedEditorType");
        QTest::addColumn<QString>("transientEditorType");
        QTest::addColumn<QString>("transientEditorTypeSnakeCase");

        QTest::newRow("dflow-title-wins")
            << QStringLiteral("D-Flow test") << QStringLiteral("settings_2b")
            << QStringLiteral("dflow") << QStringLiteral("pressure") << QStringLiteral("flow");
        QTest::newRow("aflow-title-wins")
            << QStringLiteral("A-Flow test") << QStringLiteral("settings_2a")
            << QStringLiteral("aflow") << QStringLiteral("flow") << QStringLiteral("pressure");
        QTest::newRow("settings-2a-content")
            << QStringLiteral("Profile test") << QStringLiteral("settings_2a")
            << QStringLiteral("pressure") << QStringLiteral("dflow") << QStringLiteral("aflow");
        QTest::newRow("settings-2b-content")
            << QStringLiteral("Profile test") << QStringLiteral("settings_2b")
            << QStringLiteral("flow") << QStringLiteral("aflow") << QStringLiteral("dflow");
        QTest::newRow("settings-2c-content")
            << QStringLiteral("Profile test") << QStringLiteral("settings_2c")
            << QStringLiteral("advanced") << QStringLiteral("dflow") << QStringLiteral("aflow");
    }

    void editorTypeIsDerivedFromTitleOrProfileContent()
    {
        QFETCH(QString, title);
        QFETCH(QString, profileType);
        QFETCH(QString, expectedEditorType);
        QFETCH(QString, transientEditorType);
        QFETCH(QString, transientEditorTypeSnakeCase);

        const Profile original = profileWithTitleAndType(title, profileType);
        QCOMPARE(original.editorType(), expectedEditorType);

        for (const bool keepSourceJson : {false, true}) {
            QVariantMap input = ProfileEditModel::toVariantMap(original);
            if (!keepSourceJson)
                input.remove(QStringLiteral("_profileJson"));
            input.insert(QStringLiteral("editorType"), transientEditorType);
            input.insert(QStringLiteral("editor_type"), transientEditorTypeSnakeCase);

            bool parsed = false;
            const Profile roundTrip = ProfileEditModel::fromVariantMap(input, &parsed);
            QVERIFY(parsed);
            QCOMPARE(roundTrip.editorType(), expectedEditorType);

            const QJsonObject serialized = roundTrip.toJsonObject();
            QVERIFY(!serialized.contains(QStringLiteral("editorType")));
            QVERIFY(!serialized.contains(QStringLiteral("editor_type")));
        }
    }

    void parameterEditsPreserveUnrelatedFramesAndChangeGeneratedProfile()
    {
        const Profile original = dflowProfile();
        Profile targetOnly = original;
        QVariantMap params = ProfileEditModel::paramsForProfile(original);
        params.insert(QStringLiteral("targetWeight"), 42.0);
        ProfileEditModel::applyParams(targetOnly, params);

        QCOMPARE(targetOnly.targetWeight(), 42.0);
        QCOMPARE(targetOnly.steps().size(), original.steps().size());
        for (qsizetype i = 0; i < original.steps().size(); ++i)
            QCOMPARE(targetOnly.steps().at(i).toJson(), original.steps().at(i).toJson());
        QCOMPARE(original.targetWeight(), 50.0);

        Profile changed = original;
        const double originalPourFlow = original.steps().last().flow;
        params = ProfileEditModel::paramsForProfile(original);
        params.insert(QStringLiteral("pourFlow"), 3.25);
        ProfileEditModel::applyParams(changed, params);
        QVERIFY(changed.steps().last().toJson() != original.steps().last().toJson());
        QCOMPARE(original.steps().last().flow, originalPourFlow);
    }

    void editorDraftIsIsolatedAndRetainsItsSourceRevision()
    {
        const QVariantMap source = ProfileEditModel::toVariantMap(dflowProfile());
        const double sourceFlow = source.value(QStringLiteral("steps")).toList().first().toMap()
                                      .value(QStringLiteral("flow")).toDouble();
        ProfileEditorDraft draft;
        QVERIFY(draft.load(source, QStringLiteral("draft_test.json"), QStringLiteral("catalog-rev-17")));
        QVERIFY(!draft.modified());
        QCOMPARE(draft.sourceFilename(), QStringLiteral("draft_test.json"));
        QCOMPARE(draft.sourceRevision(), QStringLiteral("catalog-rev-17"));

        const QVariantMap before = draft.profile();
        QVERIFY(draft.setFrameProperty(0, QStringLiteral("flow"), 3.75));
        QVERIFY(draft.modified());
        QCOMPARE(draft.profile().value(QStringLiteral("steps")).toList().first().toMap()
                     .value(QStringLiteral("flow")).toDouble(), 3.75);
        QCOMPARE(before.value(QStringLiteral("steps")).toList().first().toMap()
                     .value(QStringLiteral("flow")).toDouble(), sourceFlow);
        QCOMPARE(draft.sourceRevision(), QStringLiteral("catalog-rev-17"));
        draft.markClean();
        QVERIFY(!draft.modified());
    }
};

QTEST_GUILESS_MAIN(tst_ProfileEditModel)
#include "tst_profileeditmodel.moc"
