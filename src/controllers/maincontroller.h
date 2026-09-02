#pragma once

#include <QObject>
#include <QVariantList>
#include <QMap>
#include <QTimer>
#include "profilemanager.h"
#include "steamheaterpolicy.h"
#include "recipeselectionmodel.h"
#include "core/logcollapse.h"
#include "core/yieldspec.h"
#include "../profile/profile.h"
#include "../network/visualizeruploader.h"
#include "../network/visualizerimporter.h"
#include "../network/beanbaseclient.h"
#include "../ai/aimanager.h"
#include "../ai/livesteamcoach.h"
#include "../models/shotdatamodel.h"
#include "../models/steamdatamodel.h"
#include "../machine/steamhealthtracker.h"
#include "../history/shothistorystorage.h"
#include "../history/coffeebagstorage.h"
#include "../history/equipmentstorage.h"
#include "../history/recipestorage.h"
#include "../history/unifiedbeansearchmodel.h"
#include "../history/shotimporter.h"
#include "../profile/profileconverter.h"
#include "../profile/profileimporter.h"
#include "../models/shotcomparisonmodel.h"
#include "../network/shotserver.h"
#include "../network/shotreporter.h"
// This include propagates the third-party <MQTTAsync.h> to all 16 includers of
// this header, most of which use MqttClient only as an opaque pointer. It was
// tried as a forward declaration and REVERTED — do not retry without reading
// this. `Q_PROPERTY(MqttClient* mqttClient ...)` below needs the complete type:
// Qt's metatype system rejects an incomplete pointee outright ("Pointer Meta
// Types must either point to fully-defined types or be declared with
// Q_DECLARE_OPAQUE_POINTER"). And the opaque-pointer escape hatch is worse than
// the include, because SettingsHomeAutomationTab.qml reads through this property
// at 8 sites (MainController.mqttClient.connected / .status /
// .connectToBroker()); an opaque pointer carries no introspectable members, so
// every one of those would degrade at RUNTIME rather than fail to compile.
// The cost is real but bounded: touching mqttclient.h rebuilds 26 objects.
#include "../network/mqttclient.h"
#include "../core/updatechecker.h"
#include "../core/hdsfirmwareupdatecontroller.h"
#include "../core/firmwareassetcache.h"
#include "firmwareupdater.h"
#include "../core/datamigrationclient.h"
#include "../core/databasebackupmanager.h"


class QNetworkAccessManager;
class Settings;
class DE1Device;
class MachineState;
class BLEManager;
class FlowScale;
class ProfileStorage;
class ShotDebugLogger;
class LocationProvider;
class ShotTimingController;
class TranslationManager;
class ScaleDeviceProxy;
struct ShotSample;

class MainController : public QObject {
    Q_OBJECT

    // Registered to QML by MainControllerForeign (core/contextsingletons_qml.h), not by macros
    // on this class. That registration is what puts the type in the module's generated
    // Decenza.qmltypes — the only place qmllint, qmlcachegen and the language server learn about
    // C++ types. It replaced a setContextProperty, which exists only at runtime and is therefore
    // invisible to all three: it was the largest source of unresolvable names REMAINING once
    // TranslationManager and Settings had migrated (design.md records those at 3,459 and 1,335
    // against this one's 879 — and the 879 anchor was itself stale; the measured reduction was
    // 916).
    //
    // Registering the type is necessary but NOT sufficient: main.cpp must also call
    // qml_register_types_Decenza() explicitly, or no declarative type in this module reaches the
    // runtime registry at all. See the comment at that call site.
    //
    // This class used to carry QML_ELEMENT/QML_SINGLETON directly, and a long note here argued
    // that direct macros were fine because settings_qml.h's stated reason for the wrapper (a
    // <QtQml/...> include being "a build break" in saw_parity) was measurably false. That
    // measurement still stands, but it was never the reason that mattered: Qt tests
    // is_default_constructible BEFORE it looks for a create() factory (qqmlprivate.h:161-164), so
    // a directly-registered singleton whose constructor gains a defaulted parameter silently gets
    // `new T` and its factory is never called. AccessibilityManager shipped exactly that. The
    // wrapper shape cannot reach that branch at all — see docs/CLAUDE_MD/QML_GOTCHAS.md.
    //
    // What IS true and worth checking before copying either shape: a class registered directly
    // needs a complete type for every pointer parameter of its Q_INVOKABLE/signal/slot signatures,
    // because moc must build a metatype for each. That is what forced real #includes into
    // visualizeruploader.h, and what defers ShotDataModel/SteamDataModel (their registerFastSeries
    // would drag <QQuickItem> into every consumer of this header).

    // Non-profile QML properties (profile properties are on ProfileManager)
    //
    // FINAL on the pointer-typed sub-object accessors below is load-bearing, not decoration:
    // without it qmlcachegen will not compile a chained lookup like `MainController.aiManager.x`,
    // because a subclass could shadow the member. The read itself still works — it degrades to
    // `var` with a qmlCompiler log line (qqmljsshadowcheck.cpp:203-205) — and it is the NEXT
    // lookup off that base that fails with "Cannot use shadowable base type for further lookups"
    // (:248). Marking the property final is what makes the member NotShadowable here
    // (:197-198); that file has other NotShadowable paths, but none that apply to a plain
    // singleton's property.
    //
    // The scalar leaf properties further down (currentFrameName, filteredGoalPressure,
    // selectedRecipeId, …) are left non-final because it buys nothing THERE, not because they
    // are exempt from the check. They are not: every property read goes through
    // checkShadowing() and its isFinal() test, and a non-final one degrades to `var` just the
    // same. What they never reach is checkBaseType()'s hard error at :248, because no QML
    // chains a further lookup off them today. Add a binding that does — say
    // `MainController.currentFrameName.length` — and that property needs FINAL too.
    //
    // So the rule is about the VALUE being used as a lookup base, not about pointer vs scalar.
    // settings.h applies FINAL uniformly to every property rather than tracking which ones are
    // currently chained; that is inert where it is not needed and immune to this trap.
    // Nothing subclasses MainController today.
    Q_PROPERTY(VisualizerUploader* visualizer READ visualizer CONSTANT FINAL)
    Q_PROPERTY(VisualizerImporter* visualizerImporter READ visualizerImporter CONSTANT FINAL)
    Q_PROPERTY(BeanBaseClient* beanbase READ beanbase CONSTANT FINAL)
    Q_PROPERTY(AIManager* aiManager READ aiManager CONSTANT FINAL)
    Q_PROPERTY(LiveSteamCoach* liveSteamCoach READ liveSteamCoach CONSTANT FINAL)
    Q_PROPERTY(ShotDataModel* shotDataModel READ shotDataModel CONSTANT FINAL)
    Q_PROPERTY(SteamDataModel* steamDataModel READ steamDataModel CONSTANT FINAL)
    Q_PROPERTY(SteamHealthTracker* steamHealthTracker READ steamHealthTracker CONSTANT FINAL)
    Q_PROPERTY(QString currentFrameName READ currentFrameName NOTIFY frameChanged)
    Q_PROPERTY(double filteredGoalPressure READ filteredGoalPressure NOTIFY goalsChanged)
    Q_PROPERTY(double filteredGoalFlow READ filteredGoalFlow NOTIFY goalsChanged)
    Q_PROPERTY(ShotHistoryStorage* shotHistory READ shotHistory CONSTANT FINAL)
    Q_PROPERTY(CoffeeBagStorage* bagStorage READ bagStorage CONSTANT FINAL)
    Q_PROPERTY(EquipmentStorage* equipmentStorage READ equipmentStorage CONSTANT FINAL)
    Q_PROPERTY(RecipeStorage* recipeStorage READ recipeStorage CONSTANT FINAL)
    // The active recipe's full row (empty map = none). Refreshed on
    // activation, on external edits to the active row, and cleared on
    // deactivation. QML reads name/steam fields from here; the id itself
    // lives in Settings.dye.activeRecipeId.
    Q_PROPERTY(QVariantMap activeRecipe READ activeRecipe NOTIFY activeRecipeChanged)

