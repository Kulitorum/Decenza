#pragma once

#include <QObject>
#include <QVariantList>
#include <QMap>
#include <QTimer>
#include "profilemanager.h"
#include "recipeselectionmodel.h"
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
#include "../network/mqttclient.h"
#include "../core/updatechecker.h"
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
class RefractometerDevice;
class ProfileStorage;
class ShotDebugLogger;
class LocationProvider;
class ShotTimingController;
class TranslationManager;
struct ShotSample;

class MainController : public QObject {
    Q_OBJECT

    // Non-profile QML properties (profile properties are on ProfileManager)
    Q_PROPERTY(VisualizerUploader* visualizer READ visualizer CONSTANT)
    Q_PROPERTY(VisualizerImporter* visualizerImporter READ visualizerImporter CONSTANT)
    Q_PROPERTY(BeanBaseClient* beanbase READ beanbase CONSTANT)
    Q_PROPERTY(AIManager* aiManager READ aiManager CONSTANT)
    Q_PROPERTY(LiveSteamCoach* liveSteamCoach READ liveSteamCoach CONSTANT)
    Q_PROPERTY(ShotDataModel* shotDataModel READ shotDataModel CONSTANT)
    Q_PROPERTY(SteamDataModel* steamDataModel READ steamDataModel CONSTANT)
    Q_PROPERTY(SteamHealthTracker* steamHealthTracker READ steamHealthTracker CONSTANT)
    Q_PROPERTY(QString currentFrameName READ currentFrameName NOTIFY frameChanged)
    Q_PROPERTY(double filteredGoalPressure READ filteredGoalPressure NOTIFY goalsChanged)
    Q_PROPERTY(double filteredGoalFlow READ filteredGoalFlow NOTIFY goalsChanged)
    Q_PROPERTY(ShotHistoryStorage* shotHistory READ shotHistory CONSTANT)
    Q_PROPERTY(CoffeeBagStorage* bagStorage READ bagStorage CONSTANT)
    Q_PROPERTY(EquipmentStorage* equipmentStorage READ equipmentStorage CONSTANT)
    Q_PROPERTY(RecipeStorage* recipeStorage READ recipeStorage CONSTANT)
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
    Q_PROPERTY(UnifiedBeanSearchModel* beanSearch READ beanSearch CONSTANT)
    Q_PROPERTY(ShotImporter* shotImporter READ shotImporter CONSTANT)
    Q_PROPERTY(ProfileConverter* profileConverter READ profileConverter CONSTANT)
    Q_PROPERTY(ProfileImporter* profileImporter READ profileImporter CONSTANT)
    Q_PROPERTY(ShotComparisonModel* shotComparison READ shotComparison CONSTANT)
    Q_PROPERTY(ShotServer* shotServer READ shotServer CONSTANT)
    Q_PROPERTY(MqttClient* mqttClient READ mqttClient CONSTANT)
    Q_PROPERTY(UpdateChecker* updateChecker READ updateChecker CONSTANT)
    Q_PROPERTY(FirmwareUpdater* firmwareUpdater READ firmwareUpdater CONSTANT)
    Q_PROPERTY(ShotReporter* shotReporter READ shotReporter CONSTANT)
    Q_PROPERTY(DataMigrationClient* dataMigration READ dataMigration CONSTANT)
    Q_PROPERTY(DatabaseBackupManager* backupManager READ backupManager CONSTANT)
    Q_PROPERTY(qint64 lastSavedShotId READ lastSavedShotId NOTIFY lastSavedShotIdChanged)
    Q_PROPERTY(bool sawSettling READ isSawSettling NOTIFY sawSettlingChanged)

public:
    explicit MainController(QNetworkAccessManager* networkManager,
                           Settings* settings, DE1Device* device,
                           MachineState* machineState, ShotDataModel* shotDataModel,
                           ProfileStorage* profileStorage = nullptr,
                           QObject* parent = nullptr);

