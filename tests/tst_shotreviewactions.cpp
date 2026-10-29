#include <QtTest>

#include <QQmlEngine>
#include <QFile>
#include <QJSEngine>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QSignalSpy>
#include <QUuid>
#include <QUrl>

#include <utility>
#include <functional>

#include "remote/shotreviewactions.h"
#include "remote/shotreviewactions_qml.h"
#include "remote/shotreviewsessions.h"
#include "core/shotreviewbaseline.h"
#include "core/shotreviewproposal.h"
#include "core/shotreviewpersistentplan.h"
#include "rep_shotreviewactions_source.h"

class ReviewActionFixtureSource final : public ShotReviewActionStateSimpleSource
{
    Q_OBJECT
public:
    using ShotReviewActionStateSimpleSource::setDecentLastSerial;
    using ShotReviewActionStateSimpleSource::setRefractometerOwnerReviewId;
    using ShotReviewActionStateSimpleSource::setRefractometerMeasuring;
    struct Call {
        QString requestId;
        QString operation;
        QVariantMap arguments;
    };

    QList<Call> calls;

    void execute(QString requestId, QString operation, QVariantMap arguments) override
    {
        calls.append({std::move(requestId), std::move(operation), std::move(arguments)});
    }

    void complete(const Call& call, bool success = true)
    {
        emit actionCompleted(call.requestId, call.operation, success, {},
                             success ? QString() : QStringLiteral("fixture rejection"));
    }
};

class ShotReviewActionsTest final : public QObject
{
    Q_OBJECT
private:
    static QString id(quint64 value)
    {
        return QStringLiteral("00000000-0000-4000-8000-%1")
            .arg(value, 12, 16, QLatin1Char('0'));
    }

    static QString sourceFunction(const QString& source, const QString& name)
    {
        const QString needle = QStringLiteral("function ") + name + QLatin1Char('(');
        const qsizetype start = source.indexOf(needle);
        if (start < 0 || source.indexOf(needle, start + 1) >= 0)
            return {};
        int depth = 0;
        for (qsizetype i = source.indexOf(QLatin1Char('{'), start); i >= 0 && i < source.size(); ++i) {
            if (source[i] == QLatin1Char('{'))
                ++depth;
            else if (source[i] == QLatin1Char('}') && --depth == 0)
                return source.mid(start, i - start + 1);
        }
        return {};
    }