    // Recipe-aware brew baseline (recipe-baseline-not-override, #1485). When a
    // recipe is active, ITS own yield/temp are the baseline, not overrides of the
    // profile — so a recipe's designed values must not read as overrides on any
    // live readout. These fold the recipe-vs-profile choice into one source of
    // truth a read-only widget can ask instead of re-deriving it inline and
    // drifting — currently the temperature readout (TemperatureItem) and custom
    // brew widgets (CustomItem). (Brew Settings and the Shot Plan have their own
    // richer, per-instance baselines — a seeded/mutable dialog and injected
    // recipeBaseline* props — so they intentionally do not read these.)
    // The baselines fall back to the profile when no recipe is active (or the
    // recipe pins no value for that field); the *IsRealOverride flags are true
    // only for a per-brew deviation FROM that baseline. All four re-evaluate on
    // brewBaselineChanged (recipe activation/edit, brew-override edits, profile
    // switch, or a target-weight sync).
    Q_PROPERTY(double activeBaselineTemperatureC READ activeBaselineTemperatureC NOTIFY brewBaselineChanged)
    Q_PROPERTY(double activeBaselineYieldG READ activeBaselineYieldG NOTIFY brewBaselineChanged)
    // The yield baseline as a SPEC (add-yield-ratio-anchor): the active
    // store's own {value, mode} resolved through the ladder — the recipe's
    // spec when a recipe is active and designs a yield, else the bag's, else
    // the profile's target_weight (mode "absolute"). activeBaselineYieldG
    // above is this spec resolved to grams through the current dose.
    Q_PROPERTY(double activeBaselineYieldValue READ activeBaselineYieldValue NOTIFY brewBaselineChanged)
    Q_PROPERTY(QString activeBaselineYieldMode READ activeBaselineYieldMode NOTIFY brewBaselineChanged)
    Q_PROPERTY(bool temperatureIsRealOverride READ temperatureIsRealOverride NOTIFY brewBaselineChanged)
    Q_PROPERTY(bool yieldIsRealOverride READ yieldIsRealOverride NOTIFY brewBaselineChanged)
    // The recipe the user has SELECTED in a pill row — set synchronously the
    // instant activateRecipe() is called, so the two-tap "select then start"
    // gesture (tap once to select, tap the selected pill again to pull the
    // shot) fires on the very next tap without waiting for the async activation
    // (background DB read + BLE profile upload) to echo back through
    // Settings.dye.activeRecipeId. Leads activeRecipeId during activation, then
    // tracks it: a successful activation confirms it, an external deactivation
    // or a failed activation rolls it back. This is the recipe analogue of the
    // profile pills' synchronous Settings.app.selectedFavoriteProfile, and is
    // the single source of truth shared by the regular (IdlePage) and compact
    // (RecipesItem) pill rows so both behave identically. -1 = none. Under
    // rapidly interleaved activations it tracks the last-COMPLETED activation,
    // not necessarily the last tap (same self-healing race as activeRecipe); it
    // always re-converges to activeRecipeId, so the highlight never ends
    // disagreeing with the active recipe.
    Q_PROPERTY(qint64 selectedRecipeId READ selectedRecipeId NOTIFY selectedRecipeIdChanged)
    Q_PROPERTY(UnifiedBeanSearchModel* beanSearch READ beanSearch CONSTANT FINAL)
    Q_PROPERTY(ShotImporter* shotImporter READ shotImporter CONSTANT FINAL)
    Q_PROPERTY(ProfileConverter* profileConverter READ profileConverter CONSTANT FINAL)
    Q_PROPERTY(ProfileImporter* profileImporter READ profileImporter CONSTANT FINAL)
    Q_PROPERTY(ShotComparisonModel* shotComparison READ shotComparison CONSTANT FINAL)
    Q_PROPERTY(ShotServer* shotServer READ shotServer CONSTANT FINAL)
    Q_PROPERTY(MqttClient* mqttClient READ mqttClient CONSTANT FINAL)
    Q_PROPERTY(UpdateChecker* updateChecker READ updateChecker CONSTANT FINAL)
    Q_PROPERTY(HdsFirmwareUpdateController* hdsFirmwareUpdate READ hdsFirmwareUpdate CONSTANT FINAL)
    Q_PROPERTY(FirmwareUpdater* firmwareUpdater READ firmwareUpdater CONSTANT FINAL)
    Q_PROPERTY(ShotReporter* shotReporter READ shotReporter CONSTANT FINAL)
    Q_PROPERTY(DataMigrationClient* dataMigration READ dataMigration CONSTANT FINAL)
    Q_PROPERTY(DatabaseBackupManager* backupManager READ backupManager CONSTANT FINAL)
    Q_PROPERTY(qint64 lastSavedShotId READ lastSavedShotId NOTIFY lastSavedShotIdChanged)
    Q_PROPERTY(bool sawSettling READ isSawSettling NOTIFY sawSettlingChanged)

