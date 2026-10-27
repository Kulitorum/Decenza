// Differential guard on the TWO implementations of segmentsToHtml().
//
// A custom layout widget can be authored in the app (CustomEditorPopup -> DocumentFormatter,
// src/core/documentformatter.cpp) or in the web layout editor (the JS served from
// src/network/shotserver_layout.cpp). Both compile the same segment schema to the same stored
// `content` string, so the two must agree byte for byte — CLAUDE.md's rule that the app and
// ShotServer surfaces stay in sync. Neither had any test, and they had already drifted: the
// C++ side escapes the double quote via QString::toHtmlEscaped() (qstring.cpp:10118) and the
// JS side did not.
//
// The JS is run out of shipping source, not reimplemented here. A copy would drift and then
// assert that the copy is correct, which is worse than no test — same reasoning as
// tst_textescaping.cpp, whose extraction approach this follows.
//
// Also pins DocumentFormatter's own colour round trip, which had no coverage at all. The
// distinction it guards: NO stored colour is the "Default" state that makes a widget follow
// the theme, and an explicit colour — including black — is stored as itself. Black used to
// double as the sentinel, so choosing black in the editor gave back theme-coloured text.

#include <QtTest>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextDocument>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QAccessible>
#include <QPointer>
#include <QWheelEvent>
#include <memory>

#include "core/documentformatter.h"
#include "core/settings_network.h"
#include "remote/browserdisplay.h"

class QmlSequenceFixture : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList rows READ rows CONSTANT)
    Q_PROPERTY(QVariantList days READ days CONSTANT)
public:
    QVariantList rows() const {
        return {QVariantMap{{QStringLiteral("name"), QStringLiteral("First")}},
                QVariantMap{{QStringLiteral("name"), QStringLiteral("Second")}}};
    }
    QVariantList days() const {
        QVariantList result;
        for (int i = 0; i < 7; ++i)
            result.append(QVariantMap{{QStringLiteral("enabled"), false},
                                      {QStringLiteral("hour"), i},
                                      {QStringLiteral("minute"), 0}});
        return result;
    }
};

class TestCustomWidgetHtml : public QObject {
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }
    void initTestCase();

    void bothImplementationsAgree_data();
    void bothImplementationsAgree();

    void roundTripPreservesExplicitColourAndSize();
    void colourIsDroppedOnlyWhenUnset();
    void explicitBlackIsStored();
    void clearColorReturnsToDefault();
    void clearColorLeavesOtherRunsAlone();
    void homePresetActionsRunShippingSelectionAndStartPolicy();
    void browserPixelWheelUsesQtGestureUnderPage();
    void browserFullscreenUsesWindowScreen();
    void browserVideoHandleRecoveryReloadsOnce();
    void sharedMediaImportRunsShippingAdmissionCancelAndCompletionHandlers();
    void beanFormRefusalAnnouncesWithoutSubmittingOrChangingDraft();
    void bagCardShippingActionsKeepBagAndDateIdentity();
    void recipeBagRepointAndRestockUseTheCapturedContext();
    void reviewBeanPickerFollowsTheShotAndAcknowledgedSelection();
    void comparisonAccessiblePressInspectsGraphLocalCenter();
    void screensaverDimmingFollowsRetainedModeAndReadiness();

    void everyCatalogActionHasADispatchArm();
    void everyAppShellRequestReachesTheShell();
    void homeMachineActionsUseSharedServices();
    void webCatalogOmitsActionsTheWebCannotAuthor();
    void actionContextsAreDrawnFromTheKnownVocabulary();
    void neitherEditorHardCodesItsOwnActionList();
    void historyFilterKeysAreUnderstoodByTheStorageLayer();
    void gestureDestinationsAreDeclaredExactlyOnce();
    void everyGestureCapableTypeRoutesThroughTheSharedHelper();
    void everyGestureHandlerCanActuallyFire();
    void compiledSleepTileFollowsItsOptions();
    void sharedPresetDraftUsesCapturedRevisionAndExplicitReload();
    void brewDialogOwnSaveRebasesOnlyAcknowledgedFields();
    void hotWaterAndFlushInputsStayBoundToSharedDrafts();
    void grindDialInUsesOneCombinedPreferenceRequest();
    void profileEditorsRetainDraftsAcrossActivation();
    void qtSequenceSnapshotsRemainUsable();
    void visualizerTerminalFailureReleasesResolver();
    void steamBusyEditorReportsRefusal();
    void updateTranslationToggleReportsAcknowledgedValue();
    void localApkUploadRequiresConfirmationAndCorrelatesHandoff();
    void settingsConversationRetainsRefusedDraftAndProjectsUnsavedState();
    void sharedSupportClosureResumesNativePopupQueueAfterLastModal();
    void shotKnowledgeDoesNotUseCurrentProfileDifference();
    void sensorCalibrationCapturesCurrentProfile();
    void privateSettingsFailuresReachTheErrorToast();
    void completedBrowserReviewReplacesItsPredecessor();
    void browserAppBackUsesTheRetainedParent();
    void sharedEscapeWaitsForPageReleaseGuard();
    void homePlanModeFollowsRouteAndHostPreparation();
    void planAccessibilityReplacesItsFixedRoleNode();
    void compactWorkflowPickerSelectsTheHostMode_data();
    void compactWorkflowPickerSelectsTheHostMode();

private:
    QJSEngine m_engine;
    QJSValue m_jsSegmentsToHtml;
    QString viaJs(const QVariantList &segments);
    static QString readSource(const QString &relativePath);
    static QString liftFunction(const QString &src, const QString &signature);
    static QString stripQmlTypeAnnotations(QString fn);
    static QString liftHandler(const QString& src, const QString& anchor,
                               const QString& handler, const QString& name);
};

// EVERY source path this file inspects, in one place. These tests read shipping
// source because there is no QML engine here, and that coupling is what made eight
// of them fail across one refactor — not because behaviour broke, but because a
// file moved. One constant per file means a move is one edit, and the "this test is
// now blind" guards below turn a bad path into a loud failure rather than a pass.
namespace SrcPath {
const auto kLayoutActions   = QStringLiteral("/qml/components/layout/LayoutActions.qml");
const auto kCustomItem      = QStringLiteral("/qml/components/layout/items/CustomItem.qml");
const auto kCustomEditor    = QStringLiteral("/qml/components/layout/CustomEditorPopup.qml");
const auto kReadoutOptions  = QStringLiteral("/qml/components/layout/ReadoutOptionsPopup.qml");
const auto kItemDelegate    = QStringLiteral("/qml/components/layout/LayoutItemDelegate.qml");
const auto kShotHistoryPage = QStringLiteral("/qml/pages/ShotHistoryPage.qml");
const auto kSharedPresetDraft = QStringLiteral("/qml/components/SharedPresetDraft.qml");
const auto kBrewDialog = QStringLiteral("/qml/components/BrewDialog.qml");
const auto kWebLayout       = QStringLiteral("/src/network/shotserver_layout.cpp");
const auto kStorageQueries  = QStringLiteral("/src/history/shothistorystorage_queries.cpp");
const auto kSleepEditor     = QStringLiteral("/qml/components/layout/SleepEditorPopup.qml");
const auto kLibraryItemCard = QStringLiteral("/qml/components/library/LibraryItemCard.qml");
const auto kAppShell        = QStringLiteral("/qml/AppShell.qml");
const auto kMainQml         = QStringLiteral("/qml/main.qml");
const auto kHotWaterPage   = QStringLiteral("/qml/pages/HotWaterPage.qml");
const auto kFlushPage      = QStringLiteral("/qml/pages/FlushPage.qml");
const auto kBrowserDisplay = QStringLiteral("/src/remote/browserdisplay.cpp");
const auto kBrowserEntry = QStringLiteral("/resources/webclient/index.html");
const auto kGrindQuickSelect = QStringLiteral("/qml/components/layout/items/GrindQuickSelectItem.qml");
inline QString widgetItem(const QString &name) {
    return QStringLiteral("/qml/components/layout/items/%1.qml").arg(name);
}
}

// The ten action widgets whose gestures a user can override, as their dedicated
// items/<Type>Item.qml render format. One declaration, because two slots need it and
// two hand-kept copies of the same ten names is the drift this file exists to prevent.
// (kOneSlot/kTwoSlot below split the same set by whether the type RESERVES a
// destination, which is a different question about the same widgets.)
static const QStringList kGestureWidgetFiles{
    QStringLiteral("RecipesItem"), QStringLiteral("BeansItem"), QStringLiteral("SteamItem"),
    QStringLiteral("HotWaterItem"), QStringLiteral("FlushItem"), QStringLiteral("EspressoItem"),
    QStringLiteral("EquipmentItem"), QStringLiteral("HistoryItem"),
    QStringLiteral("AutoFavoritesItem"), QStringLiteral("SettingsItem") };


// Lift the JS segmentsToHtml() out of the raw string literal it is served from. Brace-matched
// rather than regex'd, so a nested `}` inside the function body cannot truncate it.
void TestCustomWidgetHtml::initTestCase()
{
    const QString src = readSource(SrcPath::kWebLayout);
    QVERIFY2(!src.isEmpty(), "could not read shotserver_layout.cpp");

    const QString needle = QStringLiteral("function segmentsToHtml(");
    const qsizetype start = src.indexOf(needle);
    QVERIFY2(start >= 0, "function segmentsToHtml( not found in shotserver_layout.cpp — the "
                         "web editor's copy moved or was renamed; this test is now blind");
    QVERIFY2(src.indexOf(needle, start + 1) < 0,
             "more than one segmentsToHtml( in shotserver_layout.cpp — cannot tell which one "
             "the web editor serves");

    const QString body = liftFunction(src, needle);
    QVERIFY2(!body.isEmpty(), "could not brace-match the JS segmentsToHtml body");

    const QJSValue result = m_engine.evaluate(QStringLiteral("(function(){ %1 return segmentsToHtml; })()").arg(body));
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    m_jsSegmentsToHtml = result;
    QVERIFY(m_jsSegmentsToHtml.isCallable());
}

QString TestCustomWidgetHtml::readSource(const QString &relativePath)
{
    QFile f(QStringLiteral(DECENZA_SOURCE_DIR) + relativePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readAll());
}

// The first function in `src` starting with `signature`, brace-matched rather than regex'd so
// a nested `}` cannot truncate it. Empty when it is absent.
QString TestCustomWidgetHtml::liftFunction(const QString &src, const QString &signature)
{
    const qsizetype start = src.indexOf(signature);
    if (start < 0)
        return QString();
    int depth = 0;
    for (qsizetype i = src.indexOf('{', start); i >= 0 && i < src.size(); ++i) {
        if (src[i] == '{') ++depth;
        else if (src[i] == '}' && --depth == 0)
            return src.mid(start, i - start + 1);
    }
    return QString();
}

// QJSEngine evaluates plain JavaScript, which rejects QML's parameter and return type
// annotations. Remove only those, so the shipping function body runs unchanged.
QString TestCustomWidgetHtml::stripQmlTypeAnnotations(QString fn)
{
    static const QRegularExpression annotation(
        QStringLiteral(":\\s*(?:string|int|double|real|bool|var|void)\\b"));
    return fn.remove(annotation);
}

// Lift a shipping QML signal handler, including its single-expression form.
QString TestCustomWidgetHtml::liftHandler(const QString& src, const QString& anchor,
                                         const QString& handler, const QString& name)
{
    const qsizetype start = src.indexOf(anchor);
    if (start < 0) return {};
    const qsizetype signal = src.indexOf(handler, start);
    if (signal < 0) return {};
    const QString value = src.mid(signal + handler.size()).trimmed();
    if (value.startsWith(QStringLiteral("function"))) {
        QString function = liftFunction(value, QStringLiteral("function"));
        function.replace(0, qsizetype(8), QStringLiteral("function ") + name);
        return function;
    }
    if (value.startsWith(QLatin1Char('{'))) {
        QString block = liftFunction(src.mid(signal), handler);
        block.replace(0, block.indexOf(QLatin1Char('{')), QStringLiteral("function ") + name + QStringLiteral("() "));
        return block;
    }
    return QStringLiteral("function %1() { %2 }").arg(name, value.section(QLatin1Char('\n'), 0, 0));
}

// Run the SHIPPING dispatch — LayoutActions.execute() lifted out of the QML and
// executed in a QJSEngine against recording stubs — and assert every action the
// catalog offers actually reaches a handler.
//
// This replaces a scrape that sliced the function into `category === "..."` branches
// and looked for `case "x":` text. That scrape asserted the SHAPE of the code, so it
// broke whenever the code moved and would have passed on a `case` that called the
// wrong thing. Running the real function asserts the BEHAVIOUR instead: a renamed
// branch, a reordered switch, or a rewritten dispatch all still pass as long as
// every action does something, and an action that silently does nothing fails.
//
// The stubs are recorders, not fakes: every singleton the dispatch touches becomes a
// Proxy that logs `Name.member(...)` and returns another recorder, so any call shape
// works without the test knowing what a given action is supposed to do.
static const char *kRecorderPreamble = R"JS(
    var __calls = [];
    // Some arms are guarded on a NUMBER ("is there a saved shot?", "is a recipe
    // active?"). A bare recorder fails those comparisons and the arm looks dead, so
    // these read as a plausible positive value.
    var __numeric = ["lastSavedShotId", "activeRecipeId", "activeBagId", "chargingMode",
                     "activePackageId", "selectedRecipeId", "selectedFavoriteProfile"];
    function __rec(name) {
        return new Proxy(function(){}, {
            get: function(t, prop) {
                if (prop === Symbol.toPrimitive || prop === "toString") return function(){ return name; };
                if (__numeric.indexOf(String(prop)) >= 0) return 1;
                return __rec(name + "." + String(prop));
            },
            // An arm can WRITE rather than call (chargingMode = ...), so a set is
            // just as much "this action did something" as a call is.
            set: function(t, prop, value) { __calls.push(name + "." + String(prop) + "="); return true; },
            apply: function(t, self, args) { __calls.push(name); return __rec(name + "()"); },
            has: function() { return true; }
        });
    }
    var AppShell = __rec("AppShell");
    var MainController = __rec("MainController");
    var ProfileManager = __rec("ProfileManager");
    var ProfileService = __rec("ProfileService");
    var RecipeService = __rec("RecipeService");
    var RecipeContext = __rec("RecipeContext");
    var BagService = __rec("BagService");
    var EquipmentService = __rec("EquipmentService");
    var MachineControlService = __rec("MachineControlService");
    var HomePresetActions = __rec("HomePresetActions");
    var HistoryService = __rec("HistoryService");
    var ShotReviewActions = __rec("ShotReviewActions");
    var SteamService = __rec("SteamService");
    var DE1Device = __rec("DE1Device");
    var ScaleDevice = __rec("ScaleDevice");
    var MachineState = __rec("MachineState");
    var BLEManager = __rec("BLEManager");
    var BatteryManager = __rec("BatteryManager");
    var TranslationManager = __rec("TranslationManager");
    var Settings = __rec("Settings");
    var Qt = __rec("Qt");
    var console = { warn: function(m){ __calls.push("WARN:" + m); }, log: function(){} };
)JS";

void TestCustomWidgetHtml::everyCatalogActionHasADispatchArm()
{
    const QString src = readSource(SrcPath::kLayoutActions);
    QVERIFY2(!src.isEmpty(), "could not read LayoutActions.qml");

    // Lift execute() and the two helpers it calls.
    QString fns;
    for (const QString &name : { QStringLiteral("function _warn("),
                                 QStringLiteral("function execute(") }) {
        const QString fn = liftFunction(src, name);
        QVERIFY2(!fn.isEmpty(), qPrintable(QStringLiteral("%1 not found in LayoutActions.qml — "
                                                          "this test is now blind").arg(name)));
        fns += fn + QStringLiteral("\n");
    }
    // The history-filter helpers execute() calls; lifted so navigate:history* run.
    for (const QString &h : { QStringLiteral("_recipeHistoryFilter"), QStringLiteral("_beanHistoryFilter"),
                              QStringLiteral("_bagHistoryFilter"), QStringLiteral("_profileHistoryFilter") })
        fns += QStringLiteral("function %1() { return null; }\n").arg(h);

    QJSEngine eng;
    const QJSValue setup = eng.evaluate(QString::fromUtf8(kRecorderPreamble)
                                        + fns
                                        + QStringLiteral("(function(a){ __calls = []; execute(a, {}); return __calls.join('|'); })"));
    QVERIFY2(!setup.isError(), qPrintable(setup.toString()));
    QVERIFY(setup.isCallable());

    const QVariantList catalog = SettingsNetwork::layoutActionCatalog();
    QVERIFY2(catalog.size() > 35, "layoutActionCatalog() is not returning the table");

    QStringList dead;
    for (const QVariant &v : catalog) {
        const QVariantMap a = v.toMap();
        const QString id = a.value(QStringLiteral("id")).toString();
        // A parameterized action is never stored bare; it dispatches as
        // `command:loadProfile:<file>`, so that is what we exercise.
        const QString probe = a.value(QStringLiteral("expandsToSubmenu")).toBool()
            ? id + QStringLiteral(":some_profile.json") : id;
        const QJSValue out = setup.call({ probe });
        QVERIFY2(!out.isError(), qPrintable(out.toString()));
        const QString calls = out.toString();
        // Match the WORDS, not a "WARN:unknown" prefix: _warn prepends the
        // subsystem name, so the recorded line is "WARN:LayoutActions: unknown ...".
        // Anchoring on the prefix made this check unreachable — proven by breaking a
        // dispatch arm and watching the test still pass.
        if (calls.isEmpty() || calls.contains(QStringLiteral("unknown"))
                || calls.contains(QStringLiteral("malformed")))
            dead << id + QStringLiteral(" -> ") + (calls.isEmpty() ? QStringLiteral("(nothing)") : calls);
    }
    QVERIFY2(dead.isEmpty(),
             qPrintable(QStringLiteral("catalog actions that reach no handler — they would be "
                                       "offered in an editor and do nothing when tapped: ")
                        + dead.join(QStringLiteral("; "))));
}

// The hop after the dispatch arm: the shared and platform shells' Connections on AppShell. A handler whose
// name matches no signal compiles, passes qmllint, and only warns at runtime
// (qtdeclarative/src/qmlmeta/types/qqmlconnections.cpp:443) — which is
// how #2046 shipped `onDFlowEditorRequested` against `signal dflowEditorRequested()`,
// leaving "Go to D-Flow editor" a dead button.
void TestCustomWidgetHtml::everyAppShellRequestReachesTheShell()
{
    const QString shell = readSource(SrcPath::kAppShell);
    const QString main = readSource(SrcPath::kMainQml);
    const QString navigation = readSource(QStringLiteral("/qml/components/SharedAppNavigation.qml"));
    const QString browser = readSource(QStringLiteral("/qml/webclient/Main.qml"));
    QVERIFY2(!shell.isEmpty() && !main.isEmpty() && !navigation.isEmpty() && !browser.isEmpty(),
             "could not read AppShell.qml or its shared/platform handlers");
    const QString handlers = main + QLatin1Char('\n') + navigation + QLatin1Char('\n') + browser;

    QSet<QString> signalNames;
    static const QRegularExpression signalRe(QStringLiteral(R"(^\s*signal\s+(\w+)\s*\()"),
                                             QRegularExpression::MultilineOption);
    for (auto it = signalRe.globalMatch(shell); it.hasNext();)
        signalNames.insert(it.next().captured(1));
    QVERIFY2(signalNames.size() > 30, "AppShell.qml signal scrape found too few — this test is now blind");

    QSet<QString> handled;
    static const QRegularExpression handlerRe(QStringLiteral(R"(function\s+on([A-Z]\w*)\s*\()"));
    for (qsizetype open = handlers.indexOf(QStringLiteral("Connections {")); open >= 0;
         open = handlers.indexOf(QStringLiteral("Connections {"), open + 1)) {
        const QString block = liftFunction(handlers.mid(open), QStringLiteral("Connections {"));
        if (!block.contains(QStringLiteral("target: AppShell")))
            continue;
        for (auto it = handlerRe.globalMatch(block); it.hasNext();) {
            const QString h = it.next().captured(1);
            handled.insert(h.left(1).toLower() + h.mid(1));
        }
    }
    QVERIFY2(handled.size() > 30, "AppShell handler scrape found too few — this test is now blind");

    QStringList orphanHandlers, unhandledSignals;
    for (const QString &h : std::as_const(handled))
        if (!signalNames.contains(h) && !h.endsWith(QStringLiteral("Changed")))
            orphanHandlers << h;
    for (const QString &s : std::as_const(signalNames))
        if (!handled.contains(s))
            unhandledSignals << s;
    orphanHandlers.sort();
    unhandledSignals.sort();
    QVERIFY2(orphanHandlers.isEmpty(),
             qPrintable(QStringLiteral("Shell handlers naming no AppShell signal (never fire): ")
                        + orphanHandlers.join(QStringLiteral(", "))));
    QVERIFY2(unhandledSignals.isEmpty(),
             qPrintable(QStringLiteral("AppShell signals no shell handles (requests go nowhere): ")
                        + unhandledSignals.join(QStringLiteral(", "))));
}

void TestCustomWidgetHtml::homeMachineActionsUseSharedServices()
{
    const QString src = readSource(SrcPath::kLayoutActions);
    QVERIFY2(!src.isEmpty(), "could not read LayoutActions.qml");
    const QString warn = liftFunction(src, QStringLiteral("function _warn("));
    const QString execute = liftFunction(src, QStringLiteral("function execute("));
    QVERIFY(!warn.isEmpty());
    QVERIFY(!execute.isEmpty());

    QJSEngine engine;
    const QJSValue run = engine.evaluate(QString::fromUtf8(kRecorderPreamble) + warn + execute
        + QStringLiteral("\n(function(action) { __calls = []; execute(action, {}); return __calls.join('|'); })"));
    QVERIFY2(run.isCallable(), qPrintable(run.toString()));
    const QList<QPair<QString, QString>> cases{
        {QStringLiteral("command:startEspresso"), QStringLiteral("HomePresetActions.startEspresso")},
        {QStringLiteral("command:startSteam"), QStringLiteral("HomePresetActions.startSteam")},
        {QStringLiteral("command:startHotWater"), QStringLiteral("HomePresetActions.startHotWater")},
        {QStringLiteral("command:startFlush"), QStringLiteral("HomePresetActions.startFlush")},
        {QStringLiteral("command:sleep"), QStringLiteral("HomePresetActions.sleepMachine")},
        {QStringLiteral("command:idle"), QStringLiteral("HomePresetActions.requestIdle")},
        {QStringLiteral("command:scanDE1"), QStringLiteral("HomePresetActions.scanForDevices")},
        {QStringLiteral("command:scanScale"), QStringLiteral("HomePresetActions.scanForDevices")},
        {QStringLiteral("command:toggleCharging"), QStringLiteral("HomePresetActions.toggleCharging")},
        {QStringLiteral("command:tempToggleSteam"), QStringLiteral("HomePresetActions.toggleSteamHeater")},
        {QStringLiteral("command:disconnectDE1"), QStringLiteral("HomePresetActions.disconnectMachine")},
        {QStringLiteral("command:previousProfile"), QStringLiteral("HomePresetActions.loadPreviousProfile")},
        {QStringLiteral("command:tare"), QStringLiteral("SteamService.request")},
        {QStringLiteral("command:uploadVisualizer"), QStringLiteral("ShotReviewActions.uploadShot")},
        {QStringLiteral("command:loadProfile:example.json"), QStringLiteral("HomePresetActions.loadProfile")},
    };
    for (const auto& [action, expected] : cases) {
        const QJSValue result = run.call({action});
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        QVERIFY2(result.toString().contains(expected),
                 qPrintable(QStringLiteral("%1 did not use %2").arg(action, expected)));
    }
}