    // ProfileManager accessor
    ProfileManager* profileManager() const { return m_profileManager; }

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
    // user-visible strings: the live steam coach (cue i18n) and the Visualizer
    // importer (error messages).
    void setTranslationManager(TranslationManager* tm) {
        if (m_liveSteamCoach) m_liveSteamCoach->setTranslationManager(tm);
        if (m_visualizerImporter) m_visualizerImporter->setTranslationManager(tm);
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
    void setRefractometer(RefractometerDevice* refractometer);
    RefractometerDevice* refractometer() const { return m_refractometer; }
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

public slots:
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

    // Send steam temperature to machine without saving to settings (for enable/disable toggle)
    Q_INVOKABLE void sendSteamTemperature(double temp);

    // Start heating steam heater (ignores keepSteamHeaterOn - for when user wants to steam).
    // `reason` is a caller-identifying tag that flows into the [ShotSettings] BLE log
    // so redundant calls (convergent QML signals, state/phase/isSteaming transitions)
    // can be attributed. Pass a short kebab-case string like "steampage-activated".
    Q_INVOKABLE void startSteamHeating(const QString& reason = QString());

    // Turn off steam heater (sends 0 C)
    Q_INVOKABLE void turnOffSteamHeater();

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

    // DYE: upload pending shot with current metadata from Settings
    Q_INVOKABLE void uploadPendingShot();

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

    // Shot aborted because saved scale is not connected
    void shotAbortedNoScale();

    // Shot metadata loaded from history (for async loadShotWithMetadata)
    void shotMetadataLoaded(qint64 shotId, bool success);

    // Recipe activation finished (add-recipes). success=false when the
    // recipe id was not found or storage failed. Terminal status for QML
    // pill taps, MCP recipe_activate, and the web /activate route.
    void recipeActivated(qint64 recipeId, bool success);
    void activeRecipeChanged();
    void brewBaselineChanged();
    void selectedRecipeIdChanged();

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
    void applyAllSettings();
    void applyLoadedShotMetadata(qint64 shotId, const ShotRecord& shotRecord, double doseOverride = 0,
                                 qint64 matchedBagId = -1);
    void applyWaterRefillLevel();
    void applyRefillKitOverride();
    void applyHeaterTweaks();
    void applyFlowCalibration();
    void computeAutoFlowCalibration();
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
    // Tracks the visualizerId of the migration16 PATCH currently in
    // flight. Empty string when no migration16 sync is active. Compared
    // against the visualizerId argument on updateSuccess / updateFailed
    // so other concurrent PATCHes (e.g., from PostShotReviewPage's
    // metadata save) don't pop migration16 entries off the queue or
    // stall the drain. See PR #1155 review note 2.
    QString m_migration16InFlightVisualizerId;

    ProfileManager* m_profileManager = nullptr;

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
    RefractometerDevice* m_refractometer = nullptr;

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

    double m_lastSampleTime = 0;    // Previous sample.timer value, for inter-sample delta with explicit wrap handling
    double m_lastShotTime = 0;      // Last shot sample time relative to shot start (for weight sync)
    bool m_extractionStarted = false;
    int m_lastFrameNumber = -1;
    int m_trackLogCounter = 0;
    double m_filteredGoalPressure = 0.0;
    double m_filteredGoalFlow = 0.0;
    int m_frameWeightSkipSent = -1;  // Frame number for which we've sent a weight-based skip command
    double m_frameStartTime = 0;     // Shot-relative time when current frame started

    // ShotSettings drift auto-heal tracking. The commanded values live on
    // DE1Device (so every call site — MainController, ProfileManager —
    // feeds the same tracker); we only keep the retry
    // bookkeeping here. Both fields are reset in applyAllSettings() so every
    // reconnect / initial-settings cycle starts with a fresh retry budget.
    int m_shotSettingsDriftResendCount = 0;
    // Event-based "is a resend in flight?" flag, cleared when the DE1's
    // next indication matches commanded (in onShotSettingsReported). Replaces
    // a wall-clock rate limiter — see CLAUDE.md's "never timers as guards"
    // rule.
    bool m_shotSettingsResendInFlight = false;
    double m_lastPressure = 0;       // Last sample pressure (for transition reason inference)
    double m_lastFlow = 0;           // Last sample flow (for transition reason inference)
    bool m_tareDone = false;  // Track if we've tared for this shot
    QString m_pendingStopReason;  // #1161: QML-reported stop reason for the in-flight shot

    QString m_currentFrameName;  // For accessibility announcements

    QTimer m_heaterTweaksTimer;  // Debounce slider changes before sending MMR writes

    // DYE: pending shot data for delayed upload
    bool m_hasPendingShot = false;
    double m_pendingShotDuration = 0;
    double m_pendingShotFinalWeight = 0;
    double m_pendingShotDoseWeight = 0;
    qint64 m_pendingShotEpoch = 0;
    QString m_pendingDebugLog;
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
    // Pure state machine behind selectedRecipeId + the deferred recipe-shot
    // start. MainController only wires it to Qt signals and the device; the
    // policy (lead/converge/rollback, arm/fire) lives in the header-only model
    // so it can be unit-tested without linking MainController.
    RecipeSelectionModel m_recipeSelection;
    // Apply the activation bundle on the main thread (recipeActivationReady).
    void applyActivatedRecipe(qint64 recipeId, const QVariantMap& recipe,
                              qint64 linkedBagId, const QVariantMap& linkedBag);
    // Stamp a tweak onto the active recipe row (no-op when none is active
    // or activation is applying).
    void stampActiveRecipe(const QString& field, const QVariant& value);
    // Recipes-first layout upgrade offer (recipes-idle-layout-upgrade):
    // cached result of the last checkRecipesUpgradeEligibility() background
    // pass, consumed by acceptRecipesFirstUpgrade().
    bool m_recipesUpgradeWillCreate = false;
    ShotRecord m_recipesUpgradeShotRecord;
    // Rebuild + stamp the active recipe's steam block from live settings.
    void stampActiveRecipeSteam();
    // Rebuild + stamp the active recipe's hot-water block from live settings
    // (selected water vessel). No-op unless a hot-water recipe is active.
    void stampActiveRecipeHotWater();
    // True when the active recipe's steam block declares a milk drink.
    // sendMachineSettings treats this like keepSteamHeaterOn — the steam
    // heater takes 5-9 minutes to warm, so a milk recipe holds it on for
    // as long as it is active and the machine is awake.
    bool activeRecipeHasMilk() const;
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
    DE1::Firmware::FirmwareAssetCache* m_firmwareAssetCache = nullptr;
    FirmwareUpdater* m_firmwareUpdater = nullptr;
    QTimer* m_firmwareCheckTimer = nullptr;   // weekly recurring check
    LocationProvider* m_locationProvider = nullptr;
    DataMigrationClient* m_dataMigration = nullptr;
    ShotReporter* m_shotReporter = nullptr;
    DatabaseBackupManager* m_backupManager = nullptr;
};