    // Whether the steam heater is currently commanded ON, resolved by
    // SteamHeaterPolicy. Every surface that displays a steam temperature reads
    // this to decide whether to show "Off" instead of a number.
    //
    // It must NOT be inferred from DE1Device.steamTemperature: that is the
    // MEASURED boiler temperature, which keeps reporting 140, 130, 120 … for many
    // minutes after the heater is switched off. A cooling boiler and a hot one
    // are indistinguishable by their reading, and only this tells them apart.
    Q_PROPERTY(bool steamHeaterOn READ steamHeaterOn NOTIFY steamHeaterStateChanged)

public:
    explicit MainController(QNetworkAccessManager* networkManager,
                           Settings* settings, DE1Device* device,
                           MachineState* machineState, ShotDataModel* shotDataModel,
                           ProfileStorage* profileStorage = nullptr,
                           QObject* parent = nullptr);

    // ProfileManager accessor
    ProfileManager* profileManager() const { return m_profileManager; }
    // THE steam-target derivation. Anything that needs to know whether the
    // steam heater should be warm asks this, and never re-derives it.
    SteamHeaterPolicy* steamHeaterPolicy() const { return m_steamHeaterPolicy; }
    bool steamHeaterOn() const { return m_steamHeaterPolicy && m_steamHeaterPolicy->resolve().on; }

    // Non-profile accessors
    double filteredGoalPressure() const { return m_filteredGoalPressure; }
    double filteredGoalFlow() const { return m_filteredGoalFlow; }
    VisualizerUploader* visualizer() const { return m_visualizer; }
    VisualizerImporter* visualizerImporter() const { return m_visualizerImporter; }
    BeanBaseClient* beanbase() const { return m_beanbase; }
    ProfileStorage* profileStorage() const { return m_profileStorage; }
    AIManager* aiManager() const { return m_aiManager; }
    LiveSteamCoach* liveSteamCoach() const { return m_liveSteamCoach; }
    // Injects the TranslationManager into the components that localize
    // user-visible strings: the live steam coach (cue i18n), the Visualizer
    // importer and uploader (error/status messages), the update checker
    // (update error messages), and the migration client (what its AI-conversation
    // step could not do).
    void setTranslationManager(TranslationManager* tm) {
        if (m_liveSteamCoach) m_liveSteamCoach->setTranslationManager(tm);
        if (m_visualizerImporter) m_visualizerImporter->setTranslationManager(tm);
        if (m_visualizer) m_visualizer->setTranslationManager(tm);
        if (m_updateChecker) m_updateChecker->setTranslationManager(tm);
        if (m_dataMigration) m_dataMigration->setTranslationManager(tm);
    }
    void setAiManager(AIManager* aiManager) {
        m_aiManager = aiManager;
        if (m_aiManager && m_shotHistory)
            m_aiManager->setShotHistoryStorage(m_shotHistory);
        if (m_aiManager && m_profileManager)
            m_aiManager->setProfileManager(m_profileManager);
        if (m_aiManager && m_dataMigration)
            m_dataMigration->setAIManager(m_aiManager);
    }
    void setBLEManager(BLEManager* bleManager) { m_bleManager = bleManager; }
    void setFlowScale(FlowScale* flowScale) { m_flowScale = flowScale; }
    void setTimingController(ShotTimingController* controller) { m_timingController = controller; }
    void setBackupManager(DatabaseBackupManager* backupManager) { m_backupManager = backupManager; }
    ShotDataModel* shotDataModel() const { return m_shotDataModel; }
    SteamDataModel* steamDataModel() const { return m_steamDataModel; }
    SteamHealthTracker* steamHealthTracker() const { return m_steamHealthTracker; }
    void setSteamDataModel(SteamDataModel* model) { m_steamDataModel = model; }
    void setSteamHealthTracker(SteamHealthTracker* tracker) { m_steamHealthTracker = tracker; }
    bool isSawSettling() const;
    QString currentFrameName() const { return m_currentFrameName; }
    ShotHistoryStorage* shotHistory() const { return m_shotHistory; }
    CoffeeBagStorage* bagStorage() const { return m_bagStorage; }
    EquipmentStorage* equipmentStorage() const { return m_equipmentStorage; }
    RecipeStorage* recipeStorage() const { return m_recipeStorage; }
    QVariantMap activeRecipe() const { return m_activeRecipe; }
    double activeBaselineTemperatureC() const;
    double activeBaselineYieldG() const;
    double activeBaselineYieldValue() const;
    QString activeBaselineYieldMode() const;
    // One rung of the baseline ladder (recipe -> bag -> profile). The value
    // and the mode MUST come from the SAME rung: activeBaselineYieldG()
    // resolves one against the other, so pairing a recipe's "ratio" with a
    // bag's 40 g would target 40 x the dose — 720 g. The public getters
    // above are views onto a single walk (resolveBaselineYield) rather than
    // two hand-duplicated walks that merely happen to agree today.
    struct BaselineYield {
        double value = 0.0;
        QString mode = YieldSpec::modeNone();
    };
    bool temperatureIsRealOverride() const;
    bool yieldIsRealOverride() const;
    qint64 selectedRecipeId() const { return m_recipeSelection.selected(); }
    UnifiedBeanSearchModel* beanSearch() const { return m_beanSearch; }
    ShotImporter* shotImporter() const { return m_shotImporter; }
    ProfileConverter* profileConverter() const { return m_profileConverter; }
    ProfileImporter* profileImporter() const { return m_profileImporter; }
    ShotComparisonModel* shotComparison() const { return m_shotComparison; }
    ShotServer* shotServer() const { return m_shotServer; }
    MqttClient* mqttClient() const { return m_mqttClient; }
    UpdateChecker* updateChecker() const { return m_updateChecker; }
    HdsFirmwareUpdateController* hdsFirmwareUpdate() const { return m_hdsFirmwareUpdate; }
    void setScaleDeviceProxy(ScaleDeviceProxy* proxy);
    FirmwareUpdater* firmwareUpdater() const { return m_firmwareUpdater; }
    ShotReporter* shotReporter() const { return m_shotReporter; }
    DataMigrationClient* dataMigration() const { return m_dataMigration; }
    DatabaseBackupManager* backupManager() const { return m_backupManager; }
    LocationProvider* locationProvider() const { return m_locationProvider; }
    qint64 lastSavedShotId() const { return m_lastSavedShotId; }

    // For simulator integration
    void handleShotSample(const ShotSample& sample) { onShotSampleReceived(sample); }

    // Uses shot history. `doseOverride`, when > 0, overrides the shot record's dose
    // so the loaded recipe matches the auto-favorite card (which buckets dose to
    // the nearest 0.5 g). Pass 0 to use the shot's saved dose unchanged.
    Q_INVOKABLE void loadShotWithMetadata(qint64 shotId, double doseOverride = 0);