void TestCustomWidgetHtml::homePresetActionsRunShippingSelectionAndStartPolicy()
{
    const QString src = readSource(QStringLiteral("/qml/components/HomePresetActions.qml"));
    QVERIFY2(!src.isEmpty(), "could not read HomePresetActions.qml");

    QString functions;
    for (const QString& name : {
             QStringLiteral("trackControl"), QStringLiteral("loadProfile"),
             QStringLiteral("tapProfile"), QStringLiteral("startEspresso"),
             QStringLiteral("tapSteamPitcher"),
             QStringLiteral("selectSteamPitcher"), QStringLiteral("startSteam"),
             QStringLiteral("tapWaterVessel"), QStringLiteral("startHotWater"),
             QStringLiteral("tapFlushPreset"), QStringLiteral("startFlush") }) {
        QString fn = liftFunction(src, QStringLiteral("function %1(").arg(name));
        QVERIFY2(!fn.isEmpty(), qPrintable(QStringLiteral("%1 moved or disappeared").arg(name)));
        functions += stripQmlTypeAnnotations(fn) + QLatin1Char('\n');
    }

    QJSEngine engine;
    const QString setup = QString::fromUtf8(R"JS(
        var calls = [];
        var pendingControl = [];
        var nextId = 0;
        function record(name, args) {
            calls.push({name: name, args: args});
            return "request-" + (++nextId);
        }
        var ProfileService = {
            currentProfileSummary: {filename: "current.json"},
            loadProfile: function(filename) { return record("loadProfile", [filename]); }
        };
        var MachineControlService = {
            selectedRecipeId: 71,
            startRecipe: function(context, id) { return record("startRecipe", [context, id]); },
            startEspresso: function(context, filename) { return record("startEspresso", [context, filename]); },
            selectSteamPitcher: function(index, snapshot) { return record("selectSteamPitcher", [index, snapshot]); },
            startSteam: function(context, index, snapshot) { return record("startSteam", [context, index, snapshot]); },
            selectWaterVessel: function(index, snapshot) { return record("selectWaterVessel", [index, snapshot]); },
            startHotWater: function(context, index, snapshot) { return record("startHotWater", [context, index, snapshot]); },
            selectFlushPreset: function(index, snapshot) { return record("selectFlushPreset", [index, snapshot]); },
            startFlush: function(context, index, snapshot) { return record("startFlush", [context, index, snapshot]); }
        };
        var pitcherRows = [
            {name: "Small", flow: 4, temperature: 55},
            {name: "Large", flow: 5, temperature: 60},
            {name: "Heater off", disabled: true}
        ];
        var SteamService = {
            state: {controlContextId: "context-A", selectedSteamPitcher: 0,
                    selectedSteamPitcherDisplayIndex: 0},
            getSteamPitcherPreset: function(index) { return pitcherRows[index] || {}; }
        };
        var waterRows = [
            {name: "Cup", volume: 120, mode: "weight", flowRate: 40},
            {name: "Mug", volume: 250, mode: "weight", flowRate: 35}
        ];
        var flushRows = [
            {name: "Short", flow: 5.0, seconds: 2.0},
            {name: "Long", flow: 6.0, seconds: 4.0}
        ];
        var RecipeContext = {preferences: {
            selectedWaterVessel: 0, waterVesselPresets: waterRows,
            selectedFlushPreset: 0, flushPresets: flushRows
        }};
        var TranslationManager = {translate: function(key, fallback) { return fallback; }};
    )JS") + functions;
    const QJSValue evaluated = engine.evaluate(setup);
    QVERIFY2(!evaluated.isError(), qPrintable(evaluated.toString()));

    auto run = [&engine](const QString& expression) {
        return engine.evaluate(expression);
    };
    auto calls = [&engine] {
        const QJSValue value = engine.evaluate(QStringLiteral("JSON.stringify(calls)"));
        return QJsonDocument::fromJson(value.toString().toUtf8()).array();
    };
    auto expectCalls = [&calls](const QJsonArray& expected) {
        QCOMPARE(calls(), expected);
    };

    QVERIFY(!run(QStringLiteral("tapProfile('current.json')")).isError());
    expectCalls(QJsonArray{QJsonObject{
        {QStringLiteral("name"), QStringLiteral("startRecipe")},
        {QStringLiteral("args"), QJsonArray{QStringLiteral("context-A"), 71}}
    }});

    run(QStringLiteral("calls = []; MachineControlService.selectedRecipeId = -1; tapProfile('current.json')"));
    expectCalls(QJsonArray{QJsonObject{
        {QStringLiteral("name"), QStringLiteral("startEspresso")},
        {QStringLiteral("args"), QJsonArray{QStringLiteral("context-A"), QStringLiteral("current.json")}}
    }});

    run(QStringLiteral("calls = []; tapSteamPitcher(1)"));
    expectCalls(QJsonArray{QJsonObject{
        {QStringLiteral("name"), QStringLiteral("selectSteamPitcher")},
        {QStringLiteral("args"), QJsonArray{1, QJsonObject{
            {QStringLiteral("name"), QStringLiteral("Large")},
            {QStringLiteral("flow"), 5}, {QStringLiteral("temperature"), 60}
        }}}
    }});

    run(QStringLiteral("calls = []; SteamService.state.selectedSteamPitcher = 1; "
                       "SteamService.state.selectedSteamPitcherDisplayIndex = 1; tapSteamPitcher(1)"));
    expectCalls(QJsonArray{QJsonObject{
        {QStringLiteral("name"), QStringLiteral("startSteam")},
        {QStringLiteral("args"), QJsonArray{QStringLiteral("context-A"), 1, QJsonObject{
            {QStringLiteral("name"), QStringLiteral("Large")},
            {QStringLiteral("flow"), 5}, {QStringLiteral("temperature"), 60}
        }}}
    }});

    run(QStringLiteral("calls = []; SteamService.state.selectedSteamPitcher = -1; "
                       "SteamService.state.selectedSteamPitcherDisplayIndex = 2; tapSteamPitcher(2)"));
    expectCalls(QJsonArray{QJsonObject{
        {QStringLiteral("name"), QStringLiteral("selectSteamPitcher")},
        {QStringLiteral("args"), QJsonArray{2, QJsonObject{
            {QStringLiteral("name"), QStringLiteral("Heater off")},
            {QStringLiteral("disabled"), true}
        }}}
    }});

    run(QStringLiteral("calls = []; tapWaterVessel(1)"));
    expectCalls(QJsonArray{QJsonObject{
        {QStringLiteral("name"), QStringLiteral("selectWaterVessel")},
        {QStringLiteral("args"), QJsonArray{1, QJsonObject{
            {QStringLiteral("name"), QStringLiteral("Mug")},
            {QStringLiteral("volume"), 250}, {QStringLiteral("mode"), QStringLiteral("weight")},
            {QStringLiteral("flowRate"), 35}
        }}}
    }});
    run(QStringLiteral("calls = []; RecipeContext.preferences.selectedWaterVessel = 1; tapWaterVessel(1)"));
    expectCalls(QJsonArray{QJsonObject{
        {QStringLiteral("name"), QStringLiteral("startHotWater")},
        {QStringLiteral("args"), QJsonArray{QStringLiteral("context-A"), 1, QJsonObject{
            {QStringLiteral("name"), QStringLiteral("Mug")},
            {QStringLiteral("volume"), 250}, {QStringLiteral("mode"), QStringLiteral("weight")},
            {QStringLiteral("flowRate"), 35}
        }}}
    }});

    run(QStringLiteral("calls = []; tapFlushPreset(1)"));
    expectCalls(QJsonArray{QJsonObject{
        {QStringLiteral("name"), QStringLiteral("selectFlushPreset")},
        {QStringLiteral("args"), QJsonArray{1, QJsonObject{
            {QStringLiteral("name"), QStringLiteral("Long")},
            {QStringLiteral("flow"), 6}, {QStringLiteral("seconds"), 4}
        }}}
    }});
    run(QStringLiteral("calls = []; RecipeContext.preferences.selectedFlushPreset = 1; tapFlushPreset(1)"));
    expectCalls(QJsonArray{QJsonObject{
        {QStringLiteral("name"), QStringLiteral("startFlush")},
        {QStringLiteral("args"), QJsonArray{QStringLiteral("context-A"), 1, QJsonObject{
            {QStringLiteral("name"), QStringLiteral("Long")},
            {QStringLiteral("flow"), 6}, {QStringLiteral("seconds"), 4}
        }}}
    }});
}

// A parameterized action cannot be authored by a surface that has no way to
// supply the parameter: the web editor's pickAction() stores the bare id, which
// CustomItem then rejects at dispatch. Centralizing the catalog is what put such
// an action within that editor's reach, so the exclusion has to hold.
void TestCustomWidgetHtml::webCatalogOmitsActionsTheWebCannotAuthor()
{
    const QJsonObject json = SettingsNetwork::layoutActionCatalogJson();
    const QJsonArray actions = json.value(QStringLiteral("actions")).toArray();
    const QJsonObject labels = json.value(QStringLiteral("labels")).toObject();

    QVERIFY2(actions.size() > 35, "the web action list is far shorter than the table");
    // Labels cover EVERY id, offered or not — that is what keeps a stored legacy
    // action from rendering as its raw string.
    QVERIFY2(labels.size() > actions.size(),
             "the label map should cover more ids than the picker offers (legacy aliases)");
    QVERIFY2(labels.contains(QStringLiteral("command:scanDE1")),
             "the legacy alias lost its label; a stored layout will show a raw id");

    QStringList offered;
    for (const QJsonValue &v : actions)
        offered << v.toObject().value(QStringLiteral("id")).toString();

    QVERIFY2(!offered.contains(QStringLiteral("command:loadProfile")),
             "the web picker is offering a parameterized action it cannot expand; picking it "
             "stores a bare id that CustomItem rejects");
    QVERIFY2(!offered.contains(QStringLiteral("command:scanDE1")),
             "a non-picker alias is being offered");
    // The in-app picker DOES offer it — the two surfaces differ deliberately, and
    // asserting only the absence above would also pass if the action vanished.
    QStringList inApp;
    for (const QVariant &v : SettingsNetwork::layoutActionCatalog())
        inApp << v.toMap().value(QStringLiteral("id")).toString();
    QVERIFY2(inApp.contains(QStringLiteral("command:loadProfile")),
             "the in-app picker lost Load Profile");
}

// `contexts` is a space-separated string in the table, so a typo ("idel") or an
// empty value costs nothing at compile time and silently removes the action from
// every picker — invisible to whoever typed it, since a stored layout keeps
// dispatching it correctly.
void TestCustomWidgetHtml::actionContextsAreDrawnFromTheKnownVocabulary()
{
    static const QSet<QString> kKnown{
        QStringLiteral("idle"), QStringLiteral("espresso"), QStringLiteral("steam"),
        QStringLiteral("hotwater"), QStringLiteral("flush"), QStringLiteral("all") };

    const QVariantList catalog = SettingsNetwork::layoutActionCatalog();
    QVERIFY(catalog.size() > 35);

    QStringList problems;
    QSet<QString> seenIds;
    for (const QVariant &v : catalog) {
        const QVariantMap a = v.toMap();
        const QString id = a.value(QStringLiteral("id")).toString();
        if (seenIds.contains(id))
            problems << id + QStringLiteral(" (duplicate id)");
        seenIds.insert(id);

        const QStringList ctx = a.value(QStringLiteral("contexts")).toStringList();
        if (ctx.isEmpty()) {
            problems << id + QStringLiteral(" (no contexts — unreachable in every picker)");
            continue;
        }
        for (const QString &c : ctx) {
            if (!kKnown.contains(c))
                problems << id + QStringLiteral(" (unknown context '%1')").arg(c);
        }
    }
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QStringLiteral(", "))));
}

// The two editors must READ the catalog, not carry their own copies. They each
// did carry one, and the copies drifted by sixteen entries before anyone
// noticed — nothing failed when they diverged, which is why they did. If a
// hand-written list reappears in either file, this fails rather than waiting for
// a user to notice an action missing from one surface.
void TestCustomWidgetHtml::neitherEditorHardCodesItsOwnActionList()
{
    const QString webSrc = readSource(SrcPath::kWebLayout);
    QVERIFY2(!webSrc.isEmpty(), "could not read shotserver_layout.cpp");
    // The CONSUMPTION sites, not just the name: a bare `contains("LAYOUT_...")`
    // is satisfied by the C++ injection line alone, so the catalog could be
    // injected into the page and then ignored while this still passed. The page
    // normalizes the injected object into `CATALOG` once, so that is what the
    // two readers derive from.
    QVERIFY2(webSrc.contains(QStringLiteral("var CATALOG = ")),
             "the web editor no longer normalizes the injected catalog");
    QVERIFY2(webSrc.contains(QStringLiteral("CATALOG.actions")),
             "the web editor's ACTIONS no longer derives from the injected catalog");
    QVERIFY2(webSrc.contains(QStringLiteral("CATALOG.labels")),
             "the web editor's label lookup no longer derives from the injected catalog");

    const QString popupSrc = readSource(SrcPath::kCustomEditor);
    QVERIFY2(!popupSrc.isEmpty(), "could not read CustomEditorPopup.qml");
    // The catalog read moved into the LayoutActions singleton when the built-in
    // widgets' options popup needed the same list — a second "read the catalog,
    // filter by context, translate" was the copy about to be written. So the
    // in-app editors must consume the singleton, and the singleton the catalog.
    const QString actionsSrc = readSource(
        SrcPath::kLayoutActions);
    QVERIFY2(!actionsSrc.isEmpty(), "could not read LayoutActions.qml");
    QVERIFY2(actionsSrc.contains(QStringLiteral("layoutActionCatalog()")),
             "LayoutActions no longer consumes SettingsNetwork::layoutActionCatalog()");
    QVERIFY2(popupSrc.contains(QStringLiteral("LayoutActions.picker")),
             "the Custom widget editor no longer gets its action list from LayoutActions");
    const QString readoutSrc = readSource(SrcPath::kReadoutOptions);
    QVERIFY2(!readoutSrc.isEmpty(), "could not read ReadoutOptionsPopup.qml");
    QVERIFY2(readoutSrc.contains(QStringLiteral("LayoutActions.picker")),
             "the built-in widgets' options popup no longer gets its action list from "
             "LayoutActions — it has grown a second copy");

    // An action id written as a literal ALONGSIDE a label is the copy shape.
    // Bare ids are fine and expected — CustomItem's dispatch, the compiled
    // action buttons in LayoutItemDelegate, `command:loadProfile:<file>`
    // assembly — so match only id-with-label pairs.
    static const QRegularExpression pairRe(
        QStringLiteral("\"(?:navigate|command):[A-Za-z0-9]+\"\\s*,\\s*label\\s*:"));
    QVERIFY2(!pairRe.match(webSrc).hasMatch(),
             "shotserver_layout.cpp has re-grown a hand-written {id, label} action list");
    static const QRegularExpression qmlPairRe(
        QStringLiteral("id:\\s*\"(?:navigate|command):[A-Za-z0-9]+\"\\s*,\\s*label\\s*:"));
    QVERIFY2(!qmlPairRe.match(popupSrc).hasMatch(),
             "CustomEditorPopup.qml has re-grown a hand-written {id, label} action list");
    // Same shape in an HTML <option> list, the form the two regexes above
    // cannot see.
    static const QRegularExpression optionRe(
        QStringLiteral("<option value=\"(?:navigate|command):[A-Za-z0-9]+\">"));
    QVERIFY2(!optionRe.match(webSrc).hasMatch(),
             "shotserver_layout.cpp has re-grown a hand-written <option> action list");
}

// The Custom widget's four History actions hand ShotHistoryPage an
// `initialFilter` map, which is forwarded to the storage layer's
// parseFilterMap(). A key the storage does not read is not an error anywhere: it
// is dropped in silence and the user gets an UNFILTERED list that looks like a
// filtered one. `bagID` for `bagId` would do it, and so would a key that was
// renamed on the C++ side alone.
//
// So every key those four helpers put in an initialFilter must either be read by
// parseFilterMap or be one of the two the page deliberately keeps for the banner
// and never sends to the query. Source-level for the same reason as the tests
// above: exercising the helpers needs Settings, MainController and ProfileManager
// live.
void TestCustomWidgetHtml::historyFilterKeysAreUnderstoodByTheStorageLayer()
{
    // The helpers moved with the dispatch into the LayoutActions singleton.
    const QString itemSrc = readSource(SrcPath::kLayoutActions);
    QVERIFY2(!itemSrc.isEmpty(), "could not read LayoutActions.qml");

    // Each helper is `function _xHistoryFilter() { ... }` — take the whole run of
    // them rather than brace-matching four times.
    const qsizetype from = itemSrc.indexOf(QStringLiteral("function _recipeHistoryFilter("));
    const qsizetype to = itemSrc.indexOf(QStringLiteral("function pickerActions("));
    QVERIFY2(from >= 0 && to > from,
             "the _*HistoryFilter helpers moved or were renamed; this test is now blind");
    const QString helpers = itemSrc.mid(from, to - from);

    // Every helper must be INSIDE that range. A fifth one added below
    // executeActionString would otherwise contribute no keys and be checked by
    // nothing, while the key-count floor below still passed on the existing four.
    QCOMPARE(helpers.count(QStringLiteral("HistoryFilter()")),
             itemSrc.count(QStringLiteral("HistoryFilter()")) - 4);  // 4 call sites in the switch

    // Keys assigned into a filter object, in either shape the helpers use:
    // `f.beanBrand = ...` and `{ bagId: id, bagLabel: label }`.
    QSet<QString> keys;
    static const QRegularExpression dotRe(QStringLiteral("\\bf\\.([A-Za-z][A-Za-z0-9]*)\\s*="));
    auto dotIt = dotRe.globalMatch(helpers);
    while (dotIt.hasNext())
        keys.insert(dotIt.next().captured(1));
    static const QRegularExpression literalRe(
        QStringLiteral("[\\{,]\\s*([A-Za-z][A-Za-z0-9]*)\\s*:"));
    const qsizetype litFrom = helpers.indexOf(QStringLiteral("initialFilter:"));
    QVERIFY(litFrom >= 0);
    auto litIt = literalRe.globalMatch(helpers, litFrom);
    while (litIt.hasNext()) {
        const QString k = litIt.next().captured(1);
        if (k != QLatin1String("initialFilter"))
            keys.insert(k);
    }
    // By NAME, not by count. A floor only defends against losing keys, and the
    // direction that matters here is the opposite one — a key added or renamed
    // while the existing seven stay put would sail past `size() >= 6`.
    for (const QString &expected : { QStringLiteral("recipeId"), QStringLiteral("recipeName"),
                                     QStringLiteral("beanBrand"), QStringLiteral("beanType"),
                                     QStringLiteral("bagId"), QStringLiteral("bagLabel"),
                                     QStringLiteral("profileName") }) {
        QVERIFY2(keys.contains(expected),
                 qPrintable(QStringLiteral("filter key '%1' is no longer produced by the helpers — "
                                           "renamed, or the scan stopped seeing it").arg(expected)));
    }

    const QString storageSrc =
        readSource(SrcPath::kStorageQueries);
    QVERIFY2(!storageSrc.isEmpty(), "could not read shothistorystorage_queries.cpp");

    // Read by ShotHistoryPage for the banner label and deliberately never
    // forwarded to the query — see the numericFields/filterFields loops there.
    static const QStringList kBannerOnly{ QStringLiteral("recipeName"), QStringLiteral("bagLabel") };

    QStringList unknown;
    for (const QString &k : keys) {
        if (kBannerOnly.contains(k))
            continue;
        if (storageSrc.contains(QStringLiteral("filterMap.value(\"%1\"").arg(k)))
            continue;
        unknown << k;
    }
    QVERIFY2(unknown.isEmpty(),
             qPrintable(QStringLiteral("initialFilter keys the storage layer never reads — these "
                                       "are dropped silently and yield an UNFILTERED list: ")
                        + unknown.join(QStringLiteral(", "))));

    // The page forwards initialFilter WHOLESALE to the query now, excluding only
    // the banner labels. So the check is the mirror of what it was: those two
    // names must be IN the exclusion list. If one fell out, recipeName in
    // particular is a real query term in parseFilterMap (the `recipe:` keyword's
    // substring match), so forwarding it would silently widen an exact id filter
    // into a name search — a wrong result set, not an error.
    //
    // Read from the exclusion list specifically rather than the whole file: both
    // names legitimately appear elsewhere in it, where filterByRecipe and the
    // banner CONSTRUCT or display a filter.
    const QString pageSrc = readSource(SrcPath::kShotHistoryPage);
    QVERIFY2(!pageSrc.isEmpty(), "could not read ShotHistoryPage.qml");
    const qsizetype at = pageSrc.indexOf(QStringLiteral("bannerOnlyKeys = ["));
    QVERIFY2(at >= 0, "bannerOnlyKeys not found in ShotHistoryPage.qml — the merge changed shape "
                      "and this test is now blind");
    const QString list = pageSrc.mid(at, pageSrc.indexOf(QLatin1Char(']'), at) - at);
    for (const QString &k : kBannerOnly) {
        QVERIFY2(list.contains(QStringLiteral("\"%1\"").arg(k)),
                 qPrintable(QStringLiteral("%1 is missing from bannerOnlyKeys — it is a display "
                                           "label and would now be forwarded to the query").arg(k)));
    }
}