    static QString sourceBody(const QString& source, const QString& marker)
    {
        const qsizetype start = source.indexOf(marker);
        if (start < 0) return {};
        const qsizetype brace = source.indexOf(QLatin1Char('{'), start);
        int depth = 0;
        for (qsizetype i = brace; i >= 0 && i < source.size(); ++i) {
            if (source[i] == QLatin1Char('{')) ++depth;
            else if (source[i] == QLatin1Char('}') && --depth == 0)
                return source.mid(brace, i - brace + 1);
        }
        return {};
    }

private slots:
    void carryForwardSummaryRoundsWeights()
    {
        QFile file(QStringLiteral(DECENZA_SOURCE_DIR "/qml/pages/PostShotReviewPage.qml"));
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString source = QString::fromUtf8(file.readAll());
        QJSEngine engine;
        engine.evaluate(sourceFunction(source, QStringLiteral("carryForwardValue")));
        engine.evaluate(sourceFunction(source, QStringLiteral("carryForwardSummary")));
        const auto result = engine.evaluate(QStringLiteral(R"JS(
            var TranslationManager={translate:function(key,fallback){return fallback;}};
            var _carryProposal={
                changes:[{field:"targetWeight",before:60,after:59.10000000000001}],
                persistentEffects:[{record:"bag",field:"doseWeightG",before:20,after:19.700000000000003},
                                   {record:"equipment",field:"lastGrindSetting",before:"10",after:"11"},
                                   {record:"equipment",field:"lastRpm",before:600,after:700}],
                bagName:"Gran Bar",equipmentName:"Niche Zero"
            };
            carryForwardSummary();
        )JS"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        QCOMPARE(result.toString(), QStringLiteral("Stop at (ratio unchanged): 60 g → 59.1 g\nSaved bag “Gran Bar”: Dose 20 g → 19.7 g\nSaved equipment “Niche Zero”: Grind 10 → 11\nSaved equipment “Niche Zero”: RPM 600 → 700"));
    }
    void reviewLeaveWaitsForSavesAndResumesOnce()
    {
        QFile file(QStringLiteral(DECENZA_SOURCE_DIR "/qml/pages/PostShotReviewPage.qml"));
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString source = QString::fromUtf8(file.readAll());
        QJSEngine engine;
        for (const QString& name : {QStringLiteral("requestReviewLeave"), QStringLiteral("_continueReviewLeave"),
                                    QStringLiteral("_finishReviewLeave"), QStringLiteral("_abandonCarryForward")}) {
            const auto loaded = engine.evaluate(sourceFunction(source, name));
            QVERIFY2(!loaded.isError(), qPrintable(loaded.toString()));
        }
        auto result = engine.evaluate(QStringLiteral(R"JS(
            var _carryLeaveIntent=-1, _carryPreviewRequest="", _carryApplyRequest="", _carryProposal={};
            var _reviewActive=true;
            var _pendingMetadataWrites={save:{}}, _bagSelectionPending=false, _saveFailed=false;
            var editShotId=5, _editLoaded=true, _leasedClientId="client", _reviewSessionId="review";
            var _reviewOpeningValues={doseWeight:18};
            var previews=0, backs=0, steps=[];
            var carryForwardDialog={opened:false, close:function(){this.opened=false;}};
            var HistoryService={lastSavedShotId:5};
            var ShotReviewActions={ready:true,executeFileOperation:function(){++previews;return "preview";}};
            var AppShell={backRequested:function(){++backs;}};
            function autosave() {}
            function captureCarryForwardState(){return {doseWeight:19};}
            function _performStep(index){steps.push(index);}
            requestReviewLeave(-2);
            JSON.stringify([previews,backs]);
        )JS"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        QCOMPARE(result.toString(), QStringLiteral("[0,0]"));
        result = engine.evaluate(QStringLiteral("_pendingMetadataWrites={}; _continueReviewLeave(); _continueReviewLeave(); previews"));
        QCOMPARE(result.toInt(), 1);
        QCOMPARE(engine.evaluate(QStringLiteral("backs")).toInt(), 0);
        engine.evaluate(QStringLiteral("_finishReviewLeave(); _finishReviewLeave();"));
        QCOMPARE(engine.evaluate(QStringLiteral("backs")).toInt(), 1);
        engine.evaluate(QStringLiteral("HistoryService.lastSavedShotId=6; requestReviewLeave(2);"));
        QCOMPARE(engine.evaluate(QStringLiteral("JSON.stringify(steps)")).toString(), QStringLiteral("[2]"));
        QCOMPARE(engine.evaluate(QStringLiteral("previews")).toInt(), 1);
        engine.evaluate(QStringLiteral("_reviewActive=false; _carryLeaveIntent=-2; _finishReviewLeave();"));
        QCOMPARE(engine.evaluate(QStringLiteral("backs")).toInt(), 1);
        // Reconnecting may reacquire the upload lease, but must not revive
        // carry-forward edits from the interrupted review.
        const qsizetype carryConnections = source.indexOf(QStringLiteral("function onCarryForwardInvalidated("));
        const qsizetype actionHandler = source.indexOf(QStringLiteral("function onActionCompleted("), carryConnections);
        const auto stateHandler = sourceFunction(source.mid(carryConnections, actionHandler - carryConnections),
                                                  QStringLiteral("onStateChanged"));
        QVERIFY(!stateHandler.isEmpty());
        engine.evaluate(stateHandler);
        engine.evaluate(QStringLiteral("var postShotReviewPage=this; _reviewActive=true; _carryLeaveIntent=-2; _carryProposal={proposalId:'old'}; ShotReviewActions.ready=false; onStateChanged();"));
        QCOMPARE(engine.evaluate(QStringLiteral("Object.keys(_reviewOpeningValues).length")).toInt(), 0);
        engine.evaluate(QStringLiteral("ShotReviewActions.ready=true; onStateChanged(); HistoryService.lastSavedShotId=5; requestReviewLeave(-2);"));
        QCOMPARE(engine.evaluate(QStringLiteral("previews")).toInt(), 1);
        QCOMPARE(engine.evaluate(QStringLiteral("backs")).toInt(), 2);
    }
    void automaticBeanEnrichmentIsNotACarryForwardEdit()
    {
        QFile file(QStringLiteral(DECENZA_SOURCE_DIR "/qml/pages/PostShotReviewPage.qml"));
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString source = QString::fromUtf8(file.readAll());
        QJSEngine engine;
        engine.evaluate(sourceFunction(source, QStringLiteral("onCanonicalDetails")));
        auto result = engine.evaluate(QStringLiteral(R"JS(
            var original=JSON.stringify({id:"bean",source:"visualizer"});
            var saves=0;
            var postShotReviewPage={
                beanBaseLinked:true,activeBeanBase:{id:"bean"},editBeanBaseJson:original,editRoastLevel:"",
                _reviewOpeningValues:{beanBaseJson:original,roastLevel:""},autosave:function(){++saves;}
            };
            onCanonicalDetails("bean",{origin:"Brazil",degree:"Dark"});
            JSON.stringify([postShotReviewPage._reviewOpeningValues.beanBaseJson===postShotReviewPage.editBeanBaseJson,
                            postShotReviewPage._reviewOpeningValues.roastLevel,saves]);
        )JS"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        QCOMPARE(result.toString(), QStringLiteral("[true,\"Dark\",1]"));
        // Enriching an explicitly selected bean must not erase that selection
        // from the user's net corrections.
        result = engine.evaluate(QStringLiteral(R"JS(
            postShotReviewPage._reviewOpeningValues={beanBaseJson:original,roastLevel:""};
            onCanonicalDetails("bean",{process:"Washed"});
            JSON.stringify([postShotReviewPage._reviewOpeningValues.beanBaseJson===original,
                            postShotReviewPage._reviewOpeningValues.roastLevel,saves]);
        )JS"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        QCOMPARE(result.toString(), QStringLiteral("[true,\"\",2]"));
    }
    void reviewPersistentPlanDoesNotImportBagSettings()
    {
        ShotReviewSetup setup;
        setup.brew.activeBagId = 1;
        setup.brew.activeRecipeId = 7;
        setup.brew.activeEquipmentId = 9;
        const QVariantMap recipe{{"id", 7}, {"name", "Morning"}, {"bagId", 1},
                                 {"doseG", 19.0}, {"grindPinned", "10"},
                                 {"profileTitle", "Profile"}, {"drinkType", "espresso"}};
        const QVariantMap bag{{"id", 2}, {"doseWeightG", 16.0},
                              {"grinderSetting", "3"}, {"yieldValue", 39.0},
                              {"yieldMode", "absolute"}};
        const QVariantMap equipment{{"id", 9}, {"name", "Niche Zero"},
                                     {"lastGrindSetting", "10"}, {"lastRpm", 600}};
        QVariantMap plan = shotReviewPersistentPlan(setup, {{"bagId", 2}}, recipe, bag);
        QVERIFY(plan.value("valid").toBool());
        QVERIFY(plan.value("bagPatch").toMap().isEmpty());
        QCOMPARE(plan.value("recipePatch").toMap(), (QVariantMap{{"bagId", 2}}));
        QCOMPARE(plan.value("effects").toList().size(), 1);
        QVERIFY(plan.value("equipmentPatch").toMap().isEmpty());

        plan = shotReviewPersistentPlan(setup, {{"bagId", 2}, {"doseWeight", 20.0},
                                                {"grinderSetting", "11"}, {"rpm", 700}}, recipe, bag, equipment);
        QVERIFY(plan.value("valid").toBool());
        QCOMPARE(plan.value("equipmentPatch").toMap(), (QVariantMap{{"lastGrindSetting", "11"}, {"lastRpm", 700}}));
        QCOMPARE(plan.value("equipmentId").toInt(), 9);
        int equipmentEffects = 0;
        for (const auto& effect : plan.value("effects").toList())
            if (effect.toMap().value("record") == "equipment") ++equipmentEffects;
        QCOMPARE(equipmentEffects, 2);
        const auto bagPatch = plan.value("bagPatch").toMap();
        const auto recipePatch = plan.value("recipePatch").toMap();
        QCOMPARE(bagPatch.value("doseWeightG").toDouble(), 20.0);
        QCOMPARE(bagPatch.value("grinderSetting").toString(), QStringLiteral("11"));
        QVERIFY(!bagPatch.contains("yieldValue"));
        QVERIFY(!bagPatch.contains("yieldMode"));
        QCOMPARE(recipePatch.value("doseG").toDouble(), 20.0);
        QCOMPARE(recipePatch.value("grindPinned").toString(), QStringLiteral("11"));
        QVERIFY(!recipePatch.contains("yieldG"));
        QVERIFY(!shotReviewPersistentPlan(setup, {{"bagId", 2}}, recipe, {}).value("valid").toBool());
        QVERIFY(!shotReviewPersistentPlan(setup, {{"grinderSetting", "11"}}, recipe, bag).value("valid").toBool());
        setup.brew.activeEquipmentId = -1;
        plan = shotReviewPersistentPlan(setup, {{"bagId", 2}, {"rpm", 700}}, recipe, bag);
        QVERIFY(plan.value("valid").toBool());
        QVERIFY(plan.value("equipmentPatch").toMap().isEmpty());
    }
    void reviewProposalIncludesOnlyNetCorrections()
    {
        ShotReviewSetup setup;
        setup.brew.activeBagId = 1;
        setup.brew.doseG = 18;
        setup.brew.yieldMode = QStringLiteral("ratio");
        setup.brew.yieldValue = 3;
        setup.brew.profileTargetWeightG = 36;
        setup.brew.grindSetting = QStringLiteral("10");
        setup.targetG = 54;
        const QVariantMap opening{{"bagId", 1}, {"doseWeight", 18},
                                  {"grinderSetting", "10"}, {"notes", "old"},
                                  {"beanBrand", ""}, {"beanType", ""},
                                  {"roastDate", ""}, {"roastLevel", ""},
                                  {"beanBaseId", ""}, {"beanBaseJson", ""},
                                  {"rpm", 0}, {"equipmentId", -1}, {"barista", ""}};
        auto saved = opening;
        saved.insert("notes", "new");
        QVERIFY(shotReviewProposal(setup, opening, saved).value("patch").toMap().isEmpty());
        saved.insert("bagId", 2);
        saved.insert("doseWeight", 19);
        saved.insert("grinderSetting", "11");
        const QVariantMap proposed = shotReviewProposal(setup, opening, saved);
        QVERIFY(proposed.value("valid").toBool());
        const QVariantMap patch = proposed.value("patch").toMap();
        QCOMPARE(patch.size(), 3);
        QCOMPARE(patch.value("bagId").toInt(), 2);
        QCOMPARE(patch.value("doseWeight").toDouble(), 19.0);
        QCOMPARE(patch.value("grinderSetting").toString(), QStringLiteral("11"));
        const auto changes = proposed.value("changes").toList();
        QCOMPARE(changes.size(), 4);
        const QVariantMap target = changes.last().toMap();
        QCOMPARE(target.value("field").toString(), QStringLiteral("targetWeight"));
        QCOMPARE(target.value("before").toDouble(), 54.0);
        QCOMPARE(target.value("after").toDouble(), 57.0);
        saved.insert("grinderSetting", "10");
        QVERIFY(!shotReviewProposal(setup, opening, saved).value("patch").toMap().contains("grinderSetting"));
        saved = opening;
        saved.insert("bagId", 2);
        const QVariantMap beanOnly = shotReviewProposal(setup, opening, saved).value("patch").toMap();
        QCOMPARE(beanOnly.size(), 1);
        QVERIFY(beanOnly.contains("bagId"));
        saved.insert("doseWeight", -1);
        QVERIFY(!shotReviewProposal(setup, opening, saved).value("valid").toBool());
        saved = opening;
        saved.remove("grinderSetting");
        QVERIFY(!shotReviewProposal(setup, opening, saved).value("valid").toBool());
    }
    void reviewBaselineTracksShotStartAndRestoredValues()
    {
        ShotReviewSetup start;
        start.brew.activeBagId = 3;
        start.brew.activeRecipeId = 4;
        start.brew.activeEquipmentId = 5;
        start.brew.profileTitle = QStringLiteral("Profile");
        start.brew.baseProfileName = QStringLiteral("Base");
        start.brew.profileContentSha256 = QStringLiteral("profile content");
        start.brew.doseG = 18;
        start.brew.grindSetting = QStringLiteral("10");
        start.brew.grindRpm = 600;
        start.brew.yieldMode = QStringLiteral("ratio");
        start.brew.yieldValue = 2;
        start.brew.profileTemperatureC = 93;
        start.targetG = 36;
        start.beanBrand = QStringLiteral("Roaster");
        start.beanType = QStringLiteral("Coffee");
        start.roastDate = QStringLiteral("2026-10-01");
        start.roastLevel = QStringLiteral("medium");
        start.beanBaseId = QStringLiteral("42");
        start.beanBaseData = QStringLiteral("{}");
        start.barista = QStringLiteral("Barista");
        QVERIFY(start.matches(start));
        auto changed = start;
        changed.brew.yieldMode = QStringLiteral("absolute");
        changed.brew.yieldValue = 36;
        QVERIFY(!start.matches(changed));
        changed = start; changed.brew.grindSetting = QStringLiteral("11");
        QVERIFY(!start.matches(changed));
        changed.brew.grindSetting = start.brew.grindSetting;
        QVERIFY(start.matches(changed));
        changed.brew.profileTemperatureC = 94;
        QVERIFY(!start.matches(changed));
        changed = start; changed.brew.profileContentSha256 = QStringLiteral("other");
        QVERIFY(!start.matches(changed));
        changed = start; changed.targetG = 37;
        QVERIFY(!start.matches(changed));
        changed = start; changed.beanBaseId = QStringLiteral("43");
        QVERIFY(!start.matches(changed));
        changed = start; changed.brew.doseG += 0.001;
        QVERIFY(start.matches(changed));
        const QList<std::function<void(ShotReviewSetup&)>> changes = {
            [](auto& s) { ++s.brew.activeBagId; },
            [](auto& s) { ++s.brew.activeRecipeId; },
            [](auto& s) { ++s.brew.activeEquipmentId; },
            [](auto& s) { s.brew.profileTitle += QLatin1Char('x'); },
            [](auto& s) { s.brew.baseProfileName += QLatin1Char('x'); },
            [](auto& s) { s.brew.doseG += 1; },
            [](auto& s) { ++s.brew.grindRpm; },
            [](auto& s) { s.brew.yieldValue += 1; },
            [](auto& s) { s.beanBrand += QLatin1Char('x'); },
            [](auto& s) { s.beanType += QLatin1Char('x'); },
            [](auto& s) { s.roastDate += QLatin1Char('x'); },
            [](auto& s) { s.roastLevel += QLatin1Char('x'); },
            [](auto& s) { s.beanBaseData += QLatin1Char('x'); },
            [](auto& s) { s.barista += QLatin1Char('x'); }
        };
        for (const auto& mutate : changes) {
            auto candidate = start;
            mutate(candidate);
            QVERIFY(!start.matches(candidate));
        }
        auto defaults = start;
        defaults.brew.yieldMode.clear();
        defaults.brew.yieldValue = 0;
        auto normalized = defaults;
        normalized.brew.yieldMode = QStringLiteral("none");
        normalized.brew.yieldValue = 42;
        QVERIFY(defaults.matches(normalized));
        normalized.brew.hasTemperatureOverride = true;
        normalized.brew.temperatureC = defaults.brew.profileTemperatureC;
        QVERIFY(defaults.matches(normalized));

        ShotReviewBaselines baselines(12);
        QVERIFY(baselines.forReview(12, changed)->matches(changed));
        baselines.started(1, start);
        changed.brew.doseG = 19;
        baselines.started(2, changed);
        baselines.saved(13, baselines.take(1));
        QVERIFY(baselines.forReview(13, changed)->matches(start));
        QVERIFY(!baselines.forReview(12, changed));
        baselines.saved(14, baselines.take(2));
        QVERIFY(baselines.forReview(14, start)->matches(changed));
        baselines.saved(15, std::nullopt);
        QVERIFY(!baselines.forReview(15, start));
    }
    void init() { QTest::failOnWarning(); }

    void reviewLeasesAreBoundToClientViewAndShot()
    {
        ShotReviewSessions sessions;
        sessions.openClient(id(1));
        sessions.openClient(id(2));
        QCOMPARE(sessions.hold(id(1), id(101), 11), ShotReviewSessions::Hold::FirstForShot);
        QCOMPARE(sessions.hold(id(2), id(102), 22), ShotReviewSessions::Hold::FirstForShot);
        QCOMPARE(sessions.hold(id(1), id(101), 11), ShotReviewSessions::Hold::Existing);
        QCOMPARE(sessions.hold(id(1), id(101), 22), ShotReviewSessions::Hold::Rejected);
        QCOMPARE(sessions.hold(id(2), id(101), 11), ShotReviewSessions::Hold::Rejected);
        QVERIFY(sessions.owns(id(1), id(101), 11));
        QVERIFY(!sessions.owns(id(1), id(101), 22));
        QVERIFY(!sessions.owns(id(2), id(101), 11));
        QVERIFY(!sessions.hasActiveReviews());

        QVERIFY(sessions.setActive(id(1), id(101), true));
        QCOMPARE(sessions.measurementOwner(), id(101));
        QVERIFY(sessions.setActive(id(2), id(102), true));
        QCOMPARE(sessions.measurementOwner(), id(101));
        QVERIFY(sessions.setActive(id(1), id(101), false));
        QCOMPARE(sessions.measurementOwner(), id(102));
        QCOMPARE(sessions.release(id(2), id(101)), qint64(0));
        QCOMPARE(sessions.release(id(1), id(101)), qint64(11));
        QCOMPARE(sessions.release(id(2), id(102)), qint64(22));
        QVERIFY(!sessions.hasActiveReviews());
        QVERIFY(sessions.measurementOwner().isEmpty());
    }

    void sharedShotReleasesOnlyAfterTheFinalView()
    {
        ShotReviewSessions sessions;
        sessions.openClient(id(1));
        sessions.openClient(id(2));
        QCOMPARE(sessions.hold(id(1), id(101), 11), ShotReviewSessions::Hold::FirstForShot);
        QCOMPARE(sessions.hold(id(1), id(102), 11), ShotReviewSessions::Hold::AdditionalForShot);
        QCOMPARE(sessions.hold(id(2), id(103), 11), ShotReviewSessions::Hold::AdditionalForShot);
        QCOMPARE(sessions.release(id(1), id(101)), qint64(0));
        QVERIFY(sessions.closeClient(id(1)).isEmpty());
        QVERIFY(sessions.owns(id(2), id(103), 11));
        QCOMPARE(sessions.closeClient(id(2)), QList<qint64>{11});
        QVERIFY(sessions.closeClient(id(2)).isEmpty());
        QCOMPARE(sessions.hold(id(1), id(104), 11), ShotReviewSessions::Hold::Rejected);
        QVERIFY(!sessions.setActive(id(1), id(102), true));
        QVERIFY(!sessions.beginMeasurement(id(1), id(102)));
    }

    void nativeReviewHasIdlePreferenceButExplicitMeasurementClaimsItsView()
    {
        ShotReviewSessions sessions;
        sessions.openClient(id(1));
        sessions.openClient(id(2), true);
        QCOMPARE(sessions.hold(id(1), id(300), 11), ShotReviewSessions::Hold::FirstForShot);
        QCOMPARE(sessions.hold(id(1), id(200), 22), ShotReviewSessions::Hold::FirstForShot);
        QCOMPARE(sessions.hold(id(2), id(400), 33), ShotReviewSessions::Hold::FirstForShot);
        QVERIFY(sessions.setActive(id(1), id(300), true));
        QVERIFY(sessions.setActive(id(1), id(200), true));
        QCOMPARE(sessions.measurementOwner(), id(300));
        QVERIFY(sessions.setActive(id(2), id(400), true));
        QCOMPARE(sessions.measurementOwner(), id(400));

        QVERIFY(sessions.beginMeasurement(id(1), id(200)));
        QCOMPARE(sessions.measurementOwner(), id(200));
        QVERIFY(sessions.measuring());
        sessions.noteMeasurementStarted();
        QVERIFY(!sessions.beginMeasurement(id(2), id(400)));
        QVERIFY(!sessions.beginMeasurement(id(1), id(200)));
        QVERIFY(sessions.setActive(id(1), id(200), false));
        QCOMPARE(sessions.measurementOwner(), id(200));
        sessions.finishMeasurement();
        QVERIFY(!sessions.measuring());
        QCOMPARE(sessions.measurementOwner(), id(400));
        QVERIFY(!sessions.beginMeasurement(id(1), id(200)));
    }

    void leavingObserversCannotRetargetAnInflightReading()
    {
        ShotReviewSessions sessions;
        sessions.openClient(id(1));
        sessions.openClient(id(2));
        sessions.hold(id(1), id(101), 11);
        sessions.hold(id(2), id(102), 22);
        QVERIFY(sessions.setActive(id(1), id(101), true));
        QVERIFY(sessions.setActive(id(2), id(102), true));
        QVERIFY(sessions.beginMeasurement(id(1), id(101)));
        QVERIFY(sessions.setActive(id(2), id(102), false));
        QCOMPARE(sessions.release(id(2), id(102)), qint64(22));
        QCOMPARE(sessions.measurementOwner(), id(101));
        sessions.hold(id(2), id(103), 33);
        QVERIFY(sessions.setActive(id(2), id(103), true));

        QCOMPARE(sessions.closeClient(id(1)), QList<qint64>{11});
        QVERIFY(!sessions.owns(id(1), id(101)));
        QVERIFY(sessions.measuring());
        QCOMPARE(sessions.measurementOwner(), id(101));
        // Reserving the departed owner ID also prevents a new shot in that view
        // from inheriting a result already being measured for the old shot.
        QCOMPARE(sessions.hold(id(2), id(101), 44), ShotReviewSessions::Hold::Rejected);
        QVERIFY(!sessions.beginMeasurement(id(2), id(103)));
        sessions.finishMeasurement();
        QCOMPARE(sessions.measurementOwner(), id(103));
        QVERIFY(sessions.beginMeasurement(id(2), id(103)));
    }

    void physicalMeasurementWithoutAnOwnerStaysUnassignedUntilItFinishes()
    {
        ShotReviewSessions sessions;
        sessions.noteMeasurementStarted();
        QVERIFY(sessions.measuring());
        QVERIFY(sessions.measurementOwner().isEmpty());
        sessions.openClient(id(1), true);
        sessions.hold(id(1), id(101), 11);
        QVERIFY(sessions.setActive(id(1), id(101), true));
        QVERIFY(sessions.measurementOwner().isEmpty());
        sessions.finishMeasurement();
        QCOMPARE(sessions.measurementOwner(), id(101));
        sessions.noteMeasurementStarted();
        QCOMPARE(sessions.closeClient(id(1)), QList<qint64>{11});
        QCOMPARE(sessions.measurementOwner(), id(101));
        sessions.finishMeasurement();
        QVERIFY(sessions.measurementOwner().isEmpty());
    }

    void replacementOwnerIsDeterministic()
    {
        ShotReviewSessions sessions;
        sessions.openClient(id(1));
        for (quint64 view : {300, 200, 100}) {
            sessions.hold(id(1), id(view), static_cast<qint64>(view));
            QVERIFY(sessions.setActive(id(1), id(view), true));
        }
        QCOMPARE(sessions.measurementOwner(), id(300));
        QCOMPARE(sessions.release(id(1), id(300)), qint64(300));
        QCOMPARE(sessions.measurementOwner(), id(100));
    }

    void leaseLimitsRejectFreshRequestsAndAllowCapacityReuse()
    {
        ShotReviewSessions sessions;
        sessions.openClient(id(1));
        for (quint64 i = 0; i < 16; ++i)
            QCOMPARE(sessions.hold(id(1), id(100 + i), 11),
                     i == 0 ? ShotReviewSessions::Hold::FirstForShot
                            : ShotReviewSessions::Hold::AdditionalForShot);
        QCOMPARE(sessions.hold(id(1), id(200), 11), ShotReviewSessions::Hold::Rejected);
        QCOMPARE(sessions.hold(id(1), id(100), 11), ShotReviewSessions::Hold::Existing);
        QCOMPARE(sessions.release(id(1), id(100)), qint64(0));
        QCOMPARE(sessions.hold(id(1), id(200), 11), ShotReviewSessions::Hold::AdditionalForShot);

        ShotReviewSessions full;
        for (quint64 client = 1; client <= 8; ++client) {
            full.openClient(id(client));
            for (quint64 i = 0; i < 16; ++i)
                QVERIFY(full.hold(id(client), id(1000 + client * 16 + i), 11)
                        != ShotReviewSessions::Hold::Rejected);
        }
        full.openClient(id(9));
        QCOMPARE(full.hold(id(9), id(2000), 11), ShotReviewSessions::Hold::Rejected);
        QVERIFY(full.closeClient(id(1)).isEmpty());
        QCOMPARE(full.hold(id(9), id(2000), 11), ShotReviewSessions::Hold::AdditionalForShot);
    }

    void malformedAndUnregisteredReviewRequestsAreRejected()
    {
        ShotReviewSessions sessions;
        sessions.openClient(QStringLiteral("not-a-client"));
        QCOMPARE(sessions.hold(id(1), id(101), 11), ShotReviewSessions::Hold::Rejected);
        sessions.openClient(id(1));
        QCOMPARE(sessions.hold(id(1), QStringLiteral("not-a-view"), 11), ShotReviewSessions::Hold::Rejected);
        QCOMPARE(sessions.hold(id(1), id(101), 0), ShotReviewSessions::Hold::Rejected);
        QCOMPARE(sessions.hold(id(1), id(101), -1), ShotReviewSessions::Hold::Rejected);
        QVERIFY(!sessions.setActive(id(1), id(101), true));
        QVERIFY(!sessions.beginMeasurement(id(1), id(101)));
    }

    void shippingReadingHandlersConsumeOnlyTheirTargetView()
    {
        QFile file(QStringLiteral(DECENZA_SOURCE_DIR "/qml/pages/PostShotReviewPage.qml"));
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(file.errorString()));
        const QString source = QString::fromUtf8(file.readAll());
        QJSEngine engine;
        for (const QString& name : {QStringLiteral("onRefractometerReading"),
                                    QStringLiteral("onRefractometerAverageProgress"),
                                    QStringLiteral("onRefractometerMeasurementComplete")}) {
            const QString function = sourceFunction(source, name);
            QVERIFY2(!function.isEmpty(), qPrintable(name));
            const QJSValue loaded = engine.evaluate(function);
            QVERIFY2(!loaded.isError(), qPrintable(loaded.toString()));
        }
        const qsizetype handlerAt = source.indexOf(QStringLiteral("function onRefractometerReading("));
        const qsizetype enabledAt = source.lastIndexOf(QStringLiteral("enabled:"), handlerAt);
        QVERIFY(enabledAt >= 0);
        const QString enabled = source.mid(enabledAt + 8, handlerAt - enabledAt - 8).trimmed();
        QJSValue result = engine.evaluate(QStringLiteral("function deliverTds(tds) { if (")
            + enabled + QStringLiteral(") onRefractometerReading(ShotReviewActions.refractometerOwnerReviewId, tds); }"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        result = engine.evaluate(QStringLiteral(R"JS(
            var ShotReviewActions = {
                refractometerOwnerReviewId: "view-a", refractometerMeasuring: true,
                refractometerConnected: true
            };
            var WebDebugLogger = { debug: function() {} };
            function makeView(viewId, shotId) {
                return {
                    _reviewSessionId: viewId, editShotId: shotId,
                    visible: true, isEditMode: true,
                    kMinimumPlausibleTds: 3, kMaximumPlausibleTds: 35,
                    editDrinkTds: 0, r2CommitPending: false,
                    avgDone: 0, avgTotal: 0, commitCalls: 0, writes: 0,
                    calculateEy: function() {},
                    commitPendingR2Reading: function() {
                        ++this.commitCalls;
                        if (this.r2CommitPending) { ++this.writes; this.r2CommitPending = false; }
                    }
                };
            }
            var views = [makeView("view-a", 101), makeView("view-b", 202)];
            var postShotReviewPage;
            function both(action) {
                views.forEach(function(view) { postShotReviewPage = view; action(); });
            }
            both(function() { deliverTds(7.5); });
            both(function() { onRefractometerAverageProgress("view-a", 1, 3); });
        )JS"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        const auto run = [&engine](const QString& script) { return engine.evaluate(script); };
        QCOMPARE(run(QStringLiteral("views[0].editDrinkTds")).toNumber(), 7.5);
        QCOMPARE(run(QStringLiteral("views[1].editDrinkTds")).toNumber(), 0.0);
        QCOMPARE(run(QStringLiteral("views[0].avgDone")).toInt(), 1);
        QCOMPARE(run(QStringLiteral("views[1].avgDone")).toInt(), 0);
        // Exercise the shipping header label too: receiving a live value is not
        // enough if the measuring/progress label hides it throughout the run.
        const QString button = source.mid(source.indexOf(QStringLiteral("id: readTdsButton")));
        const QString label = sourceBody(button, QStringLiteral("text:"));
        QVERIFY(!label.isEmpty());
        result = run(QStringLiteral("function liveTdsLabel() ") + label + QStringLiteral(R"JS(
            var TranslationManager = { translate: function(key, fallback) { return fallback; } };
            postShotReviewPage = views[0];
            var readTdsButton = { refMeasuring: true, refConnected: true, tdsPlausible: true,
                get tdsValue() { return postShotReviewPage.editDrinkTds; } };
        )JS"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        QCOMPARE(run(QStringLiteral("liveTdsLabel()")).toString(), QStringLiteral("7.50% · 1/3"));
        run(QStringLiteral("postShotReviewPage.avgTotal = 0; deliverTds(7.6);"));
        QCOMPARE(run(QStringLiteral("liveTdsLabel()")).toString(), QStringLiteral("7.60%"));
        QCOMPARE(run(QStringLiteral("views[0].writes")).toInt(), 0);
        result = run(QStringLiteral("both(function() { onRefractometerMeasurementComplete('view-a'); });"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        QCOMPARE(run(QStringLiteral("views[0].writes")).toInt(), 1);
        QCOMPARE(run(QStringLiteral("views[1].commitCalls")).toInt(), 0);

        result = run(QStringLiteral("ShotReviewActions.refractometerOwnerReviewId = 'view-b'; both(function() { deliverTds(8.0); }); both(function() { onRefractometerMeasurementComplete('view-a'); });"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        QCOMPARE(run(QStringLiteral("views[0].editDrinkTds")).toNumber(), 7.6);
        QCOMPARE(run(QStringLiteral("views[1].editDrinkTds")).toNumber(), 8.0);
        QCOMPARE(run(QStringLiteral("views[1].writes")).toInt(), 0);
        QVERIFY(run(QStringLiteral("views[1].r2CommitPending")).toBool());
        result = run(QStringLiteral("both(function() { onRefractometerMeasurementComplete('view-b'); });"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        QCOMPARE(run(QStringLiteral("views[0].writes")).toInt(), 1);
        QCOMPARE(run(QStringLiteral("views[1].writes")).toInt(), 1);
    }

    void reviewBeanSelectionDoesNotActivateBag()
    {
        QFile picker(QStringLiteral(DECENZA_SOURCE_DIR "/qml/components/ChangeBeansDialog.qml"));
        QVERIFY(picker.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString selection = sourceFunction(QString::fromUtf8(picker.readAll()),
                                                 QStringLiteral("applySelection"));
        QVERIFY(!selection.isEmpty());
        QJSEngine engine;
        QJSValue result = engine.evaluate(selection + QStringLiteral(R"JS(
            var root = {context: "postShot", activateOnSave: true};
            var _selectionPending = false, _selectionBagId = -1, _selectionBag = {}, errorMessage = "";
            var writes = 0, activations = 0, finished = 0;
            var nextShot = {bag:3, recipe:9, dose:19, grind:"10", yieldMode:"ratio", yieldValue:3};
            var BagService = {requestActivateBag: function(id) {
                ++activations; nextShot.bag = id; nextShot.recipe = -1; nextShot.yieldMode = "none";
            }};
            function updateShotSnapshot(id, bag) { ++writes; return "pending"; }
            function finishSelection() { ++finished; }
        )JS"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        for (const QString& context : {QStringLiteral("postShot"), QStringLiteral("historicalShot")}) {
            result = engine.evaluate(QStringLiteral("root.context = '%1'; _selectionPending = false; applySelection(42, {}, false); [writes, activations, finished].join(',')").arg(context));
            QVERIFY2(!result.isError(), qPrintable(result.toString()));
            QCOMPARE(result.toString(), context == QStringLiteral("postShot")
                     ? QStringLiteral("1,0,0") : QStringLiteral("2,0,0"));
            QCOMPARE(engine.evaluate(QStringLiteral("JSON.stringify(nextShot)")).toString(),
                     QStringLiteral("{\"bag\":3,\"recipe\":9,\"dose\":19,\"grind\":\"10\",\"yieldMode\":\"ratio\",\"yieldValue\":3}"));
        }

        QFile review(QStringLiteral(DECENZA_SOURCE_DIR "/qml/pages/PostShotReviewPage.qml"));
        QVERIFY(review.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString save = sourceFunction(QString::fromUtf8(review.readAll()), QStringLiteral("saveEditedShot"));
        QVERIFY(!save.isEmpty());
        QVERIFY(!save.contains(QStringLiteral("applyShotBaseline")));
        QVERIFY(!save.contains(QStringLiteral("runStickySync")));
    }

    void createdReviewBagWaitsForShotUpdate()
    {
        QFile picker(QStringLiteral(DECENZA_SOURCE_DIR "/qml/components/ChangeBeansDialog.qml"));
        QVERIFY(picker.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString handler = sourceFunction(QString::fromUtf8(picker.readAll()), QStringLiteral("onBagCreated"));
        QVERIFY(!handler.isEmpty());
        QJSEngine engine;
        auto result = engine.evaluate(handler + QStringLiteral(R"JS(
            var selections = 0, closed = 0, recipeActions = 0;
            var root = {_awaitingCreate:true, _extractedImageUrl:"", fBeanBaseId:"",
                        fLink:"", fFreeze:false, _suggestionsShown:true, context:"postShot",
                        applySelection:function() {++selections;}, close:function(){++closed;}};
            var BagService = {activeBagId:1};
            var newBagSuggestions = {pendingAction:"apply", runPending:function(){++recipeActions;}};
            onBagCreated(42, {coffeeName:"Coffee"});
            JSON.stringify([selections, closed, recipeActions]);
        )JS"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        QCOMPARE(result.toString(), QStringLiteral("[1,0,0]"));
    }

    void reviewBagWriteCarriesTheUploadLease()
    {
        QFile picker(QStringLiteral(DECENZA_SOURCE_DIR "/qml/components/ChangeBeansDialog.qml"));
        QVERIFY(picker.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString function = sourceFunction(QString::fromUtf8(picker.readAll()), QStringLiteral("updateShotSnapshot"));
        QVERIFY(!function.isEmpty());
        QJSEngine engine;
        auto result = engine.evaluate(function + QStringLiteral(R"JS(
            var root = {shotId: 12, reviewSessionId: "review-1"};
            var _shotUpdateRequest = "", _selectionPending = true, errorMessage = "";
            var sent;
            var ShotReviewActions = {executeFileOperation: function(op, args) {
                sent = {op:op, args:args}; return "write-1";
            }};
            updateShotSnapshot(42, {});
            JSON.stringify([_shotUpdateRequest, sent.op, sent.args.reviewId, sent.args.shotId, sent.args.metadata.bagId]);
        )JS"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        QCOMPARE(result.toString(), QStringLiteral("[\"write-1\",\"updateReviewShot\",\"review-1\",12,42]"));
    }

    void carryForwardOpeningStateSurvivesLaterEdits()
    {
        QFile review(QStringLiteral(DECENZA_SOURCE_DIR "/qml/pages/PostShotReviewPage.qml"));
        QVERIFY(review.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString source = QString::fromUtf8(review.readAll());
        QJSEngine engine;
        auto result = engine.evaluate(sourceFunction(source, QStringLiteral("captureCarryForwardState"))
            + QStringLiteral(R"JS(
                var editShotData = {bagId: 5}, beanBaseLinked = true, activeBeanBase = {id: 42};
                var dose = 18, grind = "10";
                function captureEditState() { return {doseWeight: dose, grinderSetting: grind}; }
                var opening = captureCarryForwardState();
                dose = 19; grind = "11"; editShotData = {bagId: 6};
                var saved = captureCarryForwardState();
                JSON.stringify([opening.bagId, opening.beanBaseId, opening.doseWeight,
                                opening.grinderSetting, saved.bagId, saved.doseWeight]);
            )JS"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        QCOMPARE(result.toString(), QStringLiteral("[5,\"42\",18,\"10\",6,19]"));
    }

    void queuedAnchorCannotFollowNavigationToAnotherShot()
    {
        QFile file(QStringLiteral(DECENZA_SOURCE_DIR "/qml/pages/PostShotReviewPage.qml"));
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(file.errorString()));
        const QString source = QString::fromUtf8(file.readAll());
        // Execute the shipping page functions, with only their service and page
        // state supplied by the fixture, as the source-function tests elsewhere do.
        const auto extract = [&source](const QString& name) {
            const QString needle = QStringLiteral("function ") + name + QLatin1Char('(');
            const qsizetype start = source.indexOf(needle);
            if (start < 0 || source.indexOf(needle, start + 1) >= 0) return QString();
            int depth = 0;
            for (qsizetype i = source.indexOf(QLatin1Char('{'), start); i >= 0 && i < source.size(); ++i) {
                if (source[i] == QLatin1Char('{')) ++depth;
                else if (source[i] == QLatin1Char('}') && --depth == 0)
                    return source.mid(start, i - start + 1);
            }
            return QString();
        };
        QJSEngine engine;
        for (const QString& name : {QStringLiteral("markGrindAnchor"),
                                    QStringLiteral("_sendPendingGrindAnchor"),
                                    QStringLiteral("_loadSteppedShot")}) {
            const QString function = extract(name);
            QVERIFY2(!function.isEmpty(), qPrintable(name));
            const QJSValue loaded = engine.evaluate(function);
            QVERIFY2(!loaded.isError(), qPrintable(loaded.toString()));
        }
        const QJSValue fixture = engine.evaluate(QStringLiteral(R"JS(
            var EquipmentService = {
                calls: [],
                requestSetGrindAnchor: function(shot, side, pkg) { this.calls.push([shot, side, pkg]); }
            };
            var ShotReviewActions = {
                releaseShotEdits: function() {}, holdShotEdits: function() {},
                newRequestId: function() { return "next-review"; }
            };
            var _ensureReviewLease = function() {};
            var HistoryService = { requestShot: function() { return "held-shot"; } };
            var _reviewSessionId = "old-review", _leasedClientId = "client";
            var loadShotForEditing = function() {};
            var hasUnsavedChanges = false, _saveFailed = false;
            var editShotId = 101, editEquipmentId = 7, _heldShotId = 101;
            var _pendingAnchorSide = "", _pendingAnchorShotId = -1, _pendingAnchorPackageId = -1;
            var _pendingMetadataWrites = { save: { shotId: 101 } };
        )JS"));
        QVERIFY2(!fixture.isError(), qPrintable(fixture.toString()));
        const auto run = [&engine](const QString& script) { return engine.evaluate(script); };

        QJSValue result = run(QStringLiteral("markGrindAnchor('fine'); _loadSteppedShot(102); _pendingMetadataWrites = {}; _sendPendingGrindAnchor(); EquipmentService.calls.length"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        QCOMPARE(result.toInt(), 0);
        QCOMPARE(run(QStringLiteral("_pendingAnchorSide")).toString(), QString());
        QCOMPARE(run(QStringLiteral("_pendingAnchorShotId")).toInt(), -1);

        // Dispatch itself also rejects a changed selection, independently of the
        // normal stepping path clearing the queued intent.
        result = run(QStringLiteral("editShotId = 101; _pendingMetadataWrites = { save: {} }; markGrindAnchor('coarse'); editShotId = 102; _pendingMetadataWrites = {}; _sendPendingGrindAnchor(); EquipmentService.calls.length"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        QCOMPARE(result.toInt(), 0);

        // Staying on the shot sends exactly the captured shot/package after save.
        result = run(QStringLiteral("editShotId = 101; _pendingMetadataWrites = { save: {} }; markGrindAnchor('fine'); _pendingMetadataWrites = {}; _sendPendingGrindAnchor(); JSON.stringify(EquipmentService.calls)"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        QCOMPARE(result.toString(), QStringLiteral("[[101,\"fine\",7]]"));
        result = run(QStringLiteral("_loadSteppedShot(102); EquipmentService.calls.length"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        QCOMPARE(result.toInt(), 1); // Navigation leaves the already-sent host write alone.
    }

    void steppedShotFailureRetainsDraftAndLeaseUntilCorrelatedRetry_data()
    {
        QTest::addColumn<QString>("reason");
        QTest::newRow("disconnect") << QStringLiteral("disconnected");
        QTest::newRow("missing-shot") << QStringLiteral("notFound");
    }

    void steppedShotFailureRetainsDraftAndLeaseUntilCorrelatedRetry()
    {
        QFETCH(QString, reason);
        QFile file(QStringLiteral(DECENZA_SOURCE_DIR "/qml/pages/PostShotReviewPage.qml"));
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(file.errorString()));
        const QString source = QString::fromUtf8(file.readAll());
        QVERIFY(!source.contains(QStringLiteral("function onShotReady(")));
        QJSEngine engine;
        for (const QString& name : {QStringLiteral("_loadSteppedShot"),
                                    QStringLiteral("_acceptSteppedShot"),
                                    QStringLiteral("_completeShotLoad"),
                                    QStringLiteral("_applyLoadedShot"),
                                    QStringLiteral("captureCarryForwardState"),
                                    QStringLiteral("onOperationCompleted")}) {
            const QString function = sourceFunction(source, name);
            QVERIFY2(!function.isEmpty(), qPrintable(name));
            const QJSValue loaded = engine.evaluate(function);
            QVERIFY2(!loaded.isError(), qPrintable(loaded.toString()));
        }
        QJSValue result = engine.evaluate(QStringLiteral(R"JS(
            var postShotReviewPage = this;
            var editShotId = 101, _heldShotId = 101, _reviewSessionId = "old-view";
            var _leasedClientId = "client", _leaseRequestId = "", _pendingStepShotId = 0;
            var _shotLoadRequestId = "", _deleteRequestId = "", _baristaRequestId = "";
            var _pendingMetadataWrites = {oldWrite: {shotId: 101}};
            var _carryLeaveIntent = -1;
            var editShotData = {id:101, espressoNotes:"stored notes"};
            var editNotes = "local draft", editDrinkTds = 1.4, editDrinkEy = 20;
            var _committedState = {notes:"committed"}, _undoStack = ["old undo"], _undoDepth = 1;
            var _editLoaded = true, _saveFailed = true, _stepping = false;
            var pendingVisualizerUpdate = true, pendingDecentUpdate = true;
            var _decentUploaded = true, _decentState = {uploaded:true}, _visualizerId = "old-upload";
            var shotOutcome = {old:true}, r2CommitPending = true, beanBaseLinked = false;
            var _pendingAnchorSide = "fine", _pendingAnchorShotId = 101, _pendingAnchorPackageId = 7;
            var pageColumn = {opacity:0}, contentSlide = {x:50};
            var enterAnimation = {count:0, start:function() {
                ++this.count; pageColumn.opacity = 1; contentSlide.x = 0;
            }};
            var flickable = {returnToBounds:function() {}};
            var Qt = {callLater:function(callback) {callback();}};
            var errors = [], announcements = [], ensured = 0;
            var reviewActionError = {show:function(message) {errors.push(message);}};
            var AccessibilityManager = {enabled:true, announce:function(message) {announcements.push(message);}};
            var TranslationManager = {translate:function(key, fallback) {return fallback;}};
            var WebDebugLogger = {warn:function() {}};
            var RecipeContext = {revision:17, preferences:{dyeBeanWeight:18}};
            var DateUtils = {normalizeDateString:function(value) {return value;}};
            var HistoryService = {calls:[], requestShot:function(shotId) {
                this.calls.push(shotId); return "load-" + this.calls.length;
            }, requestDecentUploadState:function() {}, requestShotOutcome:function() {}};
            var ShotReviewActions = {released:[], releaseShotEdits:function(shotId, viewId) {
                this.released.push([shotId, viewId]);
            }, newRequestId:function() {return "new-view";}, refreshVisualizerShot:function() {}};
            var _ensureReviewLease = function() {++ensured;};
            var calculateEy = function() {};
            var captureEditState = function() {return {notes:editNotes};};
            var rebindInputs = function() {};
            _loadSteppedShot(102);
        )JS"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        const auto run = [&engine](const QString& script) { return engine.evaluate(script); };
        const auto oldState = [&]() {
            return run(QStringLiteral(R"JS(JSON.stringify([
                editShotId, editShotData, editNotes, editDrinkTds, editDrinkEy,
                _heldShotId, _reviewSessionId, _leasedClientId, _committedState,
                _undoStack, _undoDepth, _editLoaded, _saveFailed, _pendingMetadataWrites,
                pendingVisualizerUpdate, pendingDecentUpdate, _decentUploaded, _decentState,
                _visualizerId, shotOutcome, r2CommitPending, ShotReviewActions.released, ensured
            ]))JS")).toString();
        };
        const QString retainedState = oldState();
        QCOMPARE(run(QStringLiteral("editShotId")).toInt(), 101);
        QCOMPARE(run(QStringLiteral("_heldShotId")).toInt(), 101);
        QCOMPARE(run(QStringLiteral("editNotes")).toString(), QStringLiteral("local draft"));
        engine.globalObject().setProperty(QStringLiteral("failureReason"), reason);
        result = run(QStringLiteral("onOperationCompleted('foreign', 'shot', false, {}, failureReason)"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        QCOMPARE(run(QStringLiteral("_shotLoadRequestId")).toString(), QStringLiteral("load-1"));
        QCOMPARE(run(QStringLiteral("errors.length")).toInt(), 0);
        QCOMPARE(oldState(), retainedState);

        result = run(QStringLiteral("onOperationCompleted('load-1', 'shot', false, {}, failureReason)"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        QCOMPARE(oldState(), retainedState);
        QVERIFY(!run(QStringLiteral("_stepping")).toBool());
        QCOMPARE(run(QStringLiteral("_pendingStepShotId")).toInt(), 0);
        QCOMPARE(run(QStringLiteral("pageColumn.opacity")).toInt(), 1);
        QCOMPARE(run(QStringLiteral("contentSlide.x")).toInt(), 0);
        QCOMPARE(run(QStringLiteral("errors[0]")).toString(), QStringLiteral("Could not load shot details"));
        QCOMPARE(run(QStringLiteral("announcements[0]")).toString(), QStringLiteral("Could not load shot details"));

        result = run(QStringLiteral(R"JS(
            _loadSteppedShot(102);
            onOperationCompleted('load-1', 'shot', true, {shotId:102, shot:{id:102}}, '');
        )JS"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        QCOMPARE(oldState(), retainedState);
        QCOMPARE(run(QStringLiteral("_shotLoadRequestId")).toString(), QStringLiteral("load-2"));
        QVERIFY(run(QStringLiteral("_stepping")).toBool());
        result = run(QStringLiteral(R"JS(
            onOperationCompleted('load-2', 'shot', true,
                {shotId:102, shot:{id:102, beanBrand:'New roaster', espressoNotes:'loaded notes'}}, '');
        )JS"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        QCOMPARE(run(QStringLiteral("editShotId")).toInt(), 102);
        QCOMPARE(run(QStringLiteral("editShotData.id")).toInt(), 102);
        QCOMPARE(run(QStringLiteral("editBeanBrand")).toString(), QStringLiteral("New roaster"));
        QCOMPARE(run(QStringLiteral("editNotes")).toString(), QStringLiteral("loaded notes"));
        QCOMPARE(run(QStringLiteral("_heldShotId")).toInt(), 102);
        QCOMPARE(run(QStringLiteral("_reviewSessionId")).toString(), QStringLiteral("new-view"));
        QCOMPARE(run(QStringLiteral("JSON.stringify(ShotReviewActions.released)")).toString(),
                 QStringLiteral("[[101,\"old-view\"]]"));
        QCOMPARE(run(QStringLiteral("ensured")).toInt(), 1);
        QVERIFY(!run(QStringLiteral("_stepping")).toBool());
        QCOMPARE(run(QStringLiteral("_pendingStepShotId")).toInt(), 0);
        QCOMPARE(run(QStringLiteral("pageColumn.opacity")).toInt(), 1);
        QCOMPARE(run(QStringLiteral("errors.length")).toInt(), 1);
        QCOMPARE(run(QStringLiteral("_pendingMetadataWrites.oldWrite.shotId")).toInt(), 101);
    }

    void hiddenBeanSearchDropsLateReplies()
    {
        QFile file(QStringLiteral(DECENZA_SOURCE_DIR "/qml/components/BeanBaseSearchBar.qml"));
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString source = QString::fromUtf8(file.readAll());
        QJSEngine engine;
        for (const QString& name : {QStringLiteral("prefillAndSearch"), QStringLiteral("onSearchResults"),
                                    QStringLiteral("onSearchFailed")}) {
            const QString function = sourceFunction(source, name);
            QVERIFY2(!function.isEmpty(), qPrintable(name));
            const auto loaded = engine.evaluate(function);
            QVERIFY2(!loaded.isError(), qPrintable(loaded.toString()));
        }
        const QString hidden = sourceBody(source, QStringLiteral("onVisibleChanged:"));
        QVERIFY(!hidden.isEmpty());
        const auto result = engine.evaluate(QStringLiteral(R"JS(
            var root = this, linked = false, visible = true, searchState = 'idle';
            var _pendingQuery = '', _openOnResult = false, _accessibilityMode = false;
            var results = [], errorToken = '', searchInput = {activeFocus:false};
            var resultsPopup = {opens:0, open:function() {++this.opens;}, close:function() {}};
            var _setInputText = function() {};
            var BeanUiService = {search:function() {}};
            function visibilityChanged() %1
            prefillAndSearch('Original coffee');
            onSearchResults('foreign query', [{id:'foreign'}]);
            visible = false; visibilityChanged();
            onSearchResults('Original coffee', [{id:'cancelled'}]);
            onSearchFailed('Original coffee', 'failed');
            var opensWhileHidden = resultsPopup.opens;
            var stateWhileHidden = searchState;
            visible = true; prefillAndSearch('New coffee');
            onSearchResults('Original coffee', [{id:'superseded'}]);
            onSearchResults('New coffee', [{id:'current'}]);
            ({hiddenOpens:opensWhileHidden,hiddenState:stateWhileHidden,opens:resultsPopup.opens,
              selected:results[0].id,error:errorToken})
        )JS").arg(hidden));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        QCOMPARE(result.property(QStringLiteral("hiddenOpens")).toInt(), 0);
        QCOMPARE(result.property(QStringLiteral("hiddenState")).toString(), QStringLiteral("idle"));
        QCOMPARE(result.property(QStringLiteral("opens")).toInt(), 1);
        QCOMPARE(result.property(QStringLiteral("selected")).toString(), QStringLiteral("current"));
        QCOMPARE(result.property(QStringLiteral("error")).toString(), QString());
    }

    void leaseCommandsCarryTheirConnectionAndViewIdentity()
    {
        ReviewActionFixtureSource source;
        QRemoteObjectHost host;
        QRemoteObjectNode client;
        QQmlEngine engine;
        const QString clientId = id(1);
        const QString reviewId = id(101);
        client.setProperty("decenzaClientId", clientId);
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<ShotReviewActionStateSourceAPI>(&source));
        QVERIFY(client.connectToNode(host.hostUrl()));
        ShotReviewActions::setPublishedNode(&client);
        struct ResetNode { ~ResetNode() { ShotReviewActions::setPublishedNode(nullptr); } } resetNode;
        auto* actions = ShotReviewActionsForeign::create(&engine, &engine);
        QSignalSpy completed(actions, &ShotReviewActions::actionCompleted);
        QTRY_VERIFY(actions->ready());
        QCOMPARE(actions->clientId(), clientId);

        actions->holdShotEdits(11, reviewId);
        actions->expectHeldShotEdit(11, reviewId);
        actions->releaseShotEdits(11, reviewId);
        QTRY_COMPARE(source.calls.size(), 3);
        QCOMPARE(source.calls.at(0).operation, QStringLiteral("hold"));
        QCOMPARE(source.calls.at(1).operation, QStringLiteral("expectHeldEdit"));
        QCOMPARE(source.calls.at(2).operation, QStringLiteral("release"));
        const QVariantMap expected{{QStringLiteral("clientId"), clientId},
                                   {QStringLiteral("reviewId"), reviewId},
                                   {QStringLiteral("shotId"), qint64(11)}};
        for (const auto& call : std::as_const(source.calls)) {
            QCOMPARE(call.arguments, expected);
            source.complete(call);
        }
        QTRY_COMPARE(completed.size(), 3);
    }

    void refractometerCallsStayCorrelatedAndHostOwned()
    {
        ReviewActionFixtureSource source;
        QRemoteObjectHost host;
        QRemoteObjectNode client;
        QQmlEngine engine;
        const QString clientId = id(1);
        const QString reviewId = id(101);
        client.setProperty("decenzaClientId", clientId);
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<ShotReviewActionStateSourceAPI>(&source));
        QVERIFY(client.connectToNode(host.hostUrl()));
        ShotReviewActions::setPublishedNode(&client);
        struct ResetNode { ~ResetNode() { ShotReviewActions::setPublishedNode(nullptr); } } resetNode;
        auto* actions = ShotReviewActionsForeign::create(&engine, &engine);
        QSignalSpy completed(actions, &ShotReviewActions::actionCompleted);
        QSignalSpy stateChanges(actions, &ShotReviewActions::stateChanged);
        QSignalSpy invalidated(actions, &ShotReviewActions::carryForwardInvalidated);
        QTRY_VERIFY(actions->ready());
        emit source.carryForwardInvalidated(reviewId);
        QTRY_COMPARE(invalidated.size(), 1);
        QCOMPARE(invalidated.first().at(0).toString(), reviewId);

        const qsizetype stateChangeBaseline = stateChanges.count();
        source.setDecentLastSerial(QStringLiteral("updated-serial"));
        QTRY_COMPARE(actions->decentLastSerial(), QStringLiteral("updated-serial"));
        QVERIFY(stateChanges.count() > stateChangeBaseline);

        // These calls travel to the host as correlated commands. The test source
        // records them but does not scan or connect any physical BLE device.
        actions->setRefractometerHunt(true, reviewId);
        actions->scanForDevices(reviewId);
        actions->requestRefractometerMeasurement(reviewId);
        QTRY_COMPARE(source.calls.size(), 3);
        QCOMPARE(source.calls.at(0).operation, QStringLiteral("refractometerHunt"));
        QCOMPARE(source.calls.at(0).arguments,
                 (QVariantMap{{QStringLiteral("active"), true},
                              {QStringLiteral("reviewId"), reviewId},
                              {QStringLiteral("clientId"), clientId}}));
        QCOMPARE(source.calls.at(1).operation, QStringLiteral("scanForDevices"));
        QCOMPARE(source.calls.at(1).arguments,
                 (QVariantMap{{QStringLiteral("reviewId"), reviewId},
                              {QStringLiteral("clientId"), clientId}}));
        QCOMPARE(source.calls.at(2).operation,
                 QStringLiteral("requestRefractometerMeasurement"));
        QCOMPARE(source.calls.at(2).arguments, source.calls.at(1).arguments);

        source.setRefractometerOwnerReviewId(reviewId);
        QTRY_COMPARE(actions->refractometerOwnerReviewId(), reviewId);
        QSignalSpy measurementComplete(actions, &ShotReviewActions::refractometerMeasurementComplete);
        QSignalSpy progress(actions, &ShotReviewActions::refractometerAverageProgress);
        emit source.refractometerAverageProgress(reviewId, 1, 3);
        emit source.refractometerMeasurementComplete(reviewId);
        QTRY_COMPARE(progress.size(), 1);
        QTRY_COMPARE(measurementComplete.size(), 1);
        QCOMPARE(progress.first().at(0).toString(), reviewId);
        QCOMPARE(progress.first().at(1).toInt(), 1);
        QCOMPARE(measurementComplete.first().at(0).toString(), reviewId);

        // Completing out of order cannot cross-route the operation or its ID.
        source.complete(source.calls.at(2));
        source.complete(source.calls.at(0));
        QTRY_COMPARE(completed.count(), 2);
        QCOMPARE(completed.at(0).at(0).toString(), source.calls.at(2).requestId);
        QCOMPARE(completed.at(0).at(1).toString(),
                 QStringLiteral("requestRefractometerMeasurement"));
        QCOMPARE(completed.at(1).at(0).toString(), source.calls.at(0).requestId);
        QCOMPARE(completed.at(1).at(1).toString(), QStringLiteral("refractometerHunt"));

        // A response for an unknown request is ignored; it cannot complete the
        // still-pending scan command.
        emit source.actionCompleted(QUuid::createUuid().toString(QUuid::WithoutBraces),
                                    QStringLiteral("scanForDevices"), true, {}, {});
        QCoreApplication::processEvents();
        QCOMPARE(completed.count(), 2);
        source.complete(source.calls.at(1), false);
        QTRY_COMPARE(completed.count(), 3);
        QCOMPARE(completed.last().at(0).toString(), source.calls.at(1).requestId);
        QVERIFY(!completed.last().at(2).toBool());

    }

    void disconnectCompletesPendingActionWithoutReplay()
    {
        ReviewActionFixtureSource source;
        QRemoteObjectHost host;
        QRemoteObjectNode client;
        QQmlEngine engine;
        client.setProperty("decenzaClientId", id(1));
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<ShotReviewActionStateSourceAPI>(&source));
        QVERIFY(client.connectToNode(host.hostUrl()));
        ShotReviewActions::setPublishedNode(&client);
        struct ResetNode { ~ResetNode() { ShotReviewActions::setPublishedNode(nullptr); } } resetNode;
        auto* actions = ShotReviewActionsForeign::create(&engine, &engine);
        QSignalSpy completed(actions, &ShotReviewActions::actionCompleted);
        QTRY_VERIFY(actions->ready());

        actions->scanForDevices(id(101));
        QTRY_COMPARE(source.calls.size(), 1);
        const auto call = source.calls.first();
        actions->setNode(nullptr);
        QTRY_COMPARE(completed.count(), 1);
        QCOMPARE(completed.first().at(0).toString(), call.requestId);
        QVERIFY(!completed.first().at(2).toBool());

        source.complete(call);
        QCoreApplication::processEvents();
        QCOMPARE(completed.count(), 1);
        QCOMPARE(source.calls.size(), 1);
    }

    void deletingFacadeFromStateNotificationStopsMeasuringContinuation()
    {
        ReviewActionFixtureSource source;
        QRemoteObjectHost host;
        QRemoteObjectNode client;
        QQmlEngine engine;
        client.setProperty("decenzaClientId", id(1));
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<ShotReviewActionStateSourceAPI>(&source));
        QVERIFY(client.connectToNode(host.hostUrl()));
        ShotReviewActions::setPublishedNode(&client);
        struct ResetNode { ~ResetNode() { ShotReviewActions::setPublishedNode(nullptr); } } resetNode;
        auto* actions = ShotReviewActionsForeign::create(&engine, &engine);
        QTRY_VERIFY(actions->ready());

        QPointer<ShotReviewActions> guard(actions);
        connect(actions, &ShotReviewActions::stateChanged, actions,
                [actions] { delete actions; }, Qt::DirectConnection);
        source.setRefractometerMeasuring(true);
        QTRY_VERIFY(guard.isNull());
    }
};

QTEST_GUILESS_MAIN(ShotReviewActionsTest)
#include "tst_shotreviewactions.moc"