    // --- Recipes (add-recipes) ---
    // Activate a recipe: apply its profile, linked bag (whether or not it is
    // still in inventory — stale recipes activate fully), equipment,
    // dose/yield/temp, the recipe's own grind, and steam block (the single activation
    // path shared by QML pill taps, MCP recipe_activate, and the web
    // /activate route). Async; terminal status via recipeActivated().
    Q_INVOKABLE void activateRecipe(qint64 recipeId);
    // Start an espresso shot for the currently SELECTED recipe, but only once
    // its profile has actually been applied to the machine. If the selected
    // recipe's activation is already confirmed (activeRecipeId == selectedRecipeId)
    // the shot starts immediately; if activation is still in flight (the
    // background DB read + BLE profile upload have not landed) the start is
    // armed and fires the instant recipeActivated(id, true) arrives — so a fast
    // second tap can never pull a shot on the previous, not-yet-replaced
    // profile. A failed activation, or selecting a different recipe, cancels the
    // armed start. Callers gate on machine-ready / canStartOperations first.
    Q_INVOKABLE void startSelectedRecipeShotWhenApplied();
    // Leave the recipe (pill deselects). The recipe row itself is unchanged;
    // live settings stay as they are — the user is free-styling now.
    Q_INVOKABLE void deactivateRecipe();
    // Auto-load entry point (recipe-auto-load), mirroring
    // ProfileManager::loadAutoLoadProfileIfNeeded(). No-op when
    // Settings.dye.autoLoadRecipeId is -1 or already the active recipe;
    // clears the setting and emits autoLoadRecipeStaleCleared() when the id
    // no longer resolves to an existing, non-archived recipe; otherwise
    // calls activateRecipe(). Invoked from qml/main.qml at app startup, DE1
    // Sleep->Idle wake, and the idle-inactivity countdown, alongside
    // ProfileManager's equivalent call — the two settings are mutually
    // exclusive so at most one ever does real work per trigger.
    Q_INVOKABLE void loadAutoLoadRecipeIfNeeded();
    // Compact-JSON snapshot of the steam spec currently in effect (recipe's
    // hasMilk when one is active, plus live steam settings + pitcher +
    // milk weight). Stamped onto every saved shot and used by the composer
    // to prefill promote-from-shot steam. Public for the shot-save path,
    // MCP, and web prefill.
    QString currentSteamSpecJson() const;

    // Compact-JSON snapshot of the hot-water spec currently in effect (recipe's
    // hasWater when a hot-water recipe is active, plus the selected water
    // vessel's values). Empty unless a hot-water recipe is active. Stamped onto
    // every saved shot and used by the composer to prefill promote-from-shot.
    QString currentHotWaterSpecJson() const;

    // Sticky DYE metadata shared by the real and simulated shot-save paths.
    // Callers add the per-shot fields (weights, yield provenance) themselves.
    ShotMetadata buildShotMetadataFromSettings() const;

    // Clipboard
    Q_INVOKABLE void copyToClipboard(const QString& text);
    Q_INVOKABLE QString pasteFromClipboard() const;

    // --- Recipes-first layout upgrade offer (recipes-idle-layout-upgrade) ---
    // Background check for the one-time upgrade dialog: whether accepting
    // will create a starter recipe (user has zero recipes and at least one
    // saved shot), plus the drink-type heuristic pre-selection from that
    // shot's steam snapshot. Emits recipesUpgradeOfferReady() when done.
    Q_INVOKABLE void checkRecipesUpgradeEligibility();
    // Accept path: applies the layout transform and, when eligible, creates
    // and activates a starter recipe from the last shot using `name` (already
    // translated by the caller) and the user's Espresso/Milk choice. Emits
    // recipesUpgradeApplied() when finished — see its doc for the three
    // possible outcomes.
    Q_INVOKABLE void acceptRecipesFirstUpgrade(const QString& name, bool hasMilk);

    // Wait until every storage's background DB WRITE worker has finished, or the
    // budget runs out. Logs both outcomes itself, so there is no result to check.
    //
    // Call this before the storages are destroyed. `~SerialDbWorker` warns that
    // it is discarding queued tasks, and until now nothing in production ever
    // waited: three storages had an `isDbWorkIdle()` with no caller outside the
    // tests, `RecipeStorage` had no accessor at all, and `aboutToQuit` drained
    // the BLE queue but not the database.
    // So a dose, note, rating or Visualizer id written in the seconds before
    // quit could vanish, with the warning going to a log nobody reads on the way
    // out. Found from the other side, when a test destroyed a CoffeeBagStorage
    // with two writes still queued.
    //
    // Bounded on purpose. This runs on the quit path, where Android will kill a
    // process that takes too long, so a stuck worker must not hold the app open
    // — the same reason the BLE drain beside it carries a safety-net timeout.
    //
    // WHAT THIS DOES NOT COVER, because the comment above otherwise reads as if
    // the whole class of loss is closed. Two call sites reach this — `aboutToQuit`
    // and `Qt::ApplicationSuspended` — and NEITHER runs when the OS kills the
    // process outright: an Android
    // low-memory kill or force-stop, an iOS SIGKILL (the NORMAL iOS termination —
    // qioseventdispatcher.mm:434 says so outright), a fatal signal reaching
    // crashhandler.cpp's re-raise, or an ASan abort. Those lose queued writes with
    // no warning whatsoever, since ~SerialDbWorker never runs either.
    //
    // Android is the primary platform and is usually backgrounded rather than
    // quit, so the quit path alone would cover very little there. That is why
    // Qt::ApplicationSuspended also calls this (main.cpp), with a shorter budget:
    // backgrounding is the last hook before an OS kill, and it is the one that
    // actually fires on Android.
    //
    // That second call was briefly left out and documented as out of scope, on the
    // grounds that it risked an ANR and needed device measurement. Neither claim
    // was checked before it was written. It is recorded here because the reasoning
    // was backwards: the platform where the loss is most likely is the last place
    // to accept a narrower fix, and "documented" is not a substitute for "fixed".
    enum class DrainReason { Exiting, Backgrounding };
    void drainDbWork(int timeoutMs = 750, DrainReason reason = DrainReason::Exiting);

public slots:
    // Select a steam pitcher AND apply its stored values (duration, flow,
    // temperature), then push them to the machine. THE one implementation of
    // "the user picked this pitcher".
    //
    // It existed four times before this — the MCP select path, the idle pill
    // row, the Steam widget popup and the Steam page's activation handler — and
    // recipe activation was a fifth caller that did only half of it, storing the
    // index without applying the values, so activating a recipe heated to the
    // last global temperature instead of its own pitcher's.
    //
    // `milkFallbackG` is the surface's own answer for "how much milk" when
    // nothing is on the scale; it is the one thing that legitimately differs per
    // caller, so it is a parameter rather than another copy of the function.
    Q_INVOKABLE void selectSteamPitcher(int index, double milkFallbackG = 0.0);