// A widget's page destination must be declared in ONE place: the C++ reservation table.
// It used to be stated three times per widget — the compiled tile's longPressAction, the
// dedicated item's goToX(), and the table — three copies free to drift with nothing
// failing if they did. This is the gate that keeps them collapsed.
void TestCustomWidgetHtml::gestureDestinationsAreDeclaredExactlyOnce()
{
    // Every type that accepts gesture overrides is declared, and the one-slot types
    // resolve to a real catalog action.
    QStringList labelIds;
    for (const QVariant &v : SettingsNetwork::layoutActionCatalog())
        labelIds << v.toMap().value(QStringLiteral("id")).toString();
    const QVariantMap labels = SettingsNetwork::layoutActionLabels();

    static const QStringList kOneSlot{
        QStringLiteral("recipes"), QStringLiteral("beans"), QStringLiteral("steam"),
        QStringLiteral("hotwater"), QStringLiteral("flush"), QStringLiteral("espresso"),
        QStringLiteral("equipment") };
    static const QStringList kTwoSlot{
        QStringLiteral("history"), QStringLiteral("autofavorites"), QStringLiteral("settings") };

    for (const QString &t : kOneSlot) {
        QVERIFY2(SettingsNetwork::typeSupportsGestureOverrides(t), qPrintable(t));
        const QString reserved = SettingsNetwork::gestureReservedActionForType(t);
        QVERIFY2(!reserved.isEmpty(),
                 qPrintable(QStringLiteral("%1 reserves no destination; its page would become "
                                           "unreachable once a gesture is overridden").arg(t)));
        QVERIFY2(labels.contains(reserved),
                 qPrintable(QStringLiteral("%1 reserves '%2', which is not a catalog action")
                            .arg(t, reserved)));
    }
    for (const QString &t : kTwoSlot) {
        QVERIFY2(SettingsNetwork::typeSupportsGestureOverrides(t), qPrintable(t));
        QVERIFY2(SettingsNetwork::gestureReservedActionForType(t).isEmpty(),
                 qPrintable(QStringLiteral("%1 opens its page on TAP, so it must reserve nothing "
                                           "and leave both gestures free").arg(t)));
    }

    // No surface may re-state a destination. The compiled-tile table must not hand-write
    // a gesture action for a type that reserves one, and no dedicated item may call its
    // own navigation from a gesture handler.
    for (const QString &rel : { SrcPath::kLayoutActions, SrcPath::kItemDelegate }) {
        const QString src = readSource(rel);
        QVERIFY2(!src.isEmpty(), qPrintable("could not read " + rel));
        for (const QString &t : kOneSlot) {
            const QString reserved = SettingsNetwork::gestureReservedActionForType(t);
            QVERIFY2(!src.contains(QStringLiteral("longPressAction: \"%1\"").arg(reserved))
                         && !src.contains(QStringLiteral("doubleclickAction: \"%1\"").arg(reserved)),
                     qPrintable(QStringLiteral("%1 re-states %2's destination '%3'; it must come "
                                               "from the reservation table").arg(rel, t, reserved)));
        }
    }
}

// Ten widgets, two render formats. A file whose gesture handler still calls its own
// navigation directly ignores the user's override in that format only — visible nowhere
// except by using that widget in that zone.
void TestCustomWidgetHtml::everyGestureCapableTypeRoutesThroughTheSharedHelper()
{
    const QStringList &kFiles = kGestureWidgetFiles;

    QStringList problems;
    for (const QString &f : kFiles) {
        const QString src = readSource(SrcPath::widgetItem(f));
        if (src.isEmpty()) { problems << f + QStringLiteral(" (unreadable)"); continue; }
        for (const QString &handler : { QStringLiteral("onAccessibleLongPressed"),
                                        QStringLiteral("onAccessibleDoubleClicked") }) {
            const qsizetype at = src.indexOf(handler);
            if (at < 0) { problems << f + QStringLiteral(" (no %1)").arg(handler); continue; }
            const QString line = src.mid(at, src.indexOf(QLatin1Char('\n'), at) - at);
            if (!line.contains(QStringLiteral("LayoutActions.runGesture")))
                problems << QStringLiteral("%1.%2 does not route through LayoutActions")
                                .arg(f, handler);
        }
    }
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QStringLiteral("; "))));

    // Double-click support comes from ONE rule, on every widget and in both render
    // formats: LayoutActions.hasGesture, which resolves a stored action and the reserved
    // destination exactly as dispatch does. Unconditional support held every single tap
    // for the double-click interval on widgets whose double-tap did nothing (#1976); a
    // per-widget condition is how CustomItem once left its compiled form deaf until
    // configured. This pins the shared rule so neither can creep back.
    QStringList hardcoded;
    for (const QString &f : kFiles + QStringList{ QStringLiteral("CustomItem") }) {
        const QString src = readSource(SrcPath::widgetItem(f));
        if (src.isEmpty()) { hardcoded << f + QStringLiteral(" (unreadable)"); continue; }
        qsizetype at = 0;
        while ((at = src.indexOf(QStringLiteral("supportDoubleClick:"), at)) >= 0) {
            const QString line = src.mid(at, src.indexOf(QLatin1Char('\n'), at) - at);
            if (!line.contains(QStringLiteral("LayoutActions.hasGesture(root.modelData, \"doubleclickAction\")")))
                hardcoded << f + QStringLiteral(": ") + line.trimmed();
            at += 1;
        }
    }
    QVERIFY2(hardcoded.isEmpty(),
             qPrintable(QStringLiteral("supportDoubleClick must bind to "
                                       "LayoutActions.hasGesture(root.modelData, \"doubleclickAction\"): ")
                        + hardcoded.join(QStringLiteral("; "))));

    // And the dispatch itself stays in one place: CustomItem must delegate, not carry a
    // second copy of the switch.
    const QString custom = readSource(SrcPath::kCustomItem);
    QVERIFY2(custom.contains(QStringLiteral("LayoutActions.execute")),
             "CustomItem no longer delegates to the shared dispatch");
    QVERIFY2(!custom.contains(QStringLiteral("category === \"navigate\"")),
             "CustomItem has re-grown its own action dispatch");
}

// A gesture handler that cannot fire. AccessibleTapHandler gates its press timer on
// supportLongPress and its tap delay on supportDoubleClick, so a handler declared
// without the matching flag is dead code that reads as live: the gesture runs nothing
// and the release performs the widget's ordinary tap action instead. Three widgets
// shipped exactly that on long press, past a check that read the handler line and
// never asked whether it could run.
//
// Its own slot rather than more assertions on the routing test: QVERIFY2 aborts the
// slot, so a routing regression would hide a reachability one. A new slot in an
// existing file costs milliseconds; only a new FILE is expensive (CLAUDE.md).
void TestCustomWidgetHtml::everyGestureHandlerCanActuallyFire()
{
    // handler signal -> the flag that arms it, and the model key a conditional gate
    // must read. Accepting the KEY, not just any expression, is what catches the
    // realistic version of this bug: `modelData.longpressAction` (lower-case p) is a
    // gate that is false forever and would otherwise pass as "declared".
    struct Gesture { const char *handler; const char *flag; const char *key; };
    static const Gesture kGestures[] = {
        { "onAccessibleLongPressed",   "supportLongPress:",   "longPressAction" },
        { "onAccessibleDoubleClicked", "supportDoubleClick:", "doubleclickAction" },
    };

    QStringList problems;
    // CustomItem included: it is the render format EVERY one of these widgets takes in
    // a centre zone, so deleting either of its two flags kills the gesture for all ten
    // at once.
    for (const QString &f : kGestureWidgetFiles + QStringList{ QStringLiteral("CustomItem") }) {
        const QString src = readSource(SrcPath::widgetItem(f));
        if (src.isEmpty()) { problems << f + QStringLiteral(" (unreadable)"); continue; }
        for (const Gesture &g : kGestures) {
            const QString handler = QLatin1String(g.handler);
            qsizetype at = 0;
            while ((at = src.indexOf(handler, at)) >= 0) {
                // Anchor on the opening BRACE, not the bare type name: a comment that
                // merely mentions AccessibleTapHandler would otherwise be taken for the
                // handler's start and hide a flag declared above it. PageTitleItem.qml
                // has such a comment today, so this is a live shape, not a hypothetical.
                const qsizetype open =
                    src.lastIndexOf(QStringLiteral("AccessibleTapHandler {"), at);
                // Only the lines ABOVE the signal are scanned. QML does not care about
                // order, this check does, and the failure message says so -- a window
                // that ran past the signal would have to brace-match through comments
                // and string literals to know where the handler ends.
                bool armed = false;
                if (open >= 0) {
                    const QStringList lines = src.mid(open, at - open).split(QLatin1Char('\n'));
                    for (const QString &l : lines) {
                        const QString t = l.trimmed();
                        // Declared, not merely MENTIONED: these handlers explain the flag
                        // in prose directly above it, so a substring match over the block
                        // passes on the very files this was written to catch.
                        if (!t.startsWith(QLatin1String(g.flag)))
                            continue;
                        // And armed, not merely declared: `supportLongPress: false`, or a
                        // gate on a misspelled key, is the shipped bug with a line of
                        // code in front of it.
                        armed = t.contains(QLatin1String("true")) || t.contains(QLatin1String(g.key));
                        if (armed)
                            break;
                    }
                }
                if (!armed)
                    problems << QStringLiteral("%1.%2").arg(f, handler);
                at += 1;
            }
        }
    }
    QVERIFY2(problems.isEmpty(),
             qPrintable(QStringLiteral("gesture handler with no armed support flag above it "
                                       "in the same AccessibleTapHandler -- it can never fire "
                                       "(declare supportLongPress/supportDoubleClick before "
                                       "the handler, and not as a constant false): ")
                        + problems.join(QStringLiteral("; "))));
}

QString TestCustomWidgetHtml::viaJs(const QVariantList &segments)
{
    QJSValue arg = m_engine.toScriptValue(segments);
    QJSValue out = m_jsSegmentsToHtml.call(QJSValueList{arg});
    if (out.isError())
        return QStringLiteral("<JS ERROR: ") + out.toString() + QStringLiteral(">");
    return out.toString();
}

void TestCustomWidgetHtml::bothImplementationsAgree_data()
{
    QTest::addColumn<QVariantList>("segments");

    auto seg = [](const QString &text, const QVariantMap &extra = {}) {
        QVariantMap m = extra;
        m[QStringLiteral("text")] = text;
        return QVariant(m);
    };

    QTest::newRow("plain") << QVariantList{ seg(QStringLiteral("Hello")) };
    QTest::newRow("bold") << QVariantList{ seg(QStringLiteral("Hello"), {{QStringLiteral("bold"), true}}) };
    QTest::newRow("italic") << QVariantList{ seg(QStringLiteral("Hello"), {{QStringLiteral("italic"), true}}) };
    QTest::newRow("colour") << QVariantList{ seg(QStringLiteral("Hello"), {{QStringLiteral("color"), QStringLiteral("#00cc6d")}}) };
    QTest::newRow("size") << QVariantList{ seg(QStringLiteral("Hello"), {{QStringLiteral("size"), 28}}) };
    QTest::newRow("colour+size") << QVariantList{
        seg(QStringLiteral("Hello"), {{QStringLiteral("color"), QStringLiteral("#00cc6d")},
                                      {QStringLiteral("size"), 48}}) };
    QTest::newRow("newline") << QVariantList{ seg(QStringLiteral("a")), seg(QStringLiteral("\n")), seg(QStringLiteral("b")) };
    QTest::newRow("empty-skipped") << QVariantList{ seg(QString()), seg(QStringLiteral("x")) };
    QTest::newRow("ampersand") << QVariantList{ seg(QStringLiteral("Tea & Coffee")) };
    QTest::newRow("angle-brackets") << QVariantList{ seg(QStringLiteral("a <b> c")) };
    // The drift this test was written for: legal unescaped in element content, so both render
    // the same, but the two surfaces stored different bytes for the same widget.
    QTest::newRow("double-quote") << QVariantList{ seg(QStringLiteral("say \"hi\"")) };
    QTest::newRow("quote-inside-span") << QVariantList{
        seg(QStringLiteral("say \"hi\""), {{QStringLiteral("color"), QStringLiteral("#ff0000")}}) };
    QTest::newRow("token") << QVariantList{ seg(QStringLiteral("%STATE%")) };
    QTest::newRow("emoji") << QVariantList{ seg(QStringLiteral("café ☕")) };
    QTest::newRow("all-at-once") << QVariantList{
        seg(QStringLiteral("A&\"<"), {{QStringLiteral("bold"), true},
                                      {QStringLiteral("italic"), true},
                                      {QStringLiteral("color"), QStringLiteral("#123456")},
                                      {QStringLiteral("size"), 12}}) };
}

void TestCustomWidgetHtml::bothImplementationsAgree()
{
    QFETCH(QVariantList, segments);

    const QString cpp = DocumentFormatter::segmentsToHtml(segments);
    const QString js = viaJs(segments);

    QCOMPARE(js, cpp);
}

// A colour and size chosen in the editor survive document -> segments -> HTML. This is the
// path the custom-widget styling actually travels when saved.
void TestCustomWidgetHtml::roundTripPreservesExplicitColourAndSize()
{
    QTextDocument doc;
    QTextCursor cur(&doc);
    QTextCharFormat fmt;
    fmt.setForeground(QColor(QStringLiteral("#00cc6d")));
    fmt.setProperty(QTextFormat::FontPixelSize, 28);
    cur.insertText(QStringLiteral("Styled"), fmt);

    DocumentFormatter f;
    f.setTextDocumentForTesting(&doc);
    const QVariantList segments = f.toSegments();

    QCOMPARE(segments.size(), 1);
    const QVariantMap seg = segments.first().toMap();
    QCOMPARE(seg.value(QStringLiteral("text")).toString(), QStringLiteral("Styled"));
    QCOMPARE(seg.value(QStringLiteral("color")).toString(), QStringLiteral("#00cc6d"));
    QCOMPARE(seg.value(QStringLiteral("size")).toInt(), 28);

    const QString html = DocumentFormatter::segmentsToHtml(segments);
    QVERIFY2(html.contains(QStringLiteral("color:#00cc6d")), qPrintable(html));
    QVERIFY2(html.contains(QStringLiteral("font-size:28px")), qPrintable(html));
}

// The two states must be distinguishable, because they mean opposite things:
//
//   - NO stored colour is the "Default" state: the text follows the widget's theme colour.
//     That absence is load-bearing — emitting a colour here would pin every custom widget
//     and break dark mode.
//   - An explicitly chosen colour is stored, INCLUDING black. Black used to double as the
//     sentinel above, so picking black in the editor gave back theme-coloured text, which on
//     a dark theme is the opposite of what was asked for.
void TestCustomWidgetHtml::colourIsDroppedOnlyWhenUnset()
{
    QTextDocument doc;
    QTextCursor cur(&doc);
    cur.insertText(QStringLiteral("Unstyled"));

    DocumentFormatter f;
    f.setTextDocumentForTesting(&doc);
    QVariantList segments = f.toSegments();
    QCOMPARE(segments.size(), 1);
    QVERIFY2(!segments.first().toMap().contains(QStringLiteral("color")),
             "unstyled text must carry no colour, or every custom widget stops following "
             "the theme");
}

void TestCustomWidgetHtml::explicitBlackIsStored()
{
    QTextDocument doc;
    QTextCursor cur(&doc);
    QTextCharFormat fmt;
    fmt.setForeground(QColor(Qt::black));
    cur.insertText(QStringLiteral("Deliberate black"), fmt);

    DocumentFormatter f;
    f.setTextDocumentForTesting(&doc);
    const QVariantList segments = f.toSegments();
    QCOMPARE(segments.size(), 1);
    QCOMPARE(segments.first().toMap().value(QStringLiteral("color")).toString(),
             QStringLiteral("#000000"));
}

// "Default" is a distinct action, not a shade — mergeCharFormat cannot remove a property, so
// clearColorOnRange rewrites the affected fragments' formats.
void TestCustomWidgetHtml::clearColorReturnsToDefault()
{
    QTextDocument doc;
    QTextCursor cur(&doc);
    QTextCharFormat fmt;
    fmt.setForeground(QColor(QStringLiteral("#ff0000")));
    cur.insertText(QStringLiteral("Red"), fmt);

    DocumentFormatter f;
    f.setTextDocumentForTesting(&doc);
    QCOMPARE(f.toSegments().first().toMap().value(QStringLiteral("color")).toString(),
             QStringLiteral("#ff0000"));

    f.clearColorOnRange(0, 3);

    const QVariantList segments = f.toSegments();
    QCOMPARE(segments.size(), 1);
    QCOMPARE(segments.first().toMap().value(QStringLiteral("text")).toString(),
             QStringLiteral("Red"));
    QVERIFY2(!segments.first().toMap().contains(QStringLiteral("color")),
             "clearColorOnRange must remove the colour, not set one");
}

// Clearing one run must not touch its neighbours.
void TestCustomWidgetHtml::clearColorLeavesOtherRunsAlone()
{
    QTextDocument doc;
    QTextCursor cur(&doc);
    QTextCharFormat red;
    red.setForeground(QColor(QStringLiteral("#ff0000")));
    QTextCharFormat green;
    green.setForeground(QColor(QStringLiteral("#00ff00")));
    cur.insertText(QStringLiteral("AAA"), red);
    cur.insertText(QStringLiteral("BBB"), green);

    DocumentFormatter f;
    f.setTextDocumentForTesting(&doc);
    f.clearColorOnRange(0, 3);

    const QVariantList segments = f.toSegments();
    QCOMPARE(segments.size(), 2);
    QVERIFY2(!segments.at(0).toMap().contains(QStringLiteral("color")), "first run should be Default");
    QCOMPARE(segments.at(1).toMap().value(QStringLiteral("color")).toString(),
             QStringLiteral("#00ff00"));
}

// Runs the shipping LayoutActions.compiledTile() — the centre-zone Sleep tile — against the
// real C++ defaults, so the tile follows host Sleep state and instance options, then dispatches
// its action and long-press through the real execute().
void TestCustomWidgetHtml::compiledSleepTileFollowsItsOptions()
{
    const QString src = readSource(SrcPath::kLayoutActions);
    QVERIFY2(!src.isEmpty(), "could not read LayoutActions.qml");
    QString fns;
    for (const QString &name : { QStringLiteral("function _warn("), QStringLiteral("function execute("),
                                 QStringLiteral("function machineIsAsleep("),
                                 QStringLiteral("function sleepButtonAction("),
                                 QStringLiteral("function sleepButtonLabel("),
                                 QStringLiteral("function sleepOption("),
                                 QStringLiteral("function sleepLongPressAction("),
                                 QStringLiteral("function compiledTile(") }) {
        const QString fn = liftFunction(src, name);
        QVERIFY2(!fn.isEmpty(), qPrintable(QStringLiteral("%1 not found in LayoutActions.qml — "
                                                          "this test is now blind").arg(name)));
        fns += fn + QStringLiteral("\n");
    }

    const QString defaults = QString::fromUtf8(
        QJsonDocument(SettingsNetwork::sleepOptionDefaultsJson()).toJson(QJsonDocument::Compact));
    QJSEngine eng;
    const QJSValue tile = eng.evaluate(QString::fromUtf8(kRecorderPreamble) + QStringLiteral(
        "LayoutService = { sleepOptionDefaults: function() { return %1; } };"
        "MachineState = { phase: 'Ready', Phase: { Sleep: 'Sleep' } };"
        "TranslationManager = { translate: function(k, f) { return f; } };"
        "var Theme = {};"
        "%2\n"
        "(function(modelData, phase) {"
        "    MachineState.phase = phase;"
        "    var t = compiledTile('sleep', modelData, 'fill');"
        "    __calls = []; execute(t.action, {}); var actionRan = __calls.join('|');"
        "    __calls = []; execute(t.longPressAction, {});"
        "    return { action: t.action, content: t.content, actionRan: actionRan,"
        "             longPress: t.longPressAction, icon: t.emoji !== '', ran: __calls.join('|') };"
        "})")
        .arg(defaults, fns));
    QVERIFY2(tile.isCallable(), qPrintable(tile.toString()));

    struct Case { const char *instance; const char *longPress; const char *ran; bool icon; };
    const Case cases[] = {
        // The defaults: quit on, no confirmation, icon on.
        { "({})",                                   "command:quit",        "AppShell.immediateQuitRequested",               true  },
        { "({allowQuit: false})",                   "",                    "",                      true  },
        { "({allowQuit: false, confirmQuit: true})", "",                   "",                      true  },
        { "({confirmQuit: true})",                  "command:quitConfirm", "AppShell.quitRequested", true  },
        { "({showIcon: false})",                    "command:quit",        "AppShell.immediateQuitRequested",               false },
    };
    for (const Case &c : cases) {
        const QJSValue modelData = eng.evaluate(QString::fromLatin1(c.instance));
        const QJSValue awake = tile.call({ modelData, QJSValue(QStringLiteral("Ready")) });
        QVERIFY2(!awake.isError(), qPrintable(awake.toString()));
        QCOMPARE(awake.property(QStringLiteral("action")).toString(), QStringLiteral("command:sleep"));
        QCOMPARE(awake.property(QStringLiteral("content")).toString(), QStringLiteral("Sleep"));
        QCOMPARE(awake.property(QStringLiteral("actionRan")).toString(), QStringLiteral("HomePresetActions.sleepMachine"));
        QCOMPARE(awake.property(QStringLiteral("longPress")).toString(), QString::fromLatin1(c.longPress));
        QCOMPARE(awake.property(QStringLiteral("ran")).toString(), QString::fromLatin1(c.ran));
        QCOMPARE(awake.property(QStringLiteral("icon")).toBool(), c.icon);

        const QJSValue asleep = tile.call({ modelData, QJSValue(QStringLiteral("Sleep")) });
        QVERIFY2(!asleep.isError(), qPrintable(asleep.toString()));
        QCOMPARE(asleep.property(QStringLiteral("action")).toString(), QStringLiteral("command:wake"));
        QCOMPARE(asleep.property(QStringLiteral("content")).toString(), QStringLiteral("Wake"));
        QCOMPARE(asleep.property(QStringLiteral("actionRan")).toString(), QStringLiteral("HomePresetActions.wakeMachine"));
        // The long-press remains the configured Quit gesture in either power state.
        QCOMPARE(asleep.property(QStringLiteral("longPress")).toString(), QString::fromLatin1(c.longPress));
    }

    // The compact SleepItem takes its long-press from the same helper.
    const QString sleepItem = readSource(SrcPath::widgetItem(QStringLiteral("SleepItem")));
    QVERIFY2(sleepItem.contains(QStringLiteral("LayoutActions.sleepLongPressAction(")),
             "SleepItem no longer takes its long-press from LayoutActions.sleepLongPressAction()");
    QVERIFY2(sleepItem.contains(QStringLiteral("LayoutActions.sleepButtonAction()")),
             "compact SleepItem no longer follows the shared Sleep/Wake policy");
    const auto labelBinding = QRegularExpression(QStringLiteral(
        R"(\btext:\s*(root\.machineAsleep[\s\S]*?)\s+font:)" )).match(sleepItem);
    QVERIFY2(labelBinding.hasMatch(), "compact SleepItem label binding was not found");
    QJSValue compactRoot = eng.newObject();
    eng.globalObject().setProperty(QStringLiteral("root"), compactRoot);
    for (bool asleep : {false, true}) {
        compactRoot.setProperty(QStringLiteral("machineAsleep"), asleep);
        const QJSValue label = eng.evaluate(labelBinding.captured(1));
        QVERIFY2(!label.isError(), qPrintable(label.toString()));
        QCOMPARE(label.toString(), asleep ? QStringLiteral("Wake") : QStringLiteral("Sleep"));
    }

    // One table: neither caller keeps a hand copy of the tile entries.
    for (const QString &rel : { SrcPath::kItemDelegate, SrcPath::kLibraryItemCard }) {
        const QString caller = readSource(rel);
        QVERIFY2(caller.contains(QStringLiteral("LayoutActions.compiledTile(")),
                 qPrintable(rel + " no longer builds its tiles from LayoutActions.compiledTile()"));
        QVERIFY2(!caller.contains(QStringLiteral("qrc:/icons/sleep.svg")),
                 qPrintable(rel + " carries its own copy of the compiled-tile table"));
    }

    // And no other Sleep option reader keeps a literal default of its own.
    static const QRegularExpression literalDefault(QStringLiteral(
        "(allowQuit|confirmQuit|showIcon)\\s*[!=]==\\s*undefined\\)?\\s*\\?[^:;]{0,200}:\\s*(true|false)\\b"
        "|(allowQuit|confirmQuit|showIcon)\\s*===\\s*undefined\\)?\\s*\\?\\s*(true|false)\\b"
        "|property\\s+bool\\s+(allowQuit|confirmQuit|showIcon)\\s*:\\s*(true|false)\\b"));
    for (const QString &rel : { SrcPath::widgetItem(QStringLiteral("SleepItem")),
                                SrcPath::kSleepEditor, SrcPath::kWebLayout }) {
        const QString reader = readSource(rel);
        QVERIFY2(!reader.isEmpty(), qPrintable("could not read " + rel));
        QVERIFY2(!literalDefault.match(reader).hasMatch(),
                 qPrintable(rel + " hard-codes a Sleep option default"));
    }
}