    void applySteamSettings();
    void applyHotWaterSettings();
    void applyFlushSettings();

    // Real-time hot water setting updates
    void setHotWaterFlowRateImmediate(int flow);

    // Real-time steam setting updates
    void setSteamTemperatureImmediate(double temp);
    void setSteamFlowImmediate(int flow);
    void setSteamTimeoutImmediate(int timeout);

    // Soft stop steam (sends 1-second timeout to trigger elapsed > target, no purge)
    Q_INVOKABLE void softStopSteam();

    // Grant event permission and push the resolved target — for when the user
    // wants to steam NOW, whatever the standing settings say. `reason` is a
    // caller-identifying tag that flows into the [ShotSettings] BLE log so
    // redundant calls (convergent QML signals, state/phase/isSteaming
    // transitions) can be attributed. Pass a short kebab-case string like
    // "steampage-activated".
    Q_INVOKABLE void startSteamHeating(const QString& reason = QString());

    // Revoke event permission and re-push the resolved target. Called on the
    // return to Idle: a user who steamed once has not thereby asked for a warm
    // boiler forever, but a user with Keep warm when idle on has — so this
    // re-resolves rather than sending 0, which is what the QML used to do and
    // what made the two settings fight each other.
    Q_INVOKABLE void releaseSteamEventPermission();

    // Apply the transient veto and push the resolved target (sends 0 C).
    Q_INVOKABLE void turnOffSteamHeater();

    // The descale page needs the steam heater off for the whole time it is preparing to
    // descale, which can be an hour of waiting for the boiler to reach 60 °C. That is far
    // longer than the page itself is guaranteed to live — auto-sleep replaces it with the
    // screensaver, and a descale started from the GHC replaces it with a fresh instance —
    // so the hold and the value to restore live here, not in the page.
    //
    // begin is idempotent: a second page instance taking over does not re-capture, so the
    // original pre-descale value survives. end is the only thing that restores, and is
    // called when the user actually leaves — never from destruction, which fires for
    // screensaver and page churn too.
    void snapshotForDescaleHeaterHold();
    Q_INVOKABLE void beginDescaleHeaterHold();
    Q_INVOKABLE void endDescaleHeaterHold();
    // Drops the hold WITHOUT restoring, for when an explicit steam request has made the
    // captured value stale. Called from startSteamHeating().
    void abandonDescaleHeaterHold();

    // Flip the heater from whatever it is now. The DIRECTION rule — which of the
    // two calls above matches which resolved state — lived at two QML sites, and
    // a sweep that changed it already missed one of them once.
    Q_INVOKABLE void toggleSteamHeater(const QString& reason = QString());

    // #1161: QML already resolves a stop reason for its overlay
    // ("manual" | "weight" | "machine" | "") across every stop entry
    // point. It pushes that here via a single onStopReasonChanged handler
    // so the saved shot can record why it ended and the dial-in advisor
    // can discount the arbitrary yield of manually-stopped shots. At save
    // time (onShotEnded) C++ SAW/SAV ground truth takes precedence; this
    // QML value supplies "manual", and "weight" only as a defensive
    // fallback when the C++ SAW flag did not capture a weight stop.
    Q_INVOKABLE void reportShotStopReason(const QString& reason);

    void onEspressoCycleStarted();
    void onShotEnded();
    void onScaleWeightChanged(double weight);  // Called by scale weight updates

    // Developer mode: generate fake shot data for testing UI
    Q_INVOKABLE void generateFakeShotData();

    // Clear crash log file (called from QML after user dismisses crash report dialog)
    Q_INVOKABLE void clearCrashLog();

    Q_INVOKABLE void factoryResetAndQuit();

    // Mid-shot SAW adjustment (e.g. user pressed +10g to "salvage" a too-fast shot).
    // No-op outside Preinfusion/Pouring or when no SAW target is set. Intentionally
    // only mutates MachineState — leaving the persisted profile/setting untouched so
    // the next shot reverts to the user's normal target.
    Q_INVOKABLE void bumpTargetWeight(double deltaG);

signals:
    void sawSettlingChanged();
    void steamHeaterStateChanged();

    // Filtered goal setpoints (zeroed for non-active mode, reset when extraction ends)
    void goalsChanged();

    // Accessibility: emitted when extraction frame changes
    void frameChanged(int frameIndex, const QString& frameName, const QString& transitionReason);

    // DYE: emitted when shot ends and should show metadata page
    // shotId of the just-saved shot, or 0 when the save failed / history
    // was unavailable — the navigation handler must not fall back to
    // lastSavedShotId, which still points at the PREVIOUS shot in those
    // cases (opening it would let its edits sticky-sync forward).
    void shotEndedShowMetadata(qint64 shotId);
    void lastSavedShotIdChanged();

    // A real espresso shot has been persisted, carrying the SAME finalized
    // duration and yield that went into the stored row. Consumers that summarize
    // the last shot must use these rather than re-deriving from ShotDataModel:
    // the model's stopTime is only written on the stop-at-weight path, so on any
    // other ending (manual stop, profile end, volume) it keeps its -1 sentinel,
    // and its raw cumulative weight is a different source from the one the row
    // stores — the unrounded last point of the graph series, not the timing
    // controller's settled reading. Both are decided here in onShotEnded() —
    // extractionDuration() excludes SAW
    // settling, and the yield is the settled, 0.1 g-rounded value — so this
    // signal is the only place the finalized pair is available together.
    //
    // Emitted only for a successful save (shotId > 0); a failed save has no row
    // to describe. generateFakeShotData() deliberately does NOT emit it — a
    // dev-simulated row is not a shot anyone pulled.
    //
    // Parameter order is (duration, yield) to match shotDiscarded() below and
    // ShotHistoryStorage::saveShot(), the two other carriers of this same pair
    // in onShotEnded(). Both are doubles, so a transposed call would compile and
    // produce a plausible-looking "30.0 g in 36 s" — matching the neighbours is
    // the only thing making that mistake visible at the call site. (#1658)
    void shotPersisted(qint64 shotId, double durationSec, double yieldG);

    // Shot aborted because saved scale is not connected
    void shotAbortedNoScale();

    // Shot metadata loaded from history (for async loadShotWithMetadata)
    void shotMetadataLoaded(qint64 shotId, bool success);

    // Recipe activation finished (add-recipes). success=false when the
    // recipe id was not found or storage failed. Terminal status for QML
    // pill taps, MCP recipe_activate, and the web /activate route.
    void recipeActivated(qint64 recipeId, bool success);

    // Emitted with every recipeActivated(id, false), carrying what the user has
    // to fix. recipeActivated alone was the only report of a failure and no QML
    // handled it, so a failed activation showed as a pill that lit and then
    // silently reverted — no message, nothing marking the recipe.
    //
    // missingProfileTitle is the profile the recipe names when that is the
    // cause (not installed and no stored JSON), and empty for any other
    // failure, so the message can name the value to change in the editor.
    void recipeActivationFailed(qint64 recipeId, const QString& recipeName,
                                const QString& missingProfileTitle);
    void activeRecipeChanged();
    void brewBaselineChanged();
    void selectedRecipeIdChanged();

    // Emitted when the configured Settings.dye.autoLoadRecipeId no longer
    // resolves to an existing, non-archived recipe — either discovered by
    // loadAutoLoadRecipeIfNeeded() at a trigger, or proactively when the
    // pinned recipe is edited or deleted (mirrors
    // ProfileManager::autoLoadStaleCleared()). The setting is already
    // cleared to -1 by the time this fires; QML uses it to show a toast.
    void autoLoadRecipeStaleCleared();

    // Recipes-first layout upgrade offer (recipes-idle-layout-upgrade):
    // willCreateStarterRecipe/milkPreselected answer checkRecipesUpgradeEligibility().
    // recipesUpgradeApplied() answers acceptRecipesFirstUpgrade() — the layout
    // transform has always already applied by the time it fires. Three
    // outcomes: (recipeName, false) = starter recipe created; ("", false) =
    // no starter recipe was requested (not eligible); ("", true) = a starter
    // recipe WAS requested but creation failed — starterRecipeFailed
    // distinguishes this from the "not requested" case so the UI can surface
    // the failure instead of showing a silent success toast.
    void recipesUpgradeOfferReady(bool willCreateStarterRecipe, bool milkPreselected);
    void recipesUpgradeApplied(const QString& recipeName, bool starterRecipeFailed);

    // Auto-wake: emitted when scheduled wake time is reached
    void autoWakeTriggered();

    // Remote sleep: emitted when sleep is triggered via MQTT or REST API
    void remoteSleepRequested();

    // Auto flow calibration: emitted when per-profile multiplier is updated
    void flowCalibrationAutoUpdated(const QString& profileTitle, double oldValue, double newValue);

    // Aborted-shot classifier: shot did not start and was discarded (not saved to history).
    // Always fires when the classifier triggers — there is no user opt-out.
    // Informational only — the shot is intentionally not recoverable.
    void shotDiscarded(double durationSec, double finalWeightG);

private slots:
    void onShotSampleReceived(const ShotSample& sample);
    // Verify that the DE1's stored ShotSettings match what we've commanded.
    // Logs drift and auto-heals by re-sending ShotSettings, with a retry
    // budget to avoid infinite loops when the DE1 refuses the value.
    void onShotSettingsReported(double deviceSteamTargetC, int deviceSteamDurationSec,
                                double deviceHotWaterTempC, int deviceHotWaterVolMl,
                                double deviceGroupTargetC);

private:
    // The instance create() hands to the engine. Not owned here — main's stack object outlives
    // the engine, which is why create() pins CppOwnership.

    void applyAllSettings();
    void applyLoadedShotMetadata(qint64 shotId, const ShotRecord& shotRecord, double doseOverride = 0,
                                 qint64 matchedBagId = -1);
    void applyWaterRefillLevel();
    void applyRefillKitOverride();
    void applyHeaterTweaks();
    void applyFlowCalibration();
    /// Record that this shot produced no calibration ideal, and why. Counts
    /// consecutive rejections per profile so the permanent case (a profile whose
    /// pours never reach their target flow) can be told apart from the ordinary
    /// one (not enough shots yet) — from the outside they are identical, and
    /// only one of them is fixed by pulling more shots.
    void noteAutoFlowCalRejection(const QString& profileName, const QString& reason);

    /// Compute and accumulate this shot's auto-flow-calibration ideal.
    /// `pouredMultiplier` is the multiplier the shot actually POURED under,
    /// from the shot-start latch; pass 0 when the shot never latched and a live
    /// read is the only option. It matters because the ideal divides the
    /// reported flow by it, and reported flow carries the multiplier that was
    /// in effect DURING the pour — a value written mid-shot (the MCP
    /// flow_calibration tool does exactly this) would otherwise be paired with
    /// samples recorded under the old one.
    void computeAutoFlowCalibration(double pouredMultiplier);
    void updateGlobalFromPerProfileMedian();
    double getGroupTemperature() const;
    // `reason` is a caller tag that flows into the [ShotSettings] BLE log.
    void sendMachineSettings(const QString& reason = QString());

    // Drain the migration16 pending list, re-PATCHing each affected
    // shot's rating to Visualizer so the cloud copy matches the local
    // corrected value. Serial: pops one entry, dispatches the PATCH,
    // resumes from VisualizerUploader::updateSuccess — or from a
    // permanent updateFailed after evicting the entry. On transient
    // failure (anything but 404) the entry remains in the list and the
    // drain pauses until the next app boot; on permanent failure (404 —
    // shot gone from Visualizer) the entry is evicted, the local row's
    // dead visualizer_id cleared (guarded on still holding that id), and
    // draining continues (#1431).
    void processPendingVisualizerRatingSync();
    void dispatchNextPendingVisualizerSync();

    // One-time Visualizer reconciliation backfill (OpenSpec
    // persist-visualizer-id-in-controller). Run-once via QSettings
    // visualizerBackfill/doneV1. Lists the user's Visualizer shots,
    // relinks orphaned local rows by timestamp, then pushes the
    // now-authoritative local rating to each linked cloud shot by
    // appending to the same serial drain queue used above. Skips
    // without setting the flag when credentials are absent.
    void processVisualizerReconciliation();

    // Drain the Visualizer bean-repair queue: the uploaded shots whose bag was
    // unlinked from a borrowed canonical record, flagged at the unlink. No run-once flag and no library
    // scan — the flags are the state, and each is cleared once the server says
    // that shot needs nothing. Called at startup and whenever the bag inventory
    // changes; a no-op without Visualizer credentials.
    void processVisualizerBeanRepair();
    // Tracks the visualizerId of the migration16 PATCH currently in
    // flight. Empty string when no migration16 sync is active. Compared
    // against the visualizerId argument on updateSuccess / updateFailed
    // so other concurrent PATCHes (e.g., from PostShotReviewPage's
    // metadata save) don't pop migration16 entries off the queue or
    // stall the drain. See PR #1155 review note 2.
    QString m_migration16InFlightVisualizerId;

    ProfileManager* m_profileManager = nullptr;