void TestCustomWidgetHtml::sharedPresetDraftUsesCapturedRevisionAndExplicitReload()
{
    const QString src = readSource(SrcPath::kSharedPresetDraft);
    QVERIFY2(!src.isEmpty(), "could not read SharedPresetDraft.qml");

    QString methods;
    for (const QString &name : {QStringLiteral("function presetAt("),
                                QStringLiteral("function begin("),
                                QStringLiteral("function recordFailure("),
                                QStringLiteral("function reload("),
                                QStringLiteral("function setValue("),
                                QStringLiteral("function submit("),
                                QStringLiteral("function commit(")}) {
        const QString function = liftFunction(src, name);
        QVERIFY2(!function.isEmpty(), qPrintable(name + " missing from SharedPresetDraft.qml"));
        methods += function + QLatin1Char('\n');
    }

    QJSEngine engine;
    const QString harness = QStringLiteral(R"JS(
        var RecipeContext = {
            ready: true,
            revision: 12,
            preferences: { waterVesselPresets: [
                { name: "Small cup", volume: 180, mode: "weight", flowRate: 40, temperature: 92 }
            ], selectedWaterVessel: 0, waterTemperature: 92 },
            serial: 0,
            lastRequest: null,
            newRequestId: function() { return "request-" + (++this.serial); },
            executePresetEdit: function(id, domain, operation, fields, expectedRevision) {
                this.lastRequest = { id: id, domain: domain, operation: operation,
                                     fields: fields, expectedRevision: expectedRevision };
            }
        };
        var root = {
            domain: "waterVessel", waterVessel: true, presets: RecipeContext.preferences.waterVesselPresets,
            selectedIndex: 0, draftIndex: -1, draftRevision: 0, expectedName: "", values: {},
            dirty: false, pending: false, stale: false, pendingRequestId: "",
            pendingOperation: "", errorMessage: "", completed: function() {}
        };
        %1
        root.presetAt = presetAt;
        root.begin = begin;
        root.recordFailure = recordFailure;
        root.reload = reload;
        root.setValue = setValue;
        root.submit = submit;
        root.commit = commit;
        (function() {
            root.begin(0);
            root.setValue("volume", 195);

            // A remote writer advances the canonical row after this draft began.
            RecipeContext.revision = 13;
            RecipeContext.preferences.waterVesselPresets = [
                { name: "Small cup", volume: 210, mode: "weight", flowRate: 45, temperature: 93 }
            ];
            root.presets = RecipeContext.preferences.waterVesselPresets;
            root.commit();
            var first = RecipeContext.lastRequest;
            root.pending = false;
            root.pendingRequestId = "";
            root.pendingOperation = "";
            root.recordFailure("stalePreferences");
            var staleDraftKept = root.stale && root.dirty && root.draftRevision === 12
                                 && root.values.volume === 195;

            // Only the explicit reload replaces the user's draft and captures revision 13.
            root.reload();
            var reloadedHostValue = root.values.volume;
            root.setValue("volume", 225);
            root.commit();
            var second = RecipeContext.lastRequest;
            return {
                firstRevision: first.expectedRevision,
                firstVolume: first.fields.volume,
                staleDraftKept: staleDraftKept,
                reloadedRevision: root.draftRevision,
                reloadedHostValue: reloadedHostValue,
                secondRevision: second.expectedRevision,
                secondVolume: second.fields.volume
            };
        })()
    )JS").arg(methods);
    const QJSValue result = engine.evaluate(harness);
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QCOMPARE(result.property(QStringLiteral("firstRevision")).toInt(), 12);
    QCOMPARE(result.property(QStringLiteral("firstVolume")).toInt(), 195);
    QVERIFY(result.property(QStringLiteral("staleDraftKept")).toBool());
    QCOMPARE(result.property(QStringLiteral("reloadedRevision")).toInt(), 13);
    QCOMPARE(result.property(QStringLiteral("reloadedHostValue")).toInt(), 210);
    QCOMPARE(result.property(QStringLiteral("secondRevision")).toInt(), 13);
    QCOMPARE(result.property(QStringLiteral("secondVolume")).toInt(), 225);
}