    // THE steam-heater target derivation, shared with ProfileManager. Never
    // re-derive a steam temperature at a call site — see steamheaterpolicy.h.
    SteamHeaterPolicy* m_steamHeaterPolicy = nullptr;

    // See beginDescaleHeaterHold(). The captured value is the transient steamDisabled veto
    // as it stood before the descale page turned the heater off — the only input the hold
    // disturbs that can legitimately be put back. snapshotForDescaleHeaterHold() records why
    // event permission and the selected pitcher are deliberately not captured.
    bool m_descaleHeaterHold = false;
    bool m_descaleHeaterHoldPrevSteamDisabled = false;

    QNetworkAccessManager* m_networkManager = nullptr;
    Settings* m_settings = nullptr;
    DE1Device* m_device = nullptr;
    MachineState* m_machineState = nullptr;
    ShotDataModel* m_shotDataModel = nullptr;
    ProfileStorage* m_profileStorage = nullptr;
    VisualizerUploader* m_visualizer = nullptr;
    VisualizerImporter* m_visualizerImporter = nullptr;
    BeanBaseClient* m_beanbase = nullptr;
    AIManager* m_aiManager = nullptr;
    LiveSteamCoach* m_liveSteamCoach = nullptr;
    ShotTimingController* m_timingController = nullptr;
    BLEManager* m_bleManager = nullptr;
    FlowScale* m_flowScale = nullptr;  // Shadow FlowScale for comparison logging

    SteamDataModel* m_steamDataModel = nullptr;
    SteamHealthTracker* m_steamHealthTracker = nullptr;
    // Wall-clock millisecond stamp for steam. Steam doesn't go through
    // ShotTimingController, so it carries its own anchor; espresso elapsed
    // time is sourced from m_timingController->shotTime() to keep phase
    // markers and graph data on a single base. The BLE-encoded
    // sample.timer field is a 16-bit value that wraps every ~655 s, so it
    // must not be subtracted across persistent state without explicit
    // unwrap (m_lastSampleTime is the only place that touches it, and the
    // delta computation handles the wrap inline).
    qint64 m_steamStartTimeMs = 0;  // Wall-clock ms at first steam sample of session

    // Previous phase, for the steam-heater transitions that are edge-triggered
    // rather than level-triggered: waking from Sleep must re-assert the target
    // (the transient veto is cleared at sleep and nothing else re-sends it), and
    // shot start must grant the recipe its event permission exactly once.
    // Held as int, not MachineState::Phase: this header only forward-declares
    // MachineState, and pulling the full definition in for one enum would put
    // every consumer of maincontroller.h behind it.
    int m_lastPhaseForSteam = -1;

    double m_lastSampleTime = 0;    // Previous sample.timer value, for inter-sample delta with explicit wrap handling
    double m_lastShotTime = 0;      // Last shot sample time relative to shot start (for weight sync)
    bool m_extractionStarted = false;
    int m_lastFrameNumber = -1;
    int m_trackLogCounter = 0;
    double m_filteredGoalPressure = 0.0;
    double m_filteredGoalFlow = 0.0;
    int m_frameWeightSkipSent = -1;  // Frame number for which we've sent a weight-based skip command
    double m_frameStartTime = 0;     // Shot-relative time when current frame started

    // ShotSettings drift auto-heal tracking. The written values live on
    // DE1Device (so every call site — MainController, ProfileManager — feeds
    // the same tracker); only the retry bookkeeping is here. Reset in
    // applyAllSettings() so every reconnect starts with a fresh budget.
    int m_shotSettingsDriftResendCount = 0;
    // Closes a "giving up" episode and reports its tally — see m_driftGiveUpLog.
    void flushDriftGiveUpLog();
    // The "giving up after N resend attempts" WARN, collapsed.
    //
    // That branch RETURNS without latching anything, so it is re-entered on
    // every subsequent drifting indication and re-warns each time. Measured on a
    // submitted log: 60 byte-identical WARN lines in 6.7 seconds, about nine per
    // second, and they were the single loudest thing in the whole buffer. The
    // word "giving up" is what makes it wrong rather than merely noisy — the
    // ladder gives up RESENDING and never gives up warning, so the terminal line
    // of the narrative repeats forever and trains a reader to skim the one tier
    // that is supposed to mean "look here". Same cry-wolf pattern
    // BLEManager::scaleRepeatFailure() exists to prevent, in a file that had no
    // equivalent.
    //
    // kChangesOnly rather than a window: every repeat is the same fact, and the
    // fact that drift CONTINUED is carried by the tally, which states it better
    // than sixty lines do. logcollapse.h's "a source gated on a problem wants a
    // real window" carve-out is about sources whose repeats are evidence — here
    // the repeats are one exhausted ladder being asked the same question.
    //
    // EPISODIC, flushed at all three places the ladder resets: the
    // device-gone path, the drift-resolved path, and applyAllSettings()'s
    // reconnect reset. Each already ends with an INFO line, so the tally lands
    // beside the resolution a reader is looking for.
    LogCollapse m_driftGiveUpLog{LogCollapse::kChangesOnly};
    double m_lastPressure = 0;       // Last sample pressure (for transition reason inference)
    double m_lastFlow = 0;           // Last sample flow (for transition reason inference)
    // The sample BEFORE m_lastPressure/m_lastFlow. The machine can cross a
    // frame's exit threshold between two BLE samples, so the per-sample delta
    // is the tolerance FrameExit::inferReason extrapolates with. m_prevValid is
    // false on the first sample of a shot, where there is nothing to
    // extrapolate from and no tolerance is given.
    double m_prevPressure = 0;
    double m_prevFlow = 0;
    bool m_prevValid = false;
    bool m_tareDone = false;  // Track if we've tared for this shot
    QString m_pendingStopReason;  // #1161: QML-reported stop reason for the in-flight shot

    QString m_currentFrameName;  // For accessibility announcements

    QTimer m_heaterTweaksTimer;  // Debounce slider changes before sending MMR writes

    qint64 m_lastSavedShotId = 0;  // ID of most recently saved shot (for post-shot review)
    bool m_savingShot = false;     // Guard against overlapping async saves

    // Shot history and comparison
    ShotHistoryStorage* m_shotHistory = nullptr;
    CoffeeBagStorage* m_bagStorage = nullptr;
    EquipmentStorage* m_equipmentStorage = nullptr;
    RecipeStorage* m_recipeStorage = nullptr;

    // --- Recipes (add-recipes) ---
    // Cached row of the active recipe (empty = none), kept fresh by
    // activation and by recipesChanged re-reads. Drives the recipe-owned
    // grind apply and the write-through stamps.
    QVariantMap m_activeRecipe;
    // Event-based guard (never a timer): true while activation is applying
    // the recipe's values, so the deactivate-on-ingredient-swap watchers and
    // the write-through stamps ignore self-inflicted change signals.
    bool m_applyingRecipe = false;
    // Outstanding write-through stamps whose recipeUpdated echo should not
    // trigger a cache re-read (mirrors SettingsDye::m_pendingSelfWrites).
    int m_pendingRecipeSelfWrites = 0;
    // Set true immediately before the edit-triggered re-read of the ACTIVE
    // recipe (the recipeUpdated → requestRecipe hop), so the recipeReady
    // handler mirrors the refreshed grind/rpm back onto the live dial
    // (Settings.dye). The Shot Plan widget binds to the dial, not the recipe
    // cache, so an edit of the active recipe's grind (wizard/MCP/web) must
    // re-push it or the plan goes stale until re-activation (the Flow-3 refresh
    // bug). Left false on startup restore, relink refresh, and QML editor
    // prefill reads — none of which should re-apply values to the live session.
    bool m_refreshDialFromRecipeEdit = false;
    // recipe-auto-load: the id loadAutoLoadRecipeIfNeeded() is waiting on a
    // recipeReady()/recipeCheckFailed() for, so its existence/archived check
    // can filter out every OTHER recipeReady the app fires (active-recipe
    // refresh, wizard reads, etc.) without needing a one-shot connection.
    // This is the only pending flag that may lead to activateRecipe() — see
    // m_pendingAutoLoadRecheckId below for the edit-time re-check, which
    // never activates. -1 = not waiting.
    qint64 m_pendingAutoLoadRecipeId = -1;
    // recipe-auto-load: the id the proactive re-check (the recipeUpdated
    // hook in setupRecipeConnections, fired when the pinned recipe itself is
    // edited) is waiting on. Kept separate from m_pendingAutoLoadRecipeId so
    // that editing the pinned recipe can only ever clear a newly-stale pin —
    // never silently activate it and switch the live session. -1 = not
    // waiting.
    qint64 m_pendingAutoLoadRecheckId = -1;
    // Pure state machine behind selectedRecipeId + the deferred recipe-shot
    // start. MainController only wires it to Qt signals and the device; the
    // policy (lead/converge/rollback, arm/fire) lives in the header-only model
    // so it can be unit-tested without linking MainController.
    RecipeSelectionModel m_recipeSelection;
    // Apply the activation bundle on the main thread (recipeActivationReady).
    void applyActivatedRecipe(qint64 recipeId, const QVariantMap& recipe,
                              qint64 linkedBagId, const QVariantMap& linkedBag);
    // The single walk of the baseline ladder — see BaselineYield.
    BaselineYield resolveBaselineYield() const;
    // Re-seed the SESSION yield/temperature overrides from a recipe's spec,
    // replacing whatever is armed. Shared by activation and the
    // active-recipe edit refresh (an edit re-seeds the brew exactly as
    // re-activating would). Pass an empty linkedBag to resolve the bag rung
    // from the live dye cache. Returns true when an override was armed.
    bool applyRecipeBrewOverrides(const QVariantMap& recipe,
                                  const QVariantMap& linkedBag = QVariantMap());
    // Stamp a tweak onto the active recipe row (no-op when none is active
    // or activation is applying).
    void stampActiveRecipe(const QString& field, const QVariant& value);
    // True when the pitcher the recipe's steam block names is still one of the
    // stored presets. Distinguishes "the user picked another pitcher" from
    // "the recipe's pitcher was deleted", which reach the selection watcher
    // identically. A heater-off recipe names the built-in, which cannot be
    // deleted, so it always counts as present.
    bool recipeSteamPitcherStillExists(const QString& steamJson) const;
    // Recipes-first layout upgrade offer (recipes-idle-layout-upgrade):
    // cached result of the last checkRecipesUpgradeEligibility() background
    // pass, consumed by acceptRecipesFirstUpgrade().
    bool m_recipesUpgradeWillCreate = false;
    ShotRecord m_recipesUpgradeShotRecord;
    // THE single ShotSettings write for every steam entry point. The four
    // non-steam fields are read live from Settings here, so no caller can push
    // a half-stale payload — four call sites used to hand-roll this identical
    // block and differed only in which steam value they had derived.
    // Returns false when the payload never left the app (no device, or
    // disconnected) so callers do not log success for a command that was
    // dropped. The eventual state is self-healing — the return-from-dormant
    // re-assert covers reconnect — but the log must not claim otherwise.
    bool pushShotSettings(double steamTempC, const QString& reason);

    // Apply an active recipe's pitcher as an OVERRIDE of the user's standing
    // selection, or (with SettingsBrew::NoStandingPitcher) unwind back to it.
    //
    // A recipe's pitcher is part of the drink, not a new preference: activating
    // a latte used to overwrite the standing selection outright, so a user whose
    // resting state was "Heater off" got a warm boiler for the rest of the day
    // after one milk drink. Idempotent — re-activating while an override is
    // already parked does not overwrite what was parked.
    void setRecipeSteamPitcherOverride(int index);
    // Wire the deactivation watchers + write-through stamps (called once
    // from the constructor after storages exist).
    void setupRecipeConnections();
    // Migration 31's deferred data pass (recipe-relative-temp-offset): snapshot
    // the profile catalog's title→temperature map on the main thread and hand
    // it to RecipeStorage to convert legacy absolute temps into offsets. Run
    // at startup and re-run after imports that can land legacy-source rows.
    void requestRecipeTempOffsetConversion();
    UnifiedBeanSearchModel* m_beanSearch = nullptr;
    ShotImporter* m_shotImporter = nullptr;
    ProfileConverter* m_profileConverter = nullptr;
    ProfileImporter* m_profileImporter = nullptr;
    ShotDebugLogger* m_shotDebugLogger = nullptr;
    ShotComparisonModel* m_shotComparison = nullptr;
    ShotServer* m_shotServer = nullptr;
    MqttClient* m_mqttClient = nullptr;
    UpdateChecker* m_updateChecker = nullptr;
    HdsFirmwareUpdateController* m_hdsFirmwareUpdate = nullptr;
    DE1::Firmware::FirmwareAssetCache* m_firmwareAssetCache = nullptr;
    FirmwareUpdater* m_firmwareUpdater = nullptr;
    QTimer* m_firmwareCheckTimer = nullptr;   // weekly recurring check
    LocationProvider* m_locationProvider = nullptr;
    DataMigrationClient* m_dataMigration = nullptr;
    ShotReporter* m_shotReporter = nullptr;
    DatabaseBackupManager* m_backupManager = nullptr;
};