void TestCustomWidgetHtml::bagCardShippingActionsKeepBagAndDateIdentity()
{
    const QString source = readSource(QStringLiteral("/qml/components/BagCard.qml"));
    const QString freeze = liftHandler(source, QStringLiteral("id: freezeDatePicker"),
        QStringLiteral("onDateSelected:"), QStringLiteral("freezeDate"));
    const QString thaw = liftHandler(source, QStringLiteral("id: thawDatePicker"),
        QStringLiteral("onDateSelected:"), QStringLiteral("thawDate"));
    const QString refused = liftFunction(source, QStringLiteral("function onBagDeleted("));
    QVERIFY(!freeze.isEmpty() && !thaw.isEmpty() && !refused.isEmpty());
    QJSEngine engine;
    const auto result = engine.evaluate(QStringLiteral(R"JS(
        var card = {bag:{id:41}}, calls = [];
        var BagService = {requestUpdateBag:function(id, fields) {calls.push([id, fields]);}};
        var deleteRefusedText = {visible:false};
        var deleteRefusedTimer = {restarts:0, restart:function() {++this.restarts;}};
        %1
        %2
        %3
        freezeDate('2026-10-01');
        card.bag = {id:42}; thawDate('2026-10-02');
        onBagDeleted(41,false); onBagDeleted(42,true);
        var foreign = deleteRefusedText.visible;
        onBagDeleted(42,false);
        ({calls:calls,foreign:foreign,refused:deleteRefusedText.visible,restarts:deleteRefusedTimer.restarts})
    )JS").arg(freeze, thaw, refused));
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QCOMPARE(result.property(QStringLiteral("calls")).toVariant().toList(),
        QVariantList({QVariantList({41, QVariantMap{{"frozenDate", "2026-10-01"}}}),
                      QVariantList({42, QVariantMap{{"defrostDate", "2026-10-02"}}})}));
    QVERIFY(!result.property(QStringLiteral("foreign")).toBool());
    QVERIFY(result.property(QStringLiteral("refused")).toBool());
    QCOMPARE(result.property(QStringLiteral("restarts")).toInt(), 1);
}

void TestCustomWidgetHtml::recipeBagRepointAndRestockUseTheCapturedContext()
{
    const QString source = readSource(QStringLiteral("/qml/pages/RecipesPage.qml"));
    QString methods;
    const QList<QStringList> handlers{
        {"onStaleActionClicked:", "onStaleActionClicked:", "openRepoint"},
        {"id: bagRow", "onClicked:", "chooseRepoint"},
        {"id: restockDialog", "onBagSelected:", "restockSelected"},
        {"id: restockDialog", "onClosed:", "restockClosed"}
    };
    for (const auto& handler : handlers) {
        const QString method = liftHandler(source, handler[0], handler[1], handler[2]);
        QVERIFY2(!method.isEmpty(), qPrintable(handler[2]));
        methods += method + QLatin1Char('\n');
    }
    for (const QString& name : {QStringLiteral("restockRecipeBag"), QStringLiteral("onBagReady"),
                               QStringLiteral("onBagReadFailed"), QStringLiteral("onRecipeUpdated")}) {
        const QString method = liftFunction(source, QStringLiteral("function ") + name + QLatin1Char('('));
        QVERIFY2(!method.isEmpty(), qPrintable(name));
        methods += method + QLatin1Char('\n');
    }
    QJSEngine engine;
    const auto result = engine.evaluate(QStringLiteral(R"JS(
        var recipesPage=this, recipeCommandsEnabled=true;
        var _repointRecipe=null, _restockRecipe=null, _restockAwaitingBag=false;
        var _repointPendingId=-1, _restorePendingId=-1, _restoreFailReason='';
        var card={recipe:{id:7,bagId:41,grindPinned:'4.2'}}, bagRow={modelData:{id:42}};
        var repointPicker={opens:0,closes:0,open:function(){++this.opens;},close:function(){++this.closes;}};
        var restockDialog={bags:[],openRestock:function(bag){this.bags.push(bag);}};
        var BagService={reads:[],requestBag:function(id){this.reads.push(id);},
            requestActivateBag:function(){throw new Error('contextual relink activated a bag');}};
        var RecipeService={links:[],requestRelinkRecipeToBag:function(recipe,bag){this.links.push([recipe,bag]);}};
        var toasts=[], trRestockFailed={text:'restock read failed'}, trRepointFailed={text:'repoint failed'};
        function showToast(message){toasts.push(message);}
        %1
        openRepoint(); chooseRepoint();
        onRecipeUpdated(99,false); onRecipeUpdated(7,false);
        recipeCommandsEnabled=false; chooseRepoint();
        recipeCommandsEnabled=true; _repointRecipe=null; chooseRepoint();
        restockRecipeBag({id:8,bagId:51});
        onBagReady(99,{id:99});
        onBagReady(51,{id:51,coffeeName:'Captured bag'});
        onBagReady(51,{id:51,coffeeName:'Late duplicate'});
        restockSelected(52,{id:52}); onRecipeUpdated(8,true);

        restockRecipeBag({id:9,bagId:61}); restockClosed();
        onBagReady(61,{id:61}); restockSelected(62,{id:62});
        restockRecipeBag({id:10,bagId:71});
        recipeCommandsEnabled=false; restockSelected(72,{id:72}); restockClosed();
        recipeCommandsEnabled=true;
        restockRecipeBag({id:11,bagId:81}); onBagReadFailed(99); onBagReadFailed(81);
        restockRecipeBag({id:12,bagId:91}); onBagReady(91,{});
        ({links:RecipeService.links,reads:BagService.reads,forms:restockDialog.bags,
          opens:repointPicker.opens,closes:repointPicker.closes,toasts:toasts,
          waiting:_restockAwaitingBag,restockRecipe:_restockRecipe,
          pending:_repointPendingId,grind:card.recipe.grindPinned})
    )JS").arg(methods));
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    const auto links = result.property(QStringLiteral("links")).toVariant().toList();
    QCOMPARE(links.size(), 2);
    QCOMPARE(links[0].toList()[0].toInt(), 7);
    QCOMPARE(links[0].toList()[1].toInt(), 42);
    QCOMPARE(links[1].toList()[0].toInt(), 8);
    QCOMPARE(links[1].toList()[1].toInt(), 52);
    const auto reads = result.property(QStringLiteral("reads")).toVariant().toList();
    QCOMPARE(reads.size(), 5);
    for (int i = 0; i < reads.size(); ++i)
        QCOMPARE(reads[i].toInt(), 51 + i * 10);
    const auto forms = result.property(QStringLiteral("forms")).toVariant().toList();
    QCOMPARE(forms.size(), 1);
    QCOMPARE(forms[0].toMap().value("id").toInt(), 51);
    QCOMPARE(forms[0].toMap().value("coffeeName").toString(), QStringLiteral("Captured bag"));
    QCOMPARE(result.property(QStringLiteral("opens")).toInt(), 1);
    QCOMPARE(result.property(QStringLiteral("closes")).toInt(), 3);
    QCOMPARE(result.property(QStringLiteral("toasts")).toVariant().toStringList(),
             QStringList({QStringLiteral("repoint failed"), QStringLiteral("restock read failed"),
                          QStringLiteral("restock read failed")}));
    QVERIFY(!result.property(QStringLiteral("waiting")).toBool());
    QVERIFY(result.property(QStringLiteral("restockRecipe")).isNull());
    QCOMPARE(result.property(QStringLiteral("pending")).toInt(), -1);
    QCOMPARE(result.property(QStringLiteral("grind")).toString(), QStringLiteral("4.2"));
}

void TestCustomWidgetHtml::beanFormRefusalAnnouncesWithoutSubmittingOrChangingDraft()
{
    const QString source = readSource(QStringLiteral("/qml/components/ChangeBeansDialog.qml"));
    const QString confirm = liftFunction(source, QStringLiteral("function confirmForm("));
    QString changed = liftFunction(source, QStringLiteral("onErrorMessageChanged: {"));
    QVERIFY(!confirm.isEmpty());
    QVERIFY(!changed.isEmpty());
    changed.replace(0, changed.indexOf(QLatin1Char('{')), QStringLiteral("function errorChanged() "));
    QJSEngine engine;
    const auto result = engine.evaluate(QStringLiteral(R"JS(
        var root = this, visible = true, fRoaster = '  ', fCoffee = '\t';
        var commits = 0, errorValue = '';
        var Keyboard = {commit: function() { ++commits; }};
        var TranslationManager = {translate: function(key, fallback) { return fallback; }};
        var AccessibilityManager = {enabled: true, announcements: [],
            announce: function(message) { this.announcements.push(message); }};
        var BeanUiService = {bagImageKey: function() { throw new Error('invalid form reached image work'); }};
        var BagService = {requestCreateBag: function() { throw new Error('invalid form submitted'); }};
        %1
        %2
        Object.defineProperty(root, 'errorMessage', {
            get: function() { return errorValue; },
            set: function(next) {
                if (errorValue === next) return;
                errorValue = next;
                errorChanged();
            }
        });
        confirmForm();
        var firstError = errorMessage;
        confirmForm(); // A repeated refusal must be announced again.
        var validationAnnouncements = AccessibilityManager.announcements.slice();
        errorMessage = '';
        visible = false;
        errorMessage = 'hidden late failure';
        visible = true;
        errorMessage = 'Could not save the bag changes.';
        AccessibilityManager.enabled = false;
        errorMessage = 'accessibility disabled';
        ({firstError: firstError, validationAnnouncements: validationAnnouncements,
          announcements: AccessibilityManager.announcements, commits: commits,
          roaster: fRoaster, coffee: fCoffee})
    )JS").arg(confirm, changed));
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QCOMPARE(result.property(QStringLiteral("firstError")).toString(), QStringLiteral("Enter a roaster or coffee name"));
    const auto validation = result.property(QStringLiteral("validationAnnouncements"));
    QCOMPARE(validation.property(QStringLiteral("length")).toInt(), 2);
    QCOMPARE(validation.property(0).toString(), result.property(QStringLiteral("firstError")).toString());
    QCOMPARE(validation.property(1).toString(), result.property(QStringLiteral("firstError")).toString());
    const auto announcements = result.property(QStringLiteral("announcements"));
    QCOMPARE(announcements.property(QStringLiteral("length")).toInt(), 3);
    QCOMPARE(announcements.property(2).toString(), QStringLiteral("Could not save the bag changes."));
    QCOMPARE(result.property(QStringLiteral("commits")).toInt(), 2);
    QCOMPARE(result.property(QStringLiteral("roaster")).toString(), QStringLiteral("  "));
    QCOMPARE(result.property(QStringLiteral("coffee")).toString(), QStringLiteral("\t"));
}

void TestCustomWidgetHtml::sharedMediaImportRunsShippingAdmissionCancelAndCompletionHandlers()
{
    const QString source = readSource(QStringLiteral("/qml/pages/settings/SettingsScreensaverTab.qml"));
    QString methods;
    for (const QString& signature : {QStringLiteral("function acceptMediaFile("),
                                    QStringLiteral("function finishMediaUpload("),
                                    QStringLiteral("function releaseMediaTransfer("),
                                    QStringLiteral("function cancelMediaUpload("),
                                    QStringLiteral("function onCompleted(")}) {
        const QString method = liftFunction(source, signature);
        QVERIFY2(!method.isEmpty(), qPrintable(signature));
        methods += method + QLatin1Char('\n');
    }
    QVERIFY(source.contains(QStringLiteral("onAccepted: screensaverTab.acceptMediaFile(selectedFile)")));
    QVERIFY(source.contains(QStringLiteral("screensaverTab.finishMediaUpload(success, result, reason)")));
    QJSEngine engine;
    const QJSValue result = engine.evaluate(QStringLiteral(R"JS(
        var ScreensaverService = {ready:true, imports:[], cancels:[],
            importMedia:function(ticket) { this.imports.push(ticket); return "import-" + this.imports.length; },
            cancelMediaImport:function(id) { this.cancels.push(id); }};
        var ImportService = {ready:true,released:[],execute:function(operation,args) {this.released.push(args.transferId);}};
        var mediaImportBusy = false;
        var outcomes = [];
        var mediaUpload = {busy:false, starts:[], aborted:[],
            start:function(urls,kind,options) { this.starts.push({urls:urls,kind:kind}); return true; },
            abort:function(reason) { this.aborted.push(reason); this.busy=false; }};
        var screensaverTab = {mediaImportRequestId:"",mediaStagedTransferId:"",mediaCancellationRequested:false,
            pendingScreensaverOperationIds:[], showMediaImportResult:function(ok,reason) {outcomes.push([ok,reason]);},
            releaseMediaTransfer:function() {releaseMediaTransfer();}};
        // QML resolves these unqualified names against the enclosing root item.
        Object.defineProperties(this, {
            mediaImportRequestId: {get:function() {return screensaverTab.mediaImportRequestId;}},
            mediaCancellationRequested: {
                get:function() {return screensaverTab.mediaCancellationRequested;},
                set:function(value) {screensaverTab.mediaCancellationRequested=value;}}
        });
        var TranslationManager = {translate:function(key,fallback) {return fallback;}};
        var preferenceError = {show:function() {throw new Error("unowned completion was consumed");}};
        %1
        (function() {
            ScreensaverService.ready = false;
            var unavailable = acceptMediaFile("file:///local/Holiday.png");
            ScreensaverService.ready = true;
            mediaImportBusy = true;
            var busy = acceptMediaFile("file:///local/Holiday.png");
            mediaImportBusy = false;
            var accepted = acceptMediaFile("file:///local/Holiday.png");
            finishMediaUpload(false, {}, "invalidMedia");
            var failuresBeforeImport = outcomes.length;
            finishMediaUpload(true, {transferId:"opaque-complete-lease"}, "");
            onCompleted("someone-else", "import-media", true, {}, "");
            var pendingAfterForeign = screensaverTab.mediaImportRequestId;
            mediaUpload.busy = true;
            cancelMediaUpload();
            cancelMediaUpload();
            onCompleted("import-1", "import-media", false, {}, "cancelled");
            var pendingAfterCancel = screensaverTab.mediaImportRequestId;
            onCompleted("import-1", "import-media", true, {}, "");
            finishMediaUpload(true, {transferId:"retry-complete-lease"}, "");
            onCompleted("import-2", "import-media", true, {imported:1}, "");
            return {unavailable:unavailable,busy:busy,accepted:accepted,
                starts:mediaUpload.starts,imports:ScreensaverService.imports,cancels:ScreensaverService.cancels,
                aborted:mediaUpload.aborted,failuresBeforeImport:failuresBeforeImport,
                pendingAfterForeign:pendingAfterForeign,pendingAfterCancel:pendingAfterCancel,
                finalPending:screensaverTab.mediaImportRequestId,outcomes:outcomes,released:ImportService.released};
        })()
    )JS").arg(methods));
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QVERIFY(!result.property(QStringLiteral("unavailable")).toBool());
    QVERIFY(!result.property(QStringLiteral("busy")).toBool());
    QVERIFY(result.property(QStringLiteral("accepted")).toBool());
    QCOMPARE(result.property(QStringLiteral("starts")).property(QStringLiteral("length")).toInt(), 1);
    QCOMPARE(result.property(QStringLiteral("starts")).property(0).property(QStringLiteral("kind")).toString(), QStringLiteral("personalMediaFile"));
    QCOMPARE(result.property(QStringLiteral("imports")).property(0).toString(), QStringLiteral("opaque-complete-lease"));
    QCOMPARE(result.property(QStringLiteral("cancels")).property(QStringLiteral("length")).toInt(), 1);
    QCOMPARE(result.property(QStringLiteral("cancels")).property(0).toString(), QStringLiteral("import-1"));
    QCOMPARE(result.property(QStringLiteral("aborted")).property(0).toString(), QStringLiteral("cancelled"));
    QCOMPARE(result.property(QStringLiteral("pendingAfterForeign")).toString(), QStringLiteral("import-1"));
    QCOMPARE(result.property(QStringLiteral("pendingAfterCancel")).toString(), QString());
    QCOMPARE(result.property(QStringLiteral("finalPending")).toString(), QString());
    QCOMPARE(result.property(QStringLiteral("outcomes")).property(QStringLiteral("length")).toInt(), 3);
    QVERIFY(result.property(QStringLiteral("outcomes")).property(2).property(0).toBool());
    QCOMPARE(result.property(QStringLiteral("released")).property(QStringLiteral("length")).toInt(), 2);
    QCOMPARE(result.property(QStringLiteral("released")).property(0).toString(), QStringLiteral("opaque-complete-lease"));
}

void TestCustomWidgetHtml::brewDialogOwnSaveRebasesOnlyAcknowledgedFields()
{
    const QString src = readSource(SrcPath::kBrewDialog);
    QVERIFY2(!src.isEmpty(), "could not read BrewDialog.qml");
    QString methods;
    for (const QString& name : {QStringLiteral("function canRebaseSnapshot("),
                                QStringLiteral("function updateFromBrewSnapshot(")}) {
        const QString function = liftFunction(src, name);
        QVERIFY2(!function.isEmpty(), qPrintable(name + " missing from BrewDialog.qml"));
        methods += function + QLatin1Char('\n');
    }

    QJSEngine engine;
    const QString harness = QString::fromUtf8(R"JS(
        var TranslationManager = { translate: function(key, fallback) { return fallback; } };
        var previous = {
            revision: "old",
            activeBagId: 4,
            activeBagYieldValue: 100,
            activeBagYieldMode: "absolute",
            activeRecipeId: 7,
            doseG: 18,
            grindSetting: "3.2",
            profileTargetWeightG: 36,
            profileTemperatureC: 92,
            yieldMode: "none",
            yieldValue: 0,
            hasTemperatureOverride: true,
            temperatureC: 90,
            brewDialog: {
                activeBaselineYieldValue: 36,
                activeBaselineYieldMode: "absolute",
                activeBaselineTemperatureC: 90,
                activeBaselineYieldSource: "recipe",
                yieldPersistTarget: "recipe",
                activeRecipe: {name: "Cappuccino", yieldValue: 36,
                               yieldMode: "absolute"}
            }
        };
        var root = {
            brewSnapshot: previous,
            brewDialogSnapshot: previous.brewDialog,
            brewRevision: previous.revision,
            staleBrewSettings: true,
            _openedBagId: 4,
            doseValue: 19.5,
            targetValue: 155,
            ratio: 2.4,
            anchorMode: "absolute",
            yieldPersistTarget: "bag",
            recipeErrorText: "old error",
            showRecipeError: function(message) { this.recipeErrorText = message; }
        };
        // Match QML's unqualified property lookup on the enclosing object.
        with (root) {
            %1
            root.canRebaseSnapshot = canRebaseSnapshot;
            root.updateFromBrewSnapshot = updateFromBrewSnapshot;
        }
        (function() {
            var ownWrite = JSON.parse(JSON.stringify(previous));
            ownWrite.revision = "own-write";
            ownWrite.yieldMode = "absolute";
            ownWrite.yieldValue = 155;
            ownWrite.brewDialog.activeRecipe.yieldValue = 155;
            ownWrite.brewDialog.activeBaselineYieldValue = 155;
            root.updateFromBrewSnapshot(ownWrite, false, "recipe-yield",
                                        {yieldValue: 155, yieldMode: "absolute"});
            var rebased = !root.staleBrewSettings && root.brewRevision === "own-write"
                          && root.doseValue === 19.5 && root.targetValue === 155;

            var concurrentWrite = JSON.parse(JSON.stringify(ownWrite));
            concurrentWrite.revision = "other-write";
            concurrentWrite.grindSetting = "4.1";
            root.updateFromBrewSnapshot(concurrentWrite, false, "recipe-yield",
                                        {yieldValue: 155, yieldMode: "absolute"});
            var unrelatedYieldWriteRejected = root.staleBrewSettings
                                               && root.brewRevision === "own-write";

            var temperaturePrevious = JSON.parse(JSON.stringify(ownWrite));
            temperaturePrevious.revision = "before-temp";
            root.brewSnapshot = temperaturePrevious;
            root.brewRevision = "before-temp";
            root.staleBrewSettings = true;
            var ownTemperatureWrite = JSON.parse(JSON.stringify(temperaturePrevious));
            ownTemperatureWrite.revision = "own-temp-write";
            ownTemperatureWrite.hasTemperatureOverride = true;
            ownTemperatureWrite.temperatureC = 94;
            ownTemperatureWrite.brewDialog.activeBaselineTemperatureC = 94;
            root.updateFromBrewSnapshot(ownTemperatureWrite, false,
                                        "recipe-temperature", {tempOffsetC: 2});
            var rebasedTemperature = !root.staleBrewSettings
                && root.brewRevision === "own-temp-write"
                && root.doseValue === 19.5 && root.targetValue === 155;

            var concurrentTemperatureWrite = JSON.parse(JSON.stringify(ownTemperatureWrite));
            concurrentTemperatureWrite.revision = "other-temp-write";
            concurrentTemperatureWrite.doseG = 19;
            root.updateFromBrewSnapshot(concurrentTemperatureWrite, false,
                                        "recipe-temperature", {tempOffsetC: 2});
            return {
                rebasedOwnWrite: rebased,
                unrelatedWriteRejected: unrelatedYieldWriteRejected
                                         && root.doseValue === 19.5,
                unrelatedWriteError: root.recipeErrorText.length > 0,
                rebasedTemperatureWrite: rebasedTemperature,
                unrelatedTemperatureWriteRejected: root.staleBrewSettings
                                                    && root.brewRevision === "own-temp-write"
                                                    && root.doseValue === 19.5
            };
        })()
    )JS").arg(methods);
    const QJSValue result = engine.evaluate(harness);
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QVERIFY(result.property(QStringLiteral("rebasedOwnWrite")).toBool());
    QVERIFY(result.property(QStringLiteral("unrelatedWriteRejected")).toBool());
    QVERIFY(result.property(QStringLiteral("unrelatedWriteError")).toBool());
    QVERIFY(result.property(QStringLiteral("rebasedTemperatureWrite")).toBool());
    QVERIFY(result.property(QStringLiteral("unrelatedTemperatureWriteRejected")).toBool());
}

void TestCustomWidgetHtml::hotWaterAndFlushInputsStayBoundToSharedDrafts()
{
    const QString hotWater = readSource(SrcPath::kHotWaterPage);
    const QString flush = readSource(SrcPath::kFlushPage);
    QVERIFY2(!hotWater.isEmpty(), "could not read HotWaterPage.qml");
    QVERIFY2(!flush.isEmpty(), "could not read FlushPage.qml");

    // ValueInput.value is a live binding. Assigning it from a handler severs
    // that binding, so a later remote/native selection never reaches the view.
    static const QRegularExpression imperativeValueWrite(QStringLiteral(
        R"(\b(?:liveFlowRateInput|volumeInput|temperatureInput|flowRateInput|secondsInput|flowInput)\.value\s*=)"));
    QVERIFY2(!imperativeValueWrite.match(hotWater).hasMatch(),
             "Hot Water must update its draft source, not imperatively assign ValueInput.value");
    QVERIFY2(!imperativeValueWrite.match(flush).hasMatch(),
             "Flush must update its draft source, not imperatively assign ValueInput.value");

    for (const QString& binding : {
             QStringLiteral("value: page.liveFlowRateDraft"),
             QStringLiteral("value: page.getCurrentVesselVolume()"),
             QStringLiteral("value: Theme.cToDisplay(page.vesselTemperature)"),
             QStringLiteral("value: Number(presetDraft.values.flowRate")}) {
        QVERIFY2(hotWater.contains(binding),
                 qPrintable(QStringLiteral("Hot Water input lost binding: ") + binding));
    }
    QVERIFY2(flush.contains(QStringLiteral("value: flushPage.getCurrentPresetSeconds()")),
             "Flush duration must remain bound to the selected draft");
    QVERIFY2(flush.contains(QStringLiteral("value: flushPage.getCurrentPresetFlow()")),
             "Flush flow must remain bound to the selected draft");
}

void TestCustomWidgetHtml::grindDialInUsesOneCombinedPreferenceRequest()
{
    const QString src = readSource(SrcPath::kGrindQuickSelect);
    QVERIFY2(!src.isEmpty(), "could not read GrindQuickSelectItem.qml");
    QString function = liftFunction(src, QStringLiteral("function setGrindPreference("));
    QVERIFY2(!function.isEmpty(), "setGrindPreference missing from GrindQuickSelectItem.qml");
    function = stripQmlTypeAnnotations(function);

    QJSEngine engine;
    const QString harness = QStringLiteral(R"JS(
        var calls = [];
        var RecipeContext = {
            ready: true,
            revision: 12,
            newRequestId: function() { return "request-" + (calls.length + 1); },
            setPreference: function(id, name, value, revision) {
                calls.push({id:id, name:name, value:value, revision:revision});
            }
        };
        var HomePresetActions = { failed: function() { throw new Error("unexpected failure"); } };
        var TranslationManager = { translate: function(_key, fallback) { return fallback; } };
        var root = { _pendingPreferenceNames: {} };
        var _pendingPreferenceNames = root._pendingPreferenceNames;
        %1
        root.setGrindPreference = setGrindPreference;
        (function() {
            root.setGrindPreference({grindSetting:"5.2", rpm:1650});
            root.setGrindPreference({grindSetting:"5.3"});
            return {
                count: calls.length,
                firstName: calls[0].name,
                firstRevision: calls[0].revision,
                firstSetting: calls[0].value.dyeGrinderSetting,
                firstRpm: calls[0].value.dyeGrinderRpm,
                secondName: calls[1].name,
                secondSetting: calls[1].value.dyeGrinderSetting,
                secondHasRpm: Object.prototype.hasOwnProperty.call(calls[1].value, "dyeGrinderRpm")
            };
        })()
    )JS").arg(function);
    const QJSValue result = engine.evaluate(harness);
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QCOMPARE(result.property(QStringLiteral("count")).toInt(), 2);
    QCOMPARE(result.property(QStringLiteral("firstName")).toString(), QStringLiteral("grindDialIn"));
    QCOMPARE(result.property(QStringLiteral("firstRevision")).toInt(), 12);
    QCOMPARE(result.property(QStringLiteral("firstSetting")).toString(), QStringLiteral("5.2"));
    QCOMPARE(result.property(QStringLiteral("firstRpm")).toInt(), 1650);
    QCOMPARE(result.property(QStringLiteral("secondName")).toString(), QStringLiteral("grindDialIn"));
    QCOMPARE(result.property(QStringLiteral("secondSetting")).toString(), QStringLiteral("5.3"));
    QVERIFY(!result.property(QStringLiteral("secondHasRpm")).toBool());
}

void TestCustomWidgetHtml::profileEditorsRetainDraftsAcrossActivation()
{
    for (const QString& page : {QStringLiteral("ProfileEditorPage"),
                               QStringLiteral("SimpleProfileEditorPage"),
                               QStringLiteral("DFlowEditorPage")}) {
        const QString source = readSource(QStringLiteral("/qml/pages/%1.qml").arg(page));
        const QString load = liftFunction(source, QStringLiteral("function loadCurrentProfile("));
        QVERIFY2(!load.isEmpty(), qPrintable(page));
        QJSEngine engine;
        const QJSValue result = engine.evaluate(QStringLiteral(R"JS(
            var profile = null, detailRequestId = "", sourceRouteFilename = "";
            var sourceRevision = "", originalProfileName = "", selectedStepIndex = -1;
            var editorEntryFilename = "", editorEntryIsNew = true;
            var editorEntryType = "advanced", editorEntryTitle = "Draft", isFlow = false, params = {};
            var creates = 0, requests = 0;
            var TranslationManager = {translate: function(_key, fallback) { return fallback; }};
            var editorDraft = {
                createNewProfile: function() { ++creates; this.profile = {title: "Draft", steps: []}; },
                getOrConvertProfileParams: function() { return {}; }
            };
            var ProfileService = {
                requestCurrentProfile: function() { return "load-" + (++requests); },
                requestProfileDetail: function() { return "load-" + (++requests); }
            };
            function updateGraph() {}
            %1
            loadCurrentProfile();
            var retained = profile;
            profile.title = "Unsaved local edit";
            loadCurrentProfile();
            var kept = profile === retained && profile.title === "Unsaved local edit";
            profile = null;
            editorEntryIsNew = false;
            loadCurrentProfile();
            loadCurrentProfile();
            ({kept: kept, creates: creates, requests: requests})
        )JS").arg(load));
        QVERIFY2(!result.isError(), qPrintable(page + QStringLiteral(": ") + result.toString()));
        QVERIFY(result.property(QStringLiteral("kept")).toBool());
        QCOMPARE(result.property(QStringLiteral("creates")).toInt(), 1);
        QCOMPARE(result.property(QStringLiteral("requests")).toInt(), 1);
    }
}

void TestCustomWidgetHtml::qtSequenceSnapshotsRemainUsable()
{
    const QString import = readSource(QStringLiteral("/qml/pages/ProfileImportPage.qml"));
    const QRegularExpression expression(QStringLiteral(R"(readonly property var availableProfiles:\s*([^\n]+))"));
    const auto match = expression.match(import);
    QVERIFY(match.hasMatch());
    const QString wake = liftFunction(readSource(QStringLiteral("/qml/pages/settings/SettingsScreensaverTab.qml")),
                                      QStringLiteral("function updateAutoWakeDay("));
    const QString map = liftFunction(readSource(QStringLiteral("/qml/components/ShotMapScreensaver.qml")),
                                     QStringLiteral("function applyShotMapData("));
    QVERIFY(!wake.isEmpty());
    QVERIFY(!map.isEmpty());
    QmlSequenceFixture fixture;
    QJSEngine::setObjectOwnership(&fixture, QJSEngine::CppOwnership);
    QJSEngine engine;
    engine.globalObject().setProperty(QStringLiteral("fixture"), engine.newQObject(&fixture));
    const QJSValue result = engine.evaluate(QStringLiteral(R"JS(
        var importState = {availableProfiles: fixture.rows};
        var available = %1;
        var savedSchedule;
        function screenPreference() { return fixture.days; }
        function writeScreenPreference(name, value) { savedSchedule = value; return true; }
        var WebDebugLogger = {debug: function() {}};
        var shots = [], topProfiles = [], shotCount = 0;
        %2
        %3
        var updated = updateAutoWakeDay(3, {enabled:true, minute:25});
        applyShotMapData({shots:fixture.rows, top_profiles:fixture.rows});
        ({nativeArray: Array.isArray(fixture.rows), profiles:available.length,
          updated:updated, days:savedSchedule.length, enabled:savedSchedule[3].enabled,
          hour:savedSchedule[3].hour, minute:savedSchedule[3].minute,
          untouched:savedSchedule[2].enabled, shots:shotCount, top:topProfiles.length})
    )JS").arg(match.captured(1), wake, map));
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QVERIFY(!result.property(QStringLiteral("nativeArray")).toBool());
    QCOMPARE(result.property(QStringLiteral("profiles")).toInt(), 2);
    QVERIFY(result.property(QStringLiteral("updated")).toBool());
    QCOMPARE(result.property(QStringLiteral("days")).toInt(), 7);
    QVERIFY(result.property(QStringLiteral("enabled")).toBool());
    QCOMPARE(result.property(QStringLiteral("hour")).toInt(), 3);
    QCOMPARE(result.property(QStringLiteral("minute")).toInt(), 25);
    QVERIFY(!result.property(QStringLiteral("untouched")).toBool());
    QCOMPARE(result.property(QStringLiteral("shots")).toInt(), 2);
    QCOMPARE(result.property(QStringLiteral("top")).toInt(), 2);
}

void TestCustomWidgetHtml::visualizerTerminalFailureReleasesResolver()
{
    const QString source = readSource(QStringLiteral("/qml/components/VisualizerDuplicateResolver.qml"));
    QString dismiss = liftFunction(source, QStringLiteral("function dismiss("));
    dismiss.replace(QStringLiteral("function dismiss(): void"), QStringLiteral("function dismiss()"));
    const QString complete = liftFunction(source, QStringLiteral("function onOperationCompleted("));
    QVERIFY(!dismiss.isEmpty());
    QVERIFY(!complete.isEmpty());
    QJSEngine engine;
    const QJSValue result = engine.evaluate(QStringLiteral(R"JS(
        var visible = true, showNameInput = true, resolutionPending = true;
        var keyboardOffset = 50, requestId = "import-1";
        %1
        var resolver = { get requestId() { return requestId; }, dismiss:dismiss };
        %2
        onOperationCompleted("other", "import", false, {}, "networkError");
        var ignored = visible && resolutionPending;
        onOperationCompleted("import-1", "import", false, {}, "disconnected");
        ({ignored:ignored, visible:visible, pending:resolutionPending, requestId:requestId,
          keyboardOffset:keyboardOffset, nameInput:showNameInput})
    )JS").arg(dismiss, complete));
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QVERIFY(result.property(QStringLiteral("ignored")).toBool());
    QVERIFY(!result.property(QStringLiteral("visible")).toBool());
    QVERIFY(!result.property(QStringLiteral("pending")).toBool());
    QVERIFY(!result.property(QStringLiteral("nameInput")).toBool());
    QCOMPARE(result.property(QStringLiteral("requestId")).toString(), QString());
    QCOMPARE(result.property(QStringLiteral("keyboardOffset")).toInt(), 0);
}

void TestCustomWidgetHtml::steamBusyEditorReportsRefusal()
{
    const QString source = readSource(QStringLiteral("/qml/pages/SteamPage.qml"));
    const QString begin = liftFunction(source, QStringLiteral("function beginEditorRequest("));
    const QString finish = liftFunction(source, QStringLiteral("function finishEditorRequest("));
    const QString completed = liftFunction(source, QStringLiteral("function onCompleted("));
    QVERIFY(!begin.isEmpty());
    QVERIFY(!finish.isEmpty());
    QVERIFY(!completed.isEmpty());
    QJSEngine engine;
    const QJSValue result = engine.evaluate(QStringLiteral(R"JS(
        var pendingEditorRequestId = "", pendingEditorOperation = "", pendingEditorPresetName = "";
        var calls = [], errors = [];
        var TranslationManager = {translate:function(_key, fallback) { return fallback; }};
        var StackView = {Active:1};
        var steamPage = {StackView:{status:1}, pendingNavigationRequestId:"",
            get pendingEditorRequestId() { return pendingEditorRequestId; },
            actionFailureText:function(message) { return message; },
            showPitcherToast:function(message) { errors.push(message); }};
        var SteamService = {ready:true, busy:true, editorRevision:"revision", request:function(operation, payload, revision) {
            this.busy = true;
            calls.push({operation:operation, payload:payload, revision:revision}); return "edit-" + calls.length;
        }};
        Object.defineProperty(this, "editorReady", {get:function() {
            return SteamService.ready && !SteamService.busy && pendingEditorRequestId.length === 0;
        }});
        %1
        %2
        %3
        steamPage.finishEditorRequest = finishEditorRequest;
        var refused = beginEditorRequest("set-preset-duration", {durationSec:35}, "Small");
        SteamService.busy = false;
        var accepted = beginEditorRequest("set-preset-duration", {durationSec:40}, "Small");
        beginEditorRequest("set-preset-flow", {flow:120}, "Small");
        onCompleted("other", "steam-editor", true, "", {}, "revision");
        var heldUntilAck = pendingEditorRequestId === accepted && !editorReady;
        SteamService.busy = false;
        onCompleted(accepted, "steam-editor", false, "conflict", {}, "revision");
        var releasedAfterFailure = editorReady && pendingEditorRequestId === "";
        var retry = beginEditorRequest("set-preset-duration", {durationSec:45}, "Small");
        SteamService.busy = false;
        onCompleted(retry, "steam-editor", true, "", {}, "next-revision");
        ({refused:refused, accepted:accepted, errors:errors.length, requests:calls.length,
          heldUntilAck:heldUntilAck, releasedAfterFailure:releasedAfterFailure, ready:editorReady,
          duration:calls[1].payload.fields.durationSec, revision:calls[0].revision})
    )JS").arg(begin, finish, completed));
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QCOMPARE(result.property(QStringLiteral("refused")).toString(), QString());
    QCOMPARE(result.property(QStringLiteral("accepted")).toString(), QStringLiteral("edit-1"));
    QCOMPARE(result.property(QStringLiteral("errors")).toInt(), 3);
    QCOMPARE(result.property(QStringLiteral("requests")).toInt(), 2);
    QVERIFY(result.property(QStringLiteral("heldUntilAck")).toBool());
    QVERIFY(result.property(QStringLiteral("releasedAfterFailure")).toBool());
    QVERIFY(result.property(QStringLiteral("ready")).toBool());
    QCOMPARE(result.property(QStringLiteral("duration")).toInt(), 45);
    QCOMPARE(result.property(QStringLiteral("revision")).toString(), QStringLiteral("revision"));
}

void TestCustomWidgetHtml::updateTranslationToggleReportsAcknowledgedValue()
{
    const QString source = readSource(QStringLiteral("/qml/pages/settings/SettingsUpdateTab.qml"));
    const QString toggle = liftFunction(source, QStringLiteral("function toggleTranslationUpload("));
    const QString complete = liftFunction(source, QStringLiteral("function onOperationCompleted("));
    const QString completeUpdate = liftFunction(source, QStringLiteral("function completeUpdate("));
    QVERIFY(!toggle.isEmpty());
    QVERIFY(!complete.isEmpty());
    QVERIFY(!completeUpdate.isEmpty());
    QJSEngine engine;
    const QJSValue result = engine.evaluate(QStringLiteral(R"JS(
        var translationUploadRequestId = "", translationUploadTarget = false, developerTranslationUpload = false;
        var updateRequestId = "", updateOperation = "";
        var calls = [], messages = [];
        var UpdateService = {execute:function(operation, args) { calls.push(args.enabled); return "toggle-" + calls.length; }};
        var TranslationManager = {translate:function(_key, fallback) { return fallback; }};
        var AccessibilityManager = {announce:function() {}};
        var translationUploadToast = {show:function(message) { messages.push(message); }};
        var updateTab = {
            get translationUploadRequestId() { return translationUploadRequestId; },
            set translationUploadRequestId(value) { translationUploadRequestId = value; },
            get translationUploadTarget() { return translationUploadTarget; },
            completeUpdate: completeUpdate
        };
        %1
        %2
        %3
        toggleTranslationUpload();
        toggleTranslationUpload();
        onOperationCompleted("other", "setTranslationUploadEnabled", true, {}, "");
        var beforeAck = messages.length;
        onOperationCompleted("toggle-1", "setTranslationUploadEnabled", true, {}, "");
        developerTranslationUpload = true;
        toggleTranslationUpload();
        onOperationCompleted("toggle-2", "setTranslationUploadEnabled", false, {}, "disconnected");
        toggleTranslationUpload();
        onOperationCompleted("toggle-3", "setTranslationUploadEnabled", true, {}, "");
        ({calls:calls, beforeAck:beforeAck, messages:messages})
    )JS").arg(toggle, complete, completeUpdate));
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QCOMPARE(result.property(QStringLiteral("beforeAck")).toInt(), 0);
    const QJSValue calls = result.property(QStringLiteral("calls"));
    QCOMPARE(calls.property(QStringLiteral("length")).toInt(), 3);
    QVERIFY(calls.property(0).toBool());
    QVERIFY(!calls.property(1).toBool());
    const QJSValue messages = result.property(QStringLiteral("messages"));
    QVERIFY(messages.property(0).toString().startsWith(QStringLiteral("Translation upload enabled")));
    QVERIFY(messages.property(1).toString().startsWith(QStringLiteral("Could not change")));
    QCOMPARE(messages.property(2).toString(), QStringLiteral("Translation upload disabled."));
}

void TestCustomWidgetHtml::localApkUploadRequiresConfirmationAndCorrelatesHandoff()
{
    const QString source = readSource(QStringLiteral("/qml/pages/settings/SettingsDebugTab.qml"));
    QString handlers;
    for (const QString& name : {QStringLiteral("canSelectLocalApk"), QStringLiteral("selectLocalApk"),
            QStringLiteral("acceptLocalApk"), QStringLiteral("cancelLocalApkSelection"),
            QStringLiteral("uploadLocalApk"), QStringLiteral("localApkTransferred"),
            QStringLiteral("completeLocalApk"), QStringLiteral("finishLocalApk")}) {
        const QString body = liftFunction(source, QStringLiteral("function %1(").arg(name));
        QVERIFY2(!body.isEmpty(), qPrintable(name));
        handlers += body + QLatin1Char('\n');
    }
    handlers.replace(QStringLiteral("UpdateService"), QStringLiteral("service"));
    handlers.replace(QStringLiteral("ImportService"), QStringLiteral("imports"));
    handlers.replace(QStringLiteral("TranslationManager"), QStringLiteral("translator"));
    handlers.replace(QStringLiteral("AccessibilityManager"), QStringLiteral("accessibility"));
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(QStringLiteral(R"QML(
        import QtQml
        QtObject {
            id: debugTab
            property string localApkPhase: ""
            property string localApkFile: ""
            property string localApkRequestId: ""
            property string localApkTransferId: ""
            property bool localApkHostAvailable: true
            readonly property bool localApkBusy: localApkPhase !== ""
            property bool transferAdmitted: true
            property int pickerOpens: 0
            property int confirmations: 0
            property int transfers: 0
            property int installs: 0
            property int releases: 0
            property string uploadedKind: ""
            property string uploadedUrl: ""
            property string installedTicket: ""
            property string lastMessage: ""
            property QtObject service: QtObject {
                function execute(operation, args) {
                    if (operation !== "installLocalApk") throw Error("Unexpected operation")
                    ++debugTab.installs
                    debugTab.installedTicket = args.transferId
                    return "install-1"
                }
            }
            property QtObject imports: QtObject {
                function execute(operation, args) {
                    if (operation !== "cancelTransfer" || !args.transferId) throw Error("Unexpected release")
                    ++debugTab.releases
                }
            }
            property QtObject localApkPicker: QtObject { function open() { ++debugTab.pickerOpens } }
            property QtObject localApkConfirmation: QtObject {
                function open() { ++debugTab.confirmations }
                function close() { debugTab.cancelLocalApkSelection() }
            }
            property QtObject localApkTransfer: QtObject {
                function start(files, kind, options) {
                    ++debugTab.transfers
                    debugTab.uploadedKind = kind
                    debugTab.uploadedUrl = files[0]
                    return debugTab.transferAdmitted
                }
            }
            property QtObject localApkResult: QtObject {
                property bool error: false
                function show(message) { debugTab.lastMessage = message }
            }
            property QtObject translator: QtObject { function translate(key, fallback) { return fallback } }
            property QtObject accessibility: QtObject { function announce(message, urgent) {} }
            %1
        }
    )QML").arg(handlers).toUtf8(), QUrl());
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    const auto call = [&](const char* method) { return QMetaObject::invokeMethod(object.get(), method); };
    const auto select = [&] {
        if (!call("selectLocalApk")) return false;
        return QMetaObject::invokeMethod(object.get(), "acceptLocalApk",
            Q_ARG(QVariant, QVariant(QStringLiteral("file:///client/private.apk"))));
    };
    QVERIFY(select());
    QCOMPARE(object->property("localApkPhase").toString(), QStringLiteral("confirm"));
    QCOMPARE(object->property("installs").toInt(), 0);
    QCOMPARE(object->property("transfers").toInt(), 0);
    QVERIFY(call("cancelLocalApkSelection"));
    QCOMPARE(object->property("localApkPhase").toString(), QString());
    QCOMPARE(object->property("installs").toInt(), 0);
    QVERIFY(select());
    QVERIFY(call("uploadLocalApk"));
    QCOMPARE(object->property("localApkPhase").toString(), QStringLiteral("transfer"));
    QCOMPARE(object->property("uploadedKind").toString(), QStringLiteral("apkFile"));
    QCOMPARE(object->property("uploadedUrl").toString(), QStringLiteral("file:///client/private.apk"));
    QCOMPARE(object->property("installs").toInt(), 0);
    QVERIFY(QMetaObject::invokeMethod(object.get(), "localApkTransferred",
        Q_ARG(QVariant, true), Q_ARG(QVariant, QVariant(QVariantMap{{QStringLiteral("transferId"), QStringLiteral("opaque-ticket")}})),
        Q_ARG(QVariant, QString())));
    QCOMPARE(object->property("installs").toInt(), 1);
    QCOMPARE(object->property("installedTicket").toString(), QStringLiteral("opaque-ticket"));
    const auto complete = [&](const QString& request, bool success, const QString& reason) {
        return QMetaObject::invokeMethod(object.get(), "completeLocalApk", Q_ARG(QVariant, request),
            Q_ARG(QVariant, QStringLiteral("installLocalApk")), Q_ARG(QVariant, success), Q_ARG(QVariant, reason));
    };
    QVERIFY(complete(QStringLiteral("another-client"), true, {}));
    QCOMPARE(object->property("localApkPhase").toString(), QStringLiteral("request"));
    QVERIFY(complete(QStringLiteral("install-1"), false, QStringLiteral("disconnected")));
    QCOMPARE(object->property("localApkPhase").toString(), QString());
    QVERIFY(object->property("lastMessage").toString().contains(QStringLiteral("Check its screen before retrying")));
    QCOMPARE(object->property("releases").toInt(), 1);
    QCOMPARE(object->property("installs").toInt(), 1); // No automatic replay after uncertain handoff.
    QVERIFY(complete(QStringLiteral("install-1"), true, {}));
    QCOMPARE(object->property("installs").toInt(), 1);
    QVERIFY(object->setProperty("localApkHostAvailable", false));
    QVERIFY(call("selectLocalApk"));
    QCOMPARE(object->property("pickerOpens").toInt(), 2);
    QVERIFY(object->setProperty("localApkHostAvailable", true));
    QVERIFY(select());
    QVERIFY(object->setProperty("transferAdmitted", false));
    QVERIFY(call("uploadLocalApk"));
    QCOMPARE(object->property("localApkPhase").toString(), QString());
    QCOMPARE(object->property("installs").toInt(), 1);
}

void TestCustomWidgetHtml::sharedSupportClosureResumesNativePopupQueueAfterLastModal()
{
    const QString main = readSource(SrcPath::kMainQml);
    QString closed = liftFunction(main, QStringLiteral("onBlockingDialogClosed: {"));
    const QString next = liftFunction(main, QStringLiteral("function showNextPendingPopup("));
    QVERIFY(!closed.isEmpty());
    QVERIFY(!next.isEmpty());
    closed.replace(0, closed.indexOf(QLatin1Char('{')), QStringLiteral("function closed() "));
    const QString support = readSource(QStringLiteral("/qml/components/GlobalSupportDialogs.qml"));
    for (const QString& type : {QStringLiteral("CrashReportDialog"),
                               QStringLiteral("De1CommunicationErrorDialog"),
                               QStringLiteral("ProfileRefusedDialog")}) {
        const QString child = liftFunction(support, type + QStringLiteral(" {"));
        QVERIFY2(child.contains(QStringLiteral("onClosed: root.blockingDialogClosed()")),
                 qPrintable(type + QStringLiteral(" can strand a queued native prompt")));
    }
    QJSEngine engine;
    const QJSValue result = engine.evaluate(QStringLiteral(R"JS(
        var deferred = [], blocked = true, opened = [];
        var screensaverActive = false;
        var pendingPopups = [
            {id:"recipeActivationFailed", params:{missingProfileTitle:"First"}},
            {id:"recipeActivationFailed", params:{missingProfileTitle:"Second"}}
        ];
        var AppShell = {scaleDialogDeferred:false};
        var recipeActivationFailedDialog = {
            missingProfileTitle:"",
            open:function() { opened.push(this.missingProfileTitle); blocked = true; }
        };
        var Qt = {callLater:function(callback) { deferred.push(callback); }};
        var root = {anyModalDialogVisible:function() { return blocked; }};
        %1
        root.showNextPendingPopup = showNextPendingPopup;
        %2
        closed();
        deferred.shift()();
        var held = pendingPopups.length === 2 && opened.length === 0;
        closed();
        closed();
        // Reconciliation closes the last support dialog after its closed signal.
        blocked = false;
        while (deferred.length) deferred.shift()();
        var first = opened.join("|") === "First" && pendingPopups.length === 1;
        blocked = false;
        closed();
        deferred.shift()();
        ({held:held, first:first, opened:opened.join("|"), remaining:pendingPopups.length})
    )JS").arg(next, closed));
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QVERIFY(result.property(QStringLiteral("held")).toBool());
    QVERIFY(result.property(QStringLiteral("first")).toBool());
    QCOMPARE(result.property(QStringLiteral("opened")).toString(), QStringLiteral("First|Second"));
    QCOMPARE(result.property(QStringLiteral("remaining")).toInt(), 0);
}

void TestCustomWidgetHtml::settingsConversationRetainsRefusedDraftAndProjectsUnsavedState()
{
    const QString source = readSource(QStringLiteral("/qml/pages/settings/SettingsAITab.qml"));
    const QString send = liftFunction(source, QStringLiteral("function sendConversationMessage("));
    const QString complete = liftFunction(source, QStringLiteral("function completeConversationSend("));
    const QString discard = liftFunction(source, QStringLiteral("function discardConversationDraft("));
    const auto dirty = QRegularExpression(QStringLiteral(
        "readonly property bool browserHasUnsavedChanges:([^\\n]+\\n[^\\n]+)")).match(source);
    QVERIFY(!send.isEmpty());
    QVERIFY(!complete.isEmpty());
    QVERIFY(dirty.hasMatch());
    QVERIFY(!discard.isEmpty());
    QString leaveTab = liftFunction(source, QStringLiteral("function requestLeave("));
    QString confirmLeave = liftFunction(source, QStringLiteral("onDiscardClicked:"));
    QVERIFY(!leaveTab.isEmpty());
    QVERIFY(!confirmLeave.isEmpty());
    confirmLeave.replace(QStringLiteral("onDiscardClicked:"), QStringLiteral("function discardConversationAndLeave()"));
    QString handlers = send + QLatin1Char('\n') + complete + QLatin1Char('\n') + discard
        + QLatin1Char('\n') + leaveTab + QLatin1Char('\n') + confirmLeave;
    handlers.replace(QStringLiteral("ConversationSession"), QStringLiteral("session"));
    handlers.replace(QStringLiteral("Keyboard"), QStringLiteral("keyboard"));
    handlers.replace(QStringLiteral("TranslationManager"), QStringLiteral("translator"));
    handlers.replace(QStringLiteral("AccessibilityManager"), QStringLiteral("accessibility"));
    handlers.replace(QStringLiteral("AppShell"), QStringLiteral("shell"));
    const QString pageSource = readSource(QStringLiteral("/qml/pages/SettingsPage.qml"));
    const auto projection = QRegularExpression(QStringLiteral(
        "readonly property bool browserHasUnsavedChanges:\\s*([^\\n]+)")).match(pageSource);
    QVERIFY(projection.hasMatch());
    QString itemChanged = liftFunction(pageSource, QStringLiteral("onItemChanged:"));
    QVERIFY(!itemChanged.isEmpty());
    itemChanged.replace(QStringLiteral(" as SettingsAITab"), QString());
    QString leave = liftFunction(pageSource, QStringLiteral("function handleBack("));
    leave.replace(QStringLiteral("Keyboard"), QStringLiteral("keyboard"));
    leave.replace(QStringLiteral("AppShell"), QStringLiteral("shell"));
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(QStringLiteral(R"QML(
        import QtQml
        import QtQuick
        QtObject {
            id: aiTab
            property string pendingConversationSendRequestId: ""
            property string pendingConversationSendText: ""
            property string conversationRequestError: ""
            readonly property bool browserHasUnsavedChanges: %1
            property QtObject conversationInput: QtObject {
                property string text: ""
                property string preeditText: ""
            }
            property QtObject session: QtObject {
                property bool ready: true
                property bool busy: false
                property bool admitted: true
                property int calls: 0
                function request(operation, fields) {
                    if (operation !== "sendMessage") throw Error("Unexpected operation")
                    ++calls
                    return admitted ? "send-" + calls : ""
                }
            }
            property QtObject keyboard: QtObject { function commit() {} }
            property QtObject shell: settingsPage.shell
            property QtObject conversationExitDialog: QtObject { function open() { ++settingsPage.exitPrompts } }
            property QtObject translator: QtObject { function translate(key, fallback) { return fallback } }
            property QtObject accessibility: QtObject { function announce(message, urgent) {} }
            property Item settingsPage: Item {
                id: settingsPage
                property QtObject conversationTab: null
                property int tabCount: 0
                property bool tabActive: false
                property int backRequests: 0
                property int exitPrompts: 0
                property QtObject keyboard: aiTab.keyboard
                property QtObject shell: QtObject { function backRequested() { ++settingsPage.backRequests } }
                property QtObject exitDialog: QtObject { function open() { ++settingsPage.exitPrompts } }
                readonly property bool browserHasUnsavedChanges: %2
                Repeater {
                    model: settingsPage.tabCount
                    Loader {
                        readonly property string tabId: "ai"
                        active: settingsPage.tabActive
                        sourceComponent: Component {
                            QtObject {
                                readonly property bool browserHasUnsavedChanges: aiTab.browserHasUnsavedChanges
                                function requestLeave() { aiTab.requestLeave() }
                            }
                        }
                        %4
                    }
                }
                %5
            }
            %3
        }
    )QML").arg(dirty.captured(1), projection.captured(1), handlers, itemChanged, leave).toUtf8(), QUrl());
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto* input = object->property("conversationInput").value<QObject*>();
    auto* session = object->property("session").value<QObject*>();
    auto* page = object->property("settingsPage").value<QObject*>();
    QVERIFY(input && session && page);
    QVERIFY(!page->property("browserHasUnsavedChanges").toBool());
    QVERIFY(input->setProperty("text", QStringLiteral("Draft before lazy tab creation")));
    QVERIFY(page->setProperty("tabCount", 1));
    QVERIFY(!page->property("browserHasUnsavedChanges").toBool());
    QVERIFY(page->setProperty("tabActive", true));
    QTRY_VERIFY(page->property("browserHasUnsavedChanges").toBool());
    const auto sendMessage = [&] {
        QVariant result;
        if (!QMetaObject::invokeMethod(object.get(), "sendConversationMessage", Q_RETURN_ARG(QVariant, result))) return false;
        return result.toBool();
    };
    const auto finish = [&](const QString& id, bool success) {
        QVariant result;
        if (!QMetaObject::invokeMethod(object.get(), "completeConversationSend", Q_RETURN_ARG(QVariant, result),
                Q_ARG(QVariant, id), Q_ARG(QVariant, QStringLiteral("sendMessage")), Q_ARG(QVariant, success))) return false;
        return result.toBool();
    };
    QVERIFY(input->setProperty("text", QStringLiteral("Keep this question")));
    QVERIFY(page->property("browserHasUnsavedChanges").toBool());
    QVERIFY(sendMessage());
    QVERIFY(input->property("text").toString().isEmpty());
    QVERIFY(page->property("browserHasUnsavedChanges").toBool());
    QVERIFY(!sendMessage());
    QCOMPARE(session->property("calls").toInt(), 1);
    QVERIFY(QMetaObject::invokeMethod(page, "handleBack"));
    QCOMPARE(page->property("exitPrompts").toInt(), 1);
    QCOMPARE(page->property("backRequests").toInt(), 0);
    QVERIFY(QMetaObject::invokeMethod(object.get(), "discardConversationAndLeave"));
    QCOMPARE(page->property("backRequests").toInt(), 0); // A pending request cannot be discarded.
    QVERIFY(!finish(QStringLiteral("other-overlay"), false));
    QCOMPARE(object->property("pendingConversationSendRequestId").toString(), QStringLiteral("send-1"));
    QVERIFY(finish(QStringLiteral("send-1"), false));
    QCOMPARE(input->property("text").toString(), QStringLiteral("Keep this question"));
    QVERIFY(!object->property("conversationRequestError").toString().isEmpty());
    QVERIFY(page->property("browserHasUnsavedChanges").toBool());
    QVERIFY(sendMessage());
    QVERIFY(input->setProperty("text", QStringLiteral("Newer draft")));
    QVERIFY(finish(QStringLiteral("send-2"), false));
    QCOMPARE(input->property("text").toString(), QStringLiteral("Newer draft"));
    QVERIFY(sendMessage());
    QVERIFY(finish(QStringLiteral("send-3"), true));
    QVERIFY(!page->property("browserHasUnsavedChanges").toBool());
    QVERIFY(input->setProperty("preeditText", QStringLiteral("Composing")));
    QVERIFY(page->property("browserHasUnsavedChanges").toBool());
    QVERIFY(input->setProperty("preeditText", QString()));
    QVERIFY(input->setProperty("text", QStringLiteral("Offline draft")));
    QVERIFY(session->setProperty("ready", false));
    QVERIFY(!sendMessage());
    QCOMPARE(input->property("text").toString(), QStringLiteral("Offline draft"));
    QVERIFY(session->setProperty("ready", true));
    QVERIFY(session->setProperty("admitted", false));
    QVERIFY(!sendMessage());
    QCOMPARE(input->property("text").toString(), QStringLiteral("Offline draft"));
    QVERIFY(page->property("browserHasUnsavedChanges").toBool());
    QVERIFY(QMetaObject::invokeMethod(page, "handleBack"));
    QCOMPARE(page->property("backRequests").toInt(), 0);
    QVERIFY(QMetaObject::invokeMethod(object.get(), "discardConversationAndLeave"));
    QCOMPARE(page->property("backRequests").toInt(), 1);
    QVERIFY(input->property("text").toString().isEmpty());
    QVERIFY(!page->property("browserHasUnsavedChanges").toBool());
    QVERIFY(page->setProperty("tabActive", false));
    QTRY_VERIFY(page->property("conversationTab").value<QObject*>() == nullptr);
}

void TestCustomWidgetHtml::shotKnowledgeDoesNotUseCurrentProfileDifference()
{
    const QString source = readSource(QStringLiteral("/qml/components/ProfileKnowledgeDialog.qml"));
    const QString open = liftFunction(source, QStringLiteral("function openFor("));
    const QString shot = liftFunction(source, QStringLiteral("function openForShot("));
    const QString diff = liftFunction(source, QStringLiteral("function dialInDiffForContext("));
    QVERIFY(!open.isEmpty());
    QVERIFY(!shot.isEmpty());
    QVERIFY(!diff.isEmpty());
    QJSEngine engine;
    const QJSValue result = engine.evaluate(QStringLiteral(R"JS(
        var _shotKnowledge = false, profileTitle = "";
        function requestKnowledge() {}
        function open() {}
        %1
        %2
        %3
        var payload = {dialInDiff:{currentProfile:true}};
        openForShot("Historical shot", "");
        var shotDiff = dialInDiffForContext(payload);
        openFor("Catalog profile");
        var catalogDiff = dialInDiffForContext(payload);
        ({shotKeys:Object.keys(shotDiff).length, current:catalogDiff.currentProfile})
    )JS").arg(open, shot, diff));
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QCOMPARE(result.property(QStringLiteral("shotKeys")).toInt(), 0);
    QVERIFY(result.property(QStringLiteral("current")).toBool());
}

void TestCustomWidgetHtml::sensorCalibrationCapturesCurrentProfile()
{
    const QString begin = liftFunction(readSource(QStringLiteral("/qml/pages/SensorCalibrationPage.qml")),
                                       QStringLiteral("function beginCalibration("));
    QVERIFY(!begin.isEmpty());
    QJSEngine engine;
    const QJSValue result = engine.evaluate(QStringLiteral(R"JS(
        var loaded = "";
        var calibrationPage = {sensorInfo:{profileFilename:"Test.json"},
            _refreshContext:function() {}, _readCalibration:function() {}};
        var RecipeService = {activeRecipeId:-1};
        var SensorCalibrationService = {connected:true};
        var ProfileService = {previousProfileName:"Old.json", currentProfileSummary:{filename:"Selected.json"},
            loadProfile:function(filename) { loaded = filename; return "request"; }};
        %1
        beginCalibration();
        ({restore:calibrationPage._restoreProfileName, loaded:loaded})
    )JS").arg(begin));
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QCOMPARE(result.property(QStringLiteral("restore")).toString(), QStringLiteral("Selected.json"));
    QCOMPARE(result.property(QStringLiteral("loaded")).toString(), QStringLiteral("Test.json"));
}

void TestCustomWidgetHtml::privateSettingsFailuresReachTheErrorToast()
{
    // Factory Reset stays on History & Data; the authenticator, Remote Web Access
    // and Tailscale actions moved to the Network card on Connections.
    const auto handlerIn = [](const QString& file) {
        const QString source = readSource(file);
        const qsizetype start = source.indexOf(QStringLiteral("target: ShotServerSettingsService"));
        return start < 0 ? QString()
                         : liftFunction(source.mid(start), QStringLiteral("function onOperationCompleted("));
    };
    const QString dataHandler = handlerIn(QStringLiteral("/qml/pages/settings/SettingsHistoryDataTab.qml"));
    const QString networkHandler = handlerIn(QStringLiteral("/qml/pages/settings/SettingsConnectionsTab.qml"));
    QVERIFY(!dataHandler.isEmpty());
    QVERIFY(!networkHandler.isEmpty());

    QJSEngine engine;
    const QJSValue data = engine.evaluate(QStringLiteral(R"JS(
        var messages = [];
        var historyDataTab = {shotServerPrivateRequestId:"request", shotServerPrivateOperation:"factoryResetAndQuit"};
        var shotServerError = {show:function(message) { messages.push(message); }};
        var TranslationManager = {translate:function(_key, fallback) { return fallback; }};
        %1
        onOperationCompleted("request", "factoryResetAndQuit", false, {}, "disconnected");
        ({messages:messages.length, last:messages[0]})
    )JS").arg(dataHandler));
    QVERIFY2(!data.isError(), qPrintable(data.toString()));
    QCOMPARE(data.property(QStringLiteral("messages")).toInt(), 1);
    QVERIFY(data.property(QStringLiteral("last")).toString().contains(QStringLiteral("Check the host")));

    const QJSValue network = engine.evaluate(QStringLiteral(R"JS(
        var messages = [];
        var connectionsTab = {remoteUrlsRequestId:"", shotServerPrivateRequestId:"", shotServerPrivateOperation:""};
        var connectionOperationError = {show:function(message) { messages.push(message); }};
        var TranslationManager = {translate:function(_key, fallback) { return fallback; }};
        var AccessibilityManager = {announce:function() {}};
        %1
        for (const operation of ["generateTotpSetup", "resetTotpSecret", "rotateRemoteWebToken", "forgetTailscale"]) {
            connectionsTab.shotServerPrivateRequestId = "request";
            connectionsTab.shotServerPrivateOperation = operation;
            onOperationCompleted("request", operation, false, {}, "disconnected");
        }
        ({messages:messages.length})
    )JS").arg(networkHandler));
    QVERIFY2(!network.isError(), qPrintable(network.toString()));
    QCOMPARE(network.property(QStringLiteral("messages")).toInt(), 4);
}

void TestCustomWidgetHtml::completedBrowserReviewReplacesItsPredecessor()
{
    const QString source = readSource(QStringLiteral("/qml/webclient/Main.qml"));
    const QString review = liftFunction(source, QStringLiteral("function openPostShotReview("));
    QVERIFY(!review.isEmpty());
    QJSEngine engine;
    const QJSValue result = engine.evaluate(QStringLiteral(R"JS(
        var pages = [{objectName:"idlePage"}, {objectName:"shotHistoryPage"},
                     {objectName:"postShotReviewPage", editShotId:1}, {objectName:"espressoPage"}];
        var pageStack = {get currentItem() { return pages[pages.length - 1]; },
            get depth() { return pages.length; }, get:function(index) { return pages[index]; }};
        var navigation = {postShotReviewPage:"review"};
        var OperationNavigationPolicy = {isOperationPage:function(name) { return name === "espressoPage"; }};
        function startNavigation() { return true; }
        function popTrackedPageLocally() { pages.pop(); }
        function replaceTrackedPage(component, props) {
            pages[pages.length - 1] = {objectName:"postShotReviewPage", editShotId:props.editShotId};
        }
        function pushTrackedPage(component, props) { pages.push({objectName:"postShotReviewPage", editShotId:props.editShotId}); }
        %1
        openPostShotReview(2, true, true);
        var completed = pages.length === 3 && pages[1].objectName === "shotHistoryPage" && pages[2].editShotId === 2;
        openPostShotReview(3, true, true);
        var slowSave = pages.length === 3 && pages[2].editShotId === 3;
        openPostShotReview(4, false);
        ({completed:completed, slowSave:slowSave, explicitDepth:pages.length})
    )JS").arg(review));
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QVERIFY(result.property(QStringLiteral("completed")).toBool());
    QVERIFY(result.property(QStringLiteral("slowSave")).toBool());
    QCOMPARE(result.property(QStringLiteral("explicitDepth")).toInt(), 4);
}

void TestCustomWidgetHtml::browserAppBackUsesTheRetainedParent()
{
    const QString source = readSource(QStringLiteral("/qml/webclient/Main.qml"));
    const QString back = liftFunction(source, QStringLiteral("function goBack("));
    const QString complete = liftFunction(source, QStringLiteral("function onAppBackCompleted("));
    QVERIFY(!back.isEmpty());
    QVERIFY(!complete.isEmpty());
    QJSEngine engine;
    const QJSValue result = engine.evaluate(QStringLiteral(R"JS(
        var browserRouteStack = [{route:"home"}, {route:"history"}, {route:"postShotReview", args:{editShotId:2}}];
        var browserHistoryIndex = 3, browserHistorySessionId = "session", pendingBrowserPop = null, pendingAppBackStack = null;
        var pageStack = {depth:3}, traversals = [], applied;
        var AppShell = {consumeBrowserBack:function() { return false; }};
        var BrowserHistoryController = {goFromApp:function(delta) { traversals.push(delta); }};
        var BrowserRouteHistory = {parseState:JSON.parse};
        function startNavigation() { return true; }
        var window = {
            get pendingAppBackStack() { return pendingAppBackStack; },
            set pendingAppBackStack(value) { pendingAppBackStack = value; },
            browserHistorySessionId:browserHistorySessionId,
            get browserHistoryIndex() { return browserHistoryIndex; },
            set browserHistoryIndex(value) { browserHistoryIndex = value; },
            applyRouteStack:function(stack) { applied = stack; }, replaceBrowserState:function() {}
        };
        %1
        %2
        goBack();
        onAppBackCompleted(JSON.stringify({sessionId:"session", index:2, stack:[
            {route:"home"}, {route:"history"}, {route:"postShotReview", args:{editShotId:1}}
        ]}));
        ({depth:applied.length, parent:applied[1].route, index:browserHistoryIndex,
          pending:pendingAppBackStack, delta:traversals[0]})
    )JS").arg(back, complete));
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QCOMPARE(result.property(QStringLiteral("depth")).toInt(), 2);
    QCOMPARE(result.property(QStringLiteral("parent")).toString(), QStringLiteral("history"));
    QCOMPARE(result.property(QStringLiteral("index")).toInt(), 2);
    QVERIFY(result.property(QStringLiteral("pending")).isNull());
    QCOMPARE(result.property(QStringLiteral("delta")).toInt(), -1);
}

void TestCustomWidgetHtml::sharedEscapeWaitsForPageReleaseGuard()
{
    const QString source = readSource(QStringLiteral("/qml/components/SharedAppNavigation.qml"));
    const QString press = liftFunction(source, QStringLiteral("Keys.onEscapePressed:"));
    const QString release = liftFunction(source, QStringLiteral("Keys.onReleased:"));
    QVERIFY(!release.isEmpty());
    const QUrl policy = QUrl::fromLocalFile(QStringLiteral(DECENZA_SOURCE_DIR)
        + QStringLiteral("/qml/components/OperationNavigationPolicy.js"));
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QQuickWindow window;
    component.setData(QStringLiteral(R"QML(
        import QtQuick
        import "%1" as OperationNavigationPolicy
        Item {
            id: root
            width: 240; height: 180
            property int depth: 2
            property bool backOnKeyRelease: false
            property int backCount: 0
            function goBack() { ++backCount }
            %2
            %3
            Item {
                objectName: "guardedPage"
                anchors.fill: parent
                focus: true
                property int guardCount: 0
                Keys.onReleased: event => {
                    if (event.key === Qt.Key_Escape) {
                        event.accepted = true
                        ++guardCount
                    }
                }
            }
        }
    )QML").arg(policy.toString(), press, release).toUtf8(), QUrl());
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto* item = qobject_cast<QQuickItem*>(object.get());
    QVERIFY(item);
    auto* page = item->findChild<QQuickItem*>(QStringLiteral("guardedPage"));
    QVERIFY(page);
    window.resize(240, 180);
    item->setParentItem(window.contentItem());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    page->forceActiveFocus();
    QTRY_VERIFY(page->hasActiveFocus());
    QTest::keyPress(&window, Qt::Key_Escape);
    QCOMPARE(item->property("backCount").toInt(), 0);
    QTest::keyRelease(&window, Qt::Key_Escape);
    QCOMPARE(page->property("guardCount").toInt(), 1);
    QCOMPARE(item->property("backCount").toInt(), 0);
    item->setParentItem(nullptr);
}

void TestCustomWidgetHtml::homePlanModeFollowsRouteAndHostPreparation()
{
    const QString theme = readSource(QStringLiteral("/qml/Theme.qml"));
    const QString item = readSource(SrcPath::widgetItem(QStringLiteral("ShotPlanItem")));
    auto binding = [](const QString& source, const QString& name) {
        const QRegularExpression expression(QStringLiteral(
            R"((readonly property \w+ %1:\s*[^\n]+(?:\n {8,}[^\n]+)*))").arg(name));
        return expression.match(source).captured(1);
    };
    QString mode = binding(theme, QStringLiteral("currentOperationMode"));
    QString context = binding(item, QStringLiteral("_steamContext"));
    QString steamMode = binding(item, QStringLiteral("_steamMode"));
    QVERIFY(!mode.isEmpty());
    QVERIFY(!context.isEmpty());
    QVERIFY(!steamMode.isEmpty());
    mode.replace(QStringLiteral("PreparationService.state."), QStringLiteral("preparation."));
    context.replace(QStringLiteral("Theme."), QStringLiteral("root."));
    context.replace(QStringLiteral("MachineState.Phase.Steaming"), QStringLiteral("7"));
    context.replace(QStringLiteral("MachineState.phase"), QStringLiteral("root.phase"));
    steamMode.replace(QStringLiteral("compactSteamPlan.text"), QStringLiteral("root.planText"));
    steamMode.replace(QStringLiteral("fullSteamPlan.text"), QStringLiteral("root.planText"));

    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(QStringLiteral(R"QML(
        import QtQml
        QtObject {
            id: root
            property string currentPageObjectName: "idlePage"
            property bool showSteamPlan: true
            property int phase: 0
            property string planText: "Steam 221g of milk, using the Small pitcher for 60s"
            property QtObject preparation: QtObject {
                property bool available: true
                property string mode: "espresso"
            }
            %1
            %2
            %3
        }
    )QML").arg(mode, context, steamMode).toUtf8(), QUrl());
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto* preparation = object->property("preparation").value<QObject*>();
    QVERIFY(preparation);
    QCOMPARE(object->property("currentOperationMode").toString(), QStringLiteral("espresso"));
    QVERIFY(!object->property("_steamMode").toBool());
    QVERIFY(object->setProperty("currentPageObjectName", QStringLiteral("aboutPage")));
    QVERIFY(preparation->setProperty("mode", QStringLiteral("steam")));
    QCOMPARE(object->property("currentOperationMode").toString(), QString());
    QVERIFY(!object->property("_steamMode").toBool());
    // Returning from About uses the current host selection, without a page
    // callback publishing another copy of the operation mode.
    QVERIFY(object->setProperty("currentPageObjectName", QStringLiteral("idlePage")));
    QCOMPARE(object->property("currentOperationMode").toString(), QStringLiteral("steam"));
    QVERIFY(object->property("_steamMode").toBool());
    QVERIFY(preparation->setProperty("mode", QStringLiteral("hotwater")));
    QVERIFY(!object->property("_steamMode").toBool());
    QVERIFY(preparation->setProperty("mode", QStringLiteral("steam")));
    QVERIFY(preparation->setProperty("available", false));
    QCOMPARE(object->property("currentOperationMode").toString(), QString());
    QVERIFY(!object->property("_steamMode").toBool());
    QVERIFY(preparation->setProperty("available", true));
    QVERIFY(object->property("_steamMode").toBool());
    QVERIFY(object->setProperty("planText", QString()));
    QVERIFY(!object->property("_steamMode").toBool());
    QVERIFY(object->setProperty("planText", QStringLiteral("Steam plan")));
    QVERIFY(object->setProperty("showSteamPlan", false));
    QVERIFY(!object->property("_steamMode").toBool());
}

void TestCustomWidgetHtml::planAccessibilityReplacesItsFixedRoleNode()
{
    const QString source = readSource(SrcPath::widgetItem(QStringLiteral("ShotPlanItem")));
    QString semantics = liftFunction(source, QStringLiteral("Loader {\n        id: planSemantics"));
    QString label = liftFunction(source, QStringLiteral("readonly property string planAccessibleName:"));
    QString activate = liftFunction(source, QStringLiteral("function openBrewSettings()"));
    QVERIFY(!semantics.isEmpty() && !label.isEmpty() && !activate.isEmpty());
    // Replace only external service boundaries; load the shipping semantic
    // Loader, components, label binding and guarded activation unchanged.
    label.replace(QStringLiteral("TranslationManager."), QStringLiteral("translator."));
    activate.replace(QStringLiteral("AppShell."), QStringLiteral("appShell."));
    const QRegularExpression ignoredExpression(QStringLiteral(R"(\n    Accessible\.ignored:[^\n]+)"));
    const auto ignored = ignoredExpression.match(source);
    QVERIFY(ignored.hasMatch());

    QQuickWindow window;
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(QStringLiteral(R"QML(
        pragma ComponentBehavior: Bound
        import QtQuick
        Item {
            id: root
            width: 360; height: 70
            property bool _steamMode: false
            property int activationCount: 0
            property alias semanticItem: planSemantics.item
            QtObject {
                id: translator
                function translate(key, fallback) { return fallback }
            }
            QtObject {
                id: appShell
                signal brewSettingsRequested()
                onBrewSettingsRequested: ++root.activationCount
            }
            QtObject { id: compactShotPlan; property string text: "Espresso 54g, 18g dose" }
            QtObject { id: fullShotPlan; property string text: "" }
            QtObject { id: compactSteamPlan; property string text: "Steam 221g, Small pitcher, 60s" }
            QtObject { id: fullSteamPlan; property string text: "" }
            function updateShotText(text) { compactShotPlan.text = text }
            %1
            %2
            %3
            %4
        }
    )QML").arg(ignored.captured(), label, activate, semantics).toUtf8(), QUrl());
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto* root = qobject_cast<QQuickItem*>(object.get());
    QVERIFY(root);
    root->setParentItem(window.contentItem());
    window.resize(360, 70);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    const auto semanticItem = [&] {
        return qobject_cast<QQuickItem*>(root->property("semanticItem").value<QObject*>());
    };
    QPointer<QQuickItem> firstBrew = semanticItem();
    QVERIFY(firstBrew);
    auto* brewInterface = QAccessible::queryAccessibleInterface(firstBrew);
    QVERIFY(brewInterface);
    QCOMPARE(brewInterface->role(), QAccessible::Button);
    QCOMPARE(brewInterface->text(QAccessible::Name),
             QStringLiteral("Shot plan: Espresso 54g, 18g dose. Tap to edit"));
    QVERIFY(brewInterface->state().focusable);
    auto* brewActions = brewInterface->actionInterface();
    QVERIFY(brewActions);
    QVERIFY(brewActions->actionNames().contains(QAccessibleActionInterface::pressAction()));
    brewActions->doAction(QAccessibleActionInterface::pressAction());
    QCOMPARE(root->property("activationCount").toInt(), 1);
    firstBrew->forceActiveFocus();
    for (Qt::Key key : {Qt::Key_Return, Qt::Key_Enter, Qt::Key_Space})
        QTest::keyClick(&window, key);
    QCOMPARE(root->property("activationCount").toInt(), 4);
    QVERIFY(QMetaObject::invokeMethod(root, "updateShotText",
                                     Q_ARG(QVariant, QStringLiteral("Espresso 60g, 20g dose"))));
    QCOMPARE(semanticItem(), firstBrew.data());
    QCOMPARE(brewInterface->text(QAccessible::Name),
             QStringLiteral("Shot plan: Espresso 60g, 20g dose. Tap to edit"));

    QVERIFY(root->setProperty("_steamMode", true));
    QPointer<QQuickItem> steam = semanticItem();
    QVERIFY(steam);
    QVERIFY(steam != firstBrew);
    QTRY_VERIFY(firstBrew.isNull());
    auto* steamInterface = QAccessible::queryAccessibleInterface(steam);
    QVERIFY(steamInterface);
    QCOMPARE(steamInterface->role(), QAccessible::StaticText);
    QCOMPARE(steamInterface->text(QAccessible::Name),
             QStringLiteral("Steam plan: Steam 221g, Small pitcher, 60s"));
    QVERIFY(steamInterface->state().focusable);
    const auto* steamActions = steamInterface->actionInterface();
    QVERIFY(!steamActions || !steamActions->actionNames().contains(QAccessibleActionInterface::pressAction()));
    steam->forceActiveFocus();
    QTest::keyClick(&window, Qt::Key_Space);
    QVERIFY(QMetaObject::invokeMethod(root, "openBrewSettings"));
    QCOMPARE(root->property("activationCount").toInt(), 4);

    QVERIFY(root->setProperty("_steamMode", false));
    auto* nextBrew = semanticItem();
    QVERIFY(nextBrew && nextBrew != steam);
    QTRY_VERIFY(steam.isNull());
    auto* nextInterface = QAccessible::queryAccessibleInterface(nextBrew);
    QVERIFY(nextInterface);
    QCOMPARE(nextInterface->role(), QAccessible::Button);
    QCOMPARE(nextInterface->text(QAccessible::Name),
             QStringLiteral("Shot plan: Espresso 60g, 20g dose. Tap to edit"));
    nextInterface->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
    nextBrew->forceActiveFocus();
    QTest::keyClick(&window, Qt::Key_Space);
    QCOMPARE(root->property("activationCount").toInt(), 6);
}

void TestCustomWidgetHtml::compactWorkflowPickerSelectsTheHostMode_data()
{
    QTest::addColumn<QString>("widget");
    QTest::addColumn<QString>("mode");
    QTest::newRow("profiles") << QStringLiteral("EspressoItem") << QStringLiteral("espresso");
    QTest::newRow("recipes") << QStringLiteral("RecipesItem") << QStringLiteral("espresso");
    QTest::newRow("steam") << QStringLiteral("SteamItem") << QStringLiteral("steam");
    QTest::newRow("hot-water") << QStringLiteral("HotWaterItem") << QStringLiteral("hotwater");
    QTest::newRow("flush") << QStringLiteral("FlushItem") << QStringLiteral("flush");
}

void TestCustomWidgetHtml::compactWorkflowPickerSelectsTheHostMode()
{
    QFETCH(QString, widget);
    QFETCH(QString, mode);
    const QString actionsSource = readSource(QStringLiteral("/qml/components/HomePresetActions.qml"));
    QString selection = liftFunction(actionsSource, QStringLiteral("function selectPreparationMode("));
    QString results = liftFunction(actionsSource, QStringLiteral("property Connections preparationResults:"));
    QString toggle = liftFunction(readSource(SrcPath::widgetItem(widget)), QStringLiteral("function togglePresets()"));
    QVERIFY(!selection.isEmpty() && !results.isEmpty() && !toggle.isEmpty());
    selection.replace(QStringLiteral("PreparationService"), QStringLiteral("preparationService"));
    selection.replace(QStringLiteral("TranslationManager"), QStringLiteral("translator"));
    results.replace(QStringLiteral("PreparationService"), QStringLiteral("preparationService"));
    results.replace(QStringLiteral("TranslationManager"), QStringLiteral("translator"));
    toggle.replace(QStringLiteral("HomePresetActions"), QStringLiteral("actions"));
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(QStringLiteral(R"QML(
        import QtQuick
        QtObject {
            id: root
            property bool isCompact: true
            property var events: []
            property var inventoryRecipes: [({id: 1})]
            readonly property string eventText: events.join("|")
            property int failureCount: 0
            property alias preparationState: preparationService.state
            property alias pickerVisible: presetPopup.visible
            property QtObject service: QtObject {
                id: preparationService
                signal completed(string requestId, string action, bool success, string message)
                property QtObject state: QtObject {
                    property bool available: true
                    property bool admitted: true
                    property string mode: "%4" === "steam" ? "espresso" : "steam"
                    function selectMode(requested) {
                        root.events = root.events.concat(["select:" + requested])
                        return admitted
                    }
                    function isPreparationMode(action) {
                        return ["espresso", "steam", "hotwater", "flush"].indexOf(action) >= 0
                    }
                }
            }
            property QtObject translations: QtObject {
                id: translator
                function translate(key, fallback) { return fallback }
            }
            property QtObject homeActions: QtObject {
                id: actions
                signal failed(string message)
                onFailed: ++root.failureCount
                %1
                %2
            }
            property QtObject picker: QtObject {
                id: presetPopup
                property bool visible: false
                function open() { root.events = root.events.concat(["open"]); visible = true }
                function close() { root.events = root.events.concat(["close"]); visible = false }
            }
            function resetEvents() { events = [] }
            %3
        }
    )QML").arg(selection, results, toggle, mode).toUtf8(), QUrl());
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto* state = object->property("preparationState").value<QObject*>();
    auto* service = object->property("service").value<QObject*>();
    QVERIFY(state && service);
    const QString originalMode = state->property("mode").toString();
    QVERIFY(QMetaObject::invokeMethod(object.get(), "togglePresets"));
    QCOMPARE(object->property("eventText").toString(), QStringLiteral("select:%1|open").arg(mode));
    QVERIFY(object->property("pickerVisible").toBool());
    QCOMPARE(state->property("mode").toString(), originalMode); // Wait for the host echo.

    QVERIFY(QMetaObject::invokeMethod(object.get(), "resetEvents"));
    QVERIFY(state->setProperty("available", false));
    QVERIFY(QMetaObject::invokeMethod(object.get(), "togglePresets"));
    QCOMPARE(object->property("eventText").toString(), QStringLiteral("close"));
    QCOMPARE(object->property("failureCount").toInt(), 0);
    QVERIFY(QMetaObject::invokeMethod(object.get(), "resetEvents"));
    QVERIFY(QMetaObject::invokeMethod(object.get(), "togglePresets"));
    QCOMPARE(object->property("eventText").toString(), QString());
    QCOMPARE(object->property("failureCount").toInt(), 1);
    QVERIFY(!object->property("pickerVisible").toBool());

    QVERIFY(state->setProperty("available", true));
    QVERIFY(state->setProperty("admitted", false));
    QVERIFY(QMetaObject::invokeMethod(object.get(), "togglePresets"));
    QCOMPARE(object->property("eventText").toString(), QStringLiteral("select:%1").arg(mode));
    QVERIFY(!object->property("pickerVisible").toBool());
    QVERIFY(QMetaObject::invokeMethod(service, "completed",
        Q_ARG(QString, QStringLiteral("mode-request")), Q_ARG(QString, mode),
        Q_ARG(bool, false), Q_ARG(QString, QStringLiteral("Mode rejected"))));
    QCOMPARE(object->property("failureCount").toInt(), 2);

    QVERIFY(state->setProperty("mode", mode));
    QVERIFY(state->setProperty("admitted", true));
    QVERIFY(QMetaObject::invokeMethod(object.get(), "resetEvents"));
    QVERIFY(QMetaObject::invokeMethod(object.get(), "togglePresets"));
    QCOMPARE(object->property("eventText").toString(), QStringLiteral("select:%1|open").arg(mode));
    QVERIFY(object->property("pickerVisible").toBool());
    QVERIFY(QMetaObject::invokeMethod(object.get(), "togglePresets"));
    QVERIFY(!object->property("pickerVisible").toBool());
}

void TestCustomWidgetHtml::reviewBeanPickerFollowsTheShotAndAcknowledgedSelection()
{
    const QString picker = readSource(QStringLiteral("/qml/components/ChangeBeansDialog.qml"));
    const QString review = readSource(QStringLiteral("/qml/pages/PostShotReviewPage.qml"));
    const auto defaultSelection = QRegularExpression(QStringLiteral(
        "property var selectedBagId:([^\\n]+)")).match(picker);
    const auto shotSelection = QRegularExpression(QStringLiteral(
        "selectedBagId:([^\\n]+)")).match(review.mid(review.indexOf(QStringLiteral("id: changeBeansLoader"))));
    const auto marker = QRegularExpression(QStringLiteral(
        "readonly property bool isActiveBag:([^\\n]+\\n\\s*&&[^\\n]+)")).match(picker);
    QVERIFY(defaultSelection.hasMatch());
    QVERIFY(shotSelection.hasMatch());
    QVERIFY(marker.hasMatch());
    QString defaultBinding = defaultSelection.captured(1);
    defaultBinding.replace(QStringLiteral("BagService"), QStringLiteral("bagService"));
    QString markerBinding = marker.captured(1);
    markerBinding.replace(QStringLiteral("resultRow."), QStringLiteral("root."));
    const QString selected = liftHandler(review, QStringLiteral("id: changeBeansLoader"),
        QStringLiteral("onBagSelected:"), QStringLiteral("selected"));
    const QString clone = liftFunction(review, QStringLiteral("function clonePersistedShot("));
    QVERIFY(!selected.isEmpty());
    QVERIFY(!clone.isEmpty());

    // Run the shipping default/shot override/delegate bindings and completion
    // callback together, with only the surrounding page state supplied here.
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(QStringLiteral(R"QML(
        import QtQml
        QtObject {
            id: root
            property QtObject bagService: QtObject {
                id: bagService
                property var activeBagId: 41
            }
            property var ordinarySelectedBagId: %1
            property var selectedBagId: %2
            property var modelData: ({tier: 0})
            property int rowBagId: 42
            readonly property bool isActiveBag: %3
            property var originalShot: null
            function rememberShot() { originalShot = postShotReviewPage.editShotData }
            property QtObject reviewPage: QtObject {
                id: postShotReviewPage
                property var editShotData: ({bagId:42, notes:"Persisted notes", equipmentId:73, recoveryToken:"keep"})
                property string editBeanBrand: "Old roaster"
                property string editBeanType: "Old coffee"
                property string editRoastDate: ""
                property string editRoastLevel: ""
                property string editBeanBaseJson: ""
                property string editNotes: "Local recovery draft"
                property var _committedState: ({})
                property bool pendingVisualizerUpdate: false
                property bool pendingDecentUpdate: false
                %4
                function captureEditState() { return {beanBrand:editBeanBrand, notes:editNotes} }
            }
            %5
        }
    )QML").arg(defaultBinding, shotSelection.captured(1), markerBinding, clone, selected).toUtf8(), QUrl());
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QObject *service = object->property("bagService").value<QObject *>();
    QObject *page = object->property("reviewPage").value<QObject *>();
    QVERIFY(service);
    QVERIFY(page);
    QCOMPARE(object->property("ordinarySelectedBagId").toInt(), 41);
    QCOMPARE(object->property("selectedBagId").toInt(), 42);
    QVERIFY(object->property("isActiveBag").toBool());
    QVERIFY(service->setProperty("activeBagId", 99));
    QCOMPARE(object->property("ordinarySelectedBagId").toInt(), 99);
    QCOMPARE(object->property("selectedBagId").toInt(), 42);
    QVERIFY(object->property("isActiveBag").toBool());
    QVERIFY(object->setProperty("modelData", QVariantMap{{QStringLiteral("tier"), 1}}));
    QVERIFY(!object->property("isActiveBag").toBool());
    QVERIFY(object->setProperty("modelData", QVariantMap{{QStringLiteral("tier"), 0}}));
    QVERIFY(object->setProperty("rowBagId", 99));
    QVERIFY(!object->property("isActiveBag").toBool());
    QVERIFY(QMetaObject::invokeMethod(object.get(), "rememberShot"));
    const QVariantMap bag{{QStringLiteral("roasterName"), QStringLiteral("New roaster")},
        {QStringLiteral("coffeeName"), QStringLiteral("New coffee")},
        {QStringLiteral("roastDate"), QStringLiteral("2026-10-01")},
        {QStringLiteral("roastLevel"), QStringLiteral("Medium")},
        {QStringLiteral("beanBaseData"), QStringLiteral("{\"canonical\":true}")}};
    QVERIFY(QMetaObject::invokeMethod(object.get(), "selected",
        Q_ARG(QVariant, QVariant(44)), Q_ARG(QVariant, QVariant(bag))));
    QCOMPARE(object->property("selectedBagId").toInt(), 44);
    QCOMPARE(service->property("activeBagId").toInt(), 99);
    QVERIFY(object->setProperty("rowBagId", 44));
    QVERIFY(object->property("isActiveBag").toBool());
    const QVariantMap snapshot = engine.newQObject(page).property(QStringLiteral("editShotData")).toVariant().toMap();
    QCOMPARE(snapshot.value(QStringLiteral("bagId")).toInt(), 44);
    QCOMPARE(snapshot.value(QStringLiteral("beanBrand")).toString(), QStringLiteral("New roaster"));
    QCOMPARE(snapshot.value(QStringLiteral("beanType")).toString(), QStringLiteral("New coffee"));
    QCOMPARE(snapshot.value(QStringLiteral("beanBaseJson")).toString(), bag.value(QStringLiteral("beanBaseData")).toString());
    QCOMPARE(snapshot.value(QStringLiteral("equipmentId")).toInt(), 73);
    QCOMPARE(snapshot.value(QStringLiteral("recoveryToken")).toString(), QStringLiteral("keep"));
    QCOMPARE(snapshot.value(QStringLiteral("notes")).toString(), QStringLiteral("Persisted notes"));
    QCOMPARE(engine.newQObject(object.get()).property(QStringLiteral("originalShot")).toVariant()
        .toMap().value(QStringLiteral("bagId")).toInt(), 42);
    QCOMPARE(page->property("editNotes").toString(), QStringLiteral("Local recovery draft"));
    QVERIFY(page->property("pendingVisualizerUpdate").toBool());
    QVERIFY(page->property("pendingDecentUpdate").toBool());
    QVERIFY(page->setProperty("editShotData", QVariantMap{}));
    QCOMPARE(object->property("selectedBagId").toInt(), 0);
    QVERIFY(!object->property("isActiveBag").toBool());
}

void TestCustomWidgetHtml::comparisonAccessiblePressInspectsGraphLocalCenter()
{
    const QString source = readSource(QStringLiteral("/qml/pages/ShotComparisonPage.qml"));
    const QString press = liftHandler(source, QStringLiteral("id: graphCard"),
        QStringLiteral("Accessible.onPressAction:"), QStringLiteral("press"));
    QVERIFY(!press.isEmpty());
    QJSEngine engine;
    const QJSValue result = engine.evaluate(QStringLiteral(R"JS(
        var calls = [];
        var comparisonGraph = {width:713, height:271,
            inspectAtPosition:function(x,y) { calls.push([x,y]); }};
        var graphMouseArea = {clicked:function() { throw Error("No click handler inspects the graph"); }};
        %1
        press();
        calls
    )JS").arg(press));
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QCOMPARE(result.property(QStringLiteral("length")).toInt(), 1);
    QCOMPARE(result.property(0).property(0).toNumber(), 356.5);
    QCOMPARE(result.property(0).property(1).toNumber(), 135.5);
}

void TestCustomWidgetHtml::screensaverDimmingFollowsRetainedModeAndReadiness()
{
    const QString source = readSource(QStringLiteral("/qml/pages/ScreensaverPage.qml"));
    const qsizetype modesStart = source.indexOf(QStringLiteral("property string screensaverType:"));
    const qsizetype modesEnd = source.indexOf(QStringLiteral("// Anti-burn-in:"), modesStart);
    QVERIFY(modesStart >= 0 && modesEnd > modesStart);
    QString modes = source.mid(modesStart, modesEnd - modesStart);
    const QStringList signatures{QStringLiteral("function refreshDimming("),
        QStringLiteral("function applyDim("), QStringLiteral("function startDimming("),
        QStringLiteral("function handleScreensaverServiceReadiness("),
        QStringLiteral("function onDimPercentChanged("), QStringLiteral("function onDimDelayMinutesChanged(")};
    QStringList functions;
    for (const QString& signature : signatures) {
        const QString function = liftFunction(source, signature);
        QVERIFY2(!function.isEmpty(), qPrintable(signature));
        functions.append(function);
    }
    const QString complete = liftHandler(source, QStringLiteral("property bool videoDecoderBroken:"),
        QStringLiteral("Component.onCompleted:"), QStringLiteral("complete"));
    const auto modeChanged = QRegularExpression(QStringLiteral(
        "onIsDisabledModeChanged:([^\\n]+)")).match(source);
    QVERIFY(!complete.isEmpty());
    QVERIFY(modeChanged.hasMatch());
    QString handlers = functions.mid(0, 4).join(QLatin1Char('\n')) + QLatin1Char('\n') + complete;
    QString preferenceHandlers = functions.mid(4).join(QLatin1Char('\n'));
    for (QString *text : {&modes, &handlers, &preferenceHandlers}) {
        text->replace(QStringLiteral("ScreensaverService."), QStringLiteral("service."));
        text->replace(QStringLiteral("WebDebugLogger."), QStringLiteral("logger."));
    }
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(QStringLiteral(R"QML(
        import QtQuick
        QtObject {
            id: screensaverPage
            property bool visible: true
            property bool componentReady: false
            property bool screensaverServiceWasReady: true
            property bool appSuspended: false
            property int mediaRequests: 0
            property int videoStops: 0
            property string pendingMediaRequestId: ""
            property string currentMediaTicket: ""
            property string currentImageSource: ""
            property var currentMedia: ({})
            property bool mediaPlaying: false
            property bool isCurrentItemImage: false
            property QtObject imageDisplay1: QtObject { property string source: "" }
            property QtObject imageDisplay2: QtObject { property string source: "" }
            property QtObject service: QtObject {
                property bool ready: true
                property int dimCalls: 0
                property int lastDim: -1
                function setScreenDimming(value) { ++dimCalls; lastDim = value }
                property QtObject state: QtObject {
                    property string screensaverType: "disabled"
                    property int dimPercent: 35
                    property int dimDelayMinutes: 2
                }
            }
            property QtObject logger: QtObject { function debug(category, componentName, message) {} }
            property QtObject overlay: QtObject { id: dimOverlay; property real opacity: 0 }
            property QtObject behavior: QtObject { id: dimBehavior; property bool enabled: true }
            property QtObject gradient: QtObject { id: gradientAnimation; property bool running: true }
            property QtObject timer: QtObject {
                id: dimTimer
                property bool running: false
                property int restarts: 0
                function restart() { running = true; ++restarts }
                function stop() { running = false }
            }
            property Connections preferences: Connections { target: screensaverPage.service.state; %4 }
            function stopVideoPlayer() { ++videoStops }
            function playNextMedia() { ++mediaRequests }
            function elapseDimDelay() { dimTimer.running = false; applyDim() }
            %1
            %2
            onIsDisabledModeChanged: %3
            Component.onCompleted: complete()
        }
    )QML").arg(modes, handlers, modeChanged.captured(1), preferenceHandlers).toUtf8(), QUrl());
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> page(component.create());
    QVERIFY2(page, qPrintable(component.errorString()));
    QObject *service = page->property("service").value<QObject *>();
    QVERIFY(service);
    QObject *state = service->property("state").value<QObject *>();
    QObject *overlay = page->property("overlay").value<QObject *>();
    QObject *timer = page->property("timer").value<QObject *>();
    QVERIFY(service && state && overlay && timer);
    QCOMPARE(overlay->property("opacity").toDouble(), 1.0);
    QCOMPARE(service->property("lastDim").toInt(), 100);
    QVERIFY(!timer->property("running").toBool());
    QVERIFY(state->setProperty("screensaverType", QStringLiteral("flipclock")));
    QCOMPARE(overlay->property("opacity").toDouble(), 0.0);
    QCOMPARE(service->property("lastDim").toInt(), 0);
    QVERIFY(timer->property("running").toBool());
    const int restarts = timer->property("restarts").toInt();
    QVERIFY(state->setProperty("screensaverType", QStringLiteral("pipes")));
    QCOMPARE(timer->property("restarts").toInt(), restarts);
    QVERIFY(QMetaObject::invokeMethod(page.get(), "elapseDimDelay"));
    QCOMPARE(overlay->property("opacity").toDouble(), 0.35);
    QVERIFY(state->setProperty("screensaverType", QStringLiteral("attractor")));
    QCOMPARE(overlay->property("opacity").toDouble(), 0.35);
    QCOMPARE(timer->property("restarts").toInt(), restarts);
    QVERIFY(state->setProperty("dimPercent", 45));
    QCOMPARE(overlay->property("opacity").toDouble(), 0.45);
    QCOMPARE(service->property("lastDim").toInt(), 45);
    QVERIFY(state->setProperty("dimDelayMinutes", 3));
    QCOMPARE(timer->property("restarts").toInt(), restarts);
    QVERIFY(state->setProperty("screensaverType", QStringLiteral("disabled")));
    QCOMPARE(overlay->property("opacity").toDouble(), 1.0);
    QVERIFY(state->setProperty("dimPercent", 0));
    QCOMPARE(overlay->property("opacity").toDouble(), 1.0);
    const int calls = service->property("dimCalls").toInt();
    QVERIFY(service->setProperty("ready", false));
    QVERIFY(QMetaObject::invokeMethod(page.get(), "handleScreensaverServiceReadiness"));
    QCOMPARE(overlay->property("opacity").toDouble(), 0.0);
    QCOMPARE(service->property("dimCalls").toInt(), calls);
    QCOMPARE(page->property("videoStops").toInt(), 1);
    QVERIFY(service->setProperty("ready", true));
    QVERIFY(QMetaObject::invokeMethod(page.get(), "handleScreensaverServiceReadiness"));
    QCOMPARE(overlay->property("opacity").toDouble(), 1.0);
    QCOMPARE(service->property("lastDim").toInt(), 100);
    QVERIFY(state->setProperty("screensaverType", QStringLiteral("flipclock")));
    QCOMPARE(overlay->property("opacity").toDouble(), 0.0);
    QCOMPARE(service->property("lastDim").toInt(), 0);
    QVERIFY(!timer->property("running").toBool());
    QVERIFY(state->setProperty("dimDelayMinutes", 0));
    QVERIFY(state->setProperty("dimPercent", 25));
    QCOMPARE(overlay->property("opacity").toDouble(), 0.25);
    QCOMPARE(service->property("lastDim").toInt(), 25);
    QVERIFY(!timer->property("running").toBool());
}

void TestCustomWidgetHtml::browserPixelWheelUsesQtGestureUnderPage()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"QML(
        import QtQuick
        import QtQuick.Controls
        Page {
            width: 240; height: 180
            Flickable {
                objectName: "outer"
                anchors.fill: parent; contentWidth: 240; contentHeight: 1200
                boundsBehavior: Flickable.StopAtBounds
                Flickable {
                    objectName: "inner"
                    x: 20; y: 20; width: 140; height: 100
                    contentWidth: 140; contentHeight: 300
                    boundsBehavior: Flickable.StopAtBounds
                    Rectangle { width: 140; height: 300 }
                }
            }
        }
    )QML", QUrl());
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto* page = qobject_cast<QQuickItem*>(object.get());
    auto* outer = page->findChild<QQuickItem*>(QStringLiteral("outer"));
    auto* inner = page->findChild<QQuickItem*>(QStringLiteral("inner"));
    QVERIFY(outer && inner);
    // A Page's own wheelEnabled=false must not disable its scrolling children.
    QVERIFY(!page->property("wheelEnabled").toBool());
    QQuickWindow window;
    QQmlEngine::setContextForObject(&window, engine.rootContext());
    window.resize(240, 180);
    page->setParentItem(window.contentItem());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    BrowserDisplay filter;
    QCoreApplication::instance()->installEventFilter(&filter);
    ulong timestamp = 1;
    const auto send = [&](const QPoint& position, int delta) {
        QWheelEvent event(position, window.mapToGlobal(position), QPoint(0, delta),
                          QPoint(0, delta), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        event.setTimestamp(timestamp++);
        QCoreApplication::sendEvent(&window, &event);
    };
    const auto scroll = [&](const QPoint& position, int pixels) {
        for (int i = 0; i < qAbs(pixels); ++i)
            send(position, pixels > 0 ? -1 : 1);
    };
    scroll({40, 40}, 100);
    QCOMPARE(inner->property("contentY").toReal(), 100.0);
    QVERIFY(inner->property("moving").toBool());
    QCOMPARE(outer->property("contentY").toReal(), 0.0);
    scroll({40, 40}, 100);
    scroll({40, 40}, 40);
    QCOMPARE(inner->property("contentY").toReal(), 200.0);
    QCOMPARE(outer->property("contentY").toReal(), 40.0);
    scroll({40, 40}, -30);
    QCOMPARE(inner->property("contentY").toReal(), 170.0);
    QCOMPARE(outer->property("contentY").toReal(), 40.0);
    QTRY_VERIFY(!inner->property("moving").toBool());
    outer->setProperty("contentY", 0);
    inner->setProperty("contentY", 0);
    inner->setProperty("interactive", false);
    scroll({40, 40}, 20);
    QCOMPARE(inner->property("contentY").toReal(), 0.0);
    QCOMPARE(outer->property("contentY").toReal(), 20.0);
    QTRY_VERIFY(!outer->property("moving").toBool());
    outer->setProperty("contentY", 0);
    send({210, 120}, -80);
    QCOMPARE(outer->property("contentY").toReal(), 80.0);
    page->setParentItem(nullptr);
}

void TestCustomWidgetHtml::browserVideoHandleRecoveryReloadsOnce()
{
    const QString source = readSource(SrcPath::kBrowserEntry);
    const QString report = liftFunction(source, QStringLiteral("function reportFailure("));
    const QString allow = liftFunction(source, QStringLiteral("function allowVideoRecovery("));
    QVERIFY(!report.isEmpty() && !allow.isEmpty());
    QJSEngine engine;
    const auto setup = engine.evaluate(QStringLiteral(R"JS(
        var videoRecoveryKey = 'recovery', videoRecoveryRequested = false;
        var saved = null, reloads = 0;
        var status = {dataset: {}}, retry = {}, screen = {setAttribute: function() {}};
        var sessionStorage = {getItem: function() { return saved; },
                              setItem: function(k, v) { saved = v; },
                              removeItem: function() { saved = null; }};
        var location = {reload: function() { reloads++; }};
    )JS") + report + QLatin1Char('\n') + allow);
    QVERIFY2(!setup.isError(), qPrintable(setup.toString()));
    auto run = [&engine](const QString &script) {
        const auto result = engine.evaluate(script);
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
    };
    const QString failure = QStringLiteral("reportFailure('Uncaught BindingError: Cannot use deleted val. handle = 0');");
    run(failure + failure);
    QCOMPARE(engine.globalObject().property(QStringLiteral("reloads")).toInt(), 1);
    run(QStringLiteral("videoRecoveryRequested = false;") + failure);
    QCOMPARE(engine.globalObject().property(QStringLiteral("reloads")).toInt(), 1);
    run(QStringLiteral("allowVideoRecovery(); reportFailure('unrelated error');"));
    QCOMPARE(engine.globalObject().property(QStringLiteral("reloads")).toInt(), 1);
    run(failure);
    QCOMPARE(engine.globalObject().property(QStringLiteral("reloads")).toInt(), 2);
}

void TestCustomWidgetHtml::browserFullscreenUsesWindowScreen()
{
    const QString source = readSource(SrcPath::kBrowserDisplay);
    const auto entryScreen = QRegularExpression(QStringLiteral("const screen =[^;]+;"))
                                 .match(readSource(SrcPath::kBrowserEntry));
    QVERIFY(entryScreen.hasMatch());
    QString script = QStringLiteral(R"JS(
        var locks = 0, unlocks = 0, changes = 0, failures = 0, lockedTo = '';
        var window = {screen: {orientation: {
            lock: function(value) { locks++; lockedTo = value; return Promise.resolve(); },
            unlock: function() { unlocks++; }
        }}};
        var listeners = {};
        var Module = {
            _decenzaFullscreenChanged: function() { changes++; },
            _decenzaFullscreenFailed: function() { failures++; }
        };
        var document = {
            fullscreenElement: null,
            getElementById: function() { return {}; },
            addEventListener: function(name, listener) { listeners[name] = listener; },
            documentElement: {requestFullscreen: function() {
                document.fullscreenElement = this;
                listeners.fullscreenchange();
                return Promise.resolve();
            }},
            exitFullscreen: function() {
                document.fullscreenElement = null;
                listeners.fullscreenchange();
                return Promise.resolve();
            }
        };
    )JS") + entryScreen.captured() + QLatin1Char('\n');
    for (const QString& name : {QStringLiteral("installFullscreenListener"),
                               QStringLiteral("toggleBrowserFullscreen")}) {
        QString function = liftFunction(source, QStringLiteral("EM_JS(void, %1,").arg(name));
        QVERIFY(!function.isEmpty());
        function = function.mid(function.indexOf(QLatin1Char('{')));
        script += QStringLiteral("function %1() %2\n").arg(name, function);
    }
    QJSEngine engine;
    const auto setup = engine.evaluate(script + QStringLiteral("installFullscreenListener();"));
    QVERIFY2(!setup.isError(), qPrintable(setup.toString()));
    QVERIFY(!engine.evaluate(QStringLiteral("toggleBrowserFullscreen()")).isError());
    QTRY_COMPARE(engine.globalObject().property(QStringLiteral("locks")).toInt(), 1);
    QCOMPARE(engine.globalObject().property(QStringLiteral("lockedTo")).toString(), QStringLiteral("landscape"));
    QVERIFY(!engine.evaluate(QStringLiteral("toggleBrowserFullscreen()")).isError());
    QCOMPARE(engine.globalObject().property(QStringLiteral("unlocks")).toInt(), 1);
    QCOMPARE(engine.globalObject().property(QStringLiteral("changes")).toInt(), 2);
    QCOMPARE(engine.globalObject().property(QStringLiteral("failures")).toInt(), 0);
}

QTEST_MAIN(TestCustomWidgetHtml)
#include "tst_customwidgethtml.moc"
