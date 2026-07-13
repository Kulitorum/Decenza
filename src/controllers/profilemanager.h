#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QMap>
#include <QHash>
#include "../profile/profile.h"

class Settings;
class DE1Device;
class MachineState;
class ProfileStorage;

// Profile source enumeration (moved from maincontroller.h)
enum class ProfileSource {
    BuiltIn,      // Shipped with app in :/profiles/
    Downloaded,   // Downloaded from visualizer.coffee
    UserCreated   // Created or edited by user
};

// Profile metadata for filtering and display (moved from maincontroller.h)
struct ProfileInfo {
    QString filename;
    QString title;
    QString beverageType;
    QString editorType;   // "dflow", "aflow", "pressure", "flow", "advanced"
    ProfileSource source;
    bool hasKnowledgeBase = false;
    bool readOnly = false;  // From profile JSON read_only field or forced for BuiltIn source
    // Cached at catalog-scan time (the scan parses each profile's JSON
    // anyway) so list surfaces — e.g. the recipe wizard's profile tiles —
    // can show real metadata without a per-row file read. 0 = unstated.
    double espressoTemperature = 0;
    double targetWeight = 0;
};

/**
 * ProfileManager owns the profile lifecycle: catalog, load, save, edit,
 * and BLE upload coordination. Extracted from MainController to enable
 * isolated testing of profile/MCP functionality.
 *
 * Dependencies: Settings, DE1Device, MachineState, ProfileStorage
 * Does NOT depend on: MQTT, ShotServer, ShotHistory, Visualizer, Network
 */
class ProfileManager : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString currentProfileName READ currentProfileName NOTIFY currentProfileChanged)
    Q_PROPERTY(QString baseProfileName READ baseProfileName NOTIFY currentProfileChanged)
    Q_PROPERTY(bool profileModified READ isProfileModified NOTIFY profileModifiedChanged)
    Q_PROPERTY(double targetWeight READ targetWeight WRITE setTargetWeight NOTIFY targetWeightChanged)
    Q_PROPERTY(bool brewByRatioActive READ brewByRatioActive NOTIFY targetWeightChanged)
    Q_PROPERTY(double brewByRatioDose READ brewByRatioDose NOTIFY targetWeightChanged)
    Q_PROPERTY(double brewByRatio READ brewByRatio NOTIFY targetWeightChanged)
    Q_PROPERTY(QVariantList availableProfiles READ availableProfiles NOTIFY profilesChanged)
    Q_PROPERTY(QVariantList selectedProfiles READ selectedProfiles NOTIFY profilesChanged)
    Q_PROPERTY(QVariantList allBuiltInProfiles READ allBuiltInProfiles NOTIFY allBuiltInProfileListChanged)
    Q_PROPERTY(QVariantList cleaningProfiles READ cleaningProfiles NOTIFY profilesChanged)
    Q_PROPERTY(QVariantList downloadedProfiles READ downloadedProfiles NOTIFY profilesChanged)
    Q_PROPERTY(QVariantList userCreatedProfiles READ userCreatedProfiles NOTIFY profilesChanged)
    Q_PROPERTY(QVariantList allProfilesList READ allProfilesList NOTIFY profilesChanged)
    Q_PROPERTY(Profile* currentProfilePtr READ currentProfilePtr CONSTANT)
    Q_PROPERTY(bool isCurrentProfileRecipe READ isCurrentProfileRecipe NOTIFY currentProfileChanged)
    Q_PROPERTY(QString currentEditorType READ currentEditorType NOTIFY currentProfileChanged)
    Q_PROPERTY(double profileTargetTemperature READ profileTargetTemperature NOTIFY currentProfileChanged)
    Q_PROPERTY(double profileTargetWeight READ profileTargetWeight NOTIFY currentProfileChanged)
    Q_PROPERTY(QString currentProfileBeverageType READ currentProfileBeverageType NOTIFY currentProfileChanged)
    Q_PROPERTY(bool currentProfileIsMaintenance READ currentProfileIsMaintenance NOTIFY currentProfileChanged)
    // Set to true after kMaxUploadRetryAttempts consecutive profile uploads
    // have failed with retryable reasons. qml/main.qml watches this property
    // via a Connections handler (onDe1CommunicationFailureChanged) and calls
    // open()/close() on De1CommunicationErrorDialog. The dialog's OK button
    // calls acknowledgeDe1CommunicationFailure() to clear the flag.
    Q_PROPERTY(bool de1CommunicationFailure READ de1CommunicationFailure NOTIFY de1CommunicationFailureChanged)
    // Reactive view of the upload-retry backoff window. True while a failed
    // upload is queued to retry (m_profileUploadRetryAttempts > 0 AND
    // m_profileUploadRetryTimer.isActive()). QML binds a toast to this so the
    // user sees "Reconnecting…" during the otherwise-silent 15s backoff
    // window. Cleared on success, on exhaustion (when
    // de1CommunicationFailure supersedes), on disconnect, on user-initiated
    // profile switch, and on ack.
    Q_PROPERTY(bool profileUploadRetrying READ profileUploadRetrying NOTIFY profileUploadRetryingChanged)
    Q_PROPERTY(bool profileHasRecommendedDose READ profileHasRecommendedDose NOTIFY currentProfileChanged)
    Q_PROPERTY(double profileRecommendedDose READ profileRecommendedDose NOTIFY currentProfileChanged)
    Q_PROPERTY(bool isCurrentProfileReadOnly READ isCurrentProfileReadOnly NOTIFY currentProfileChanged)

public:
    explicit ProfileManager(Settings* settings, DE1Device* device,
                           MachineState* machineState,
                           ProfileStorage* profileStorage = nullptr,
                           QObject* parent = nullptr);

    // === Profile state ===
    QString currentProfileName() const;
    QString baseProfileName() const { return m_baseProfileName; }
    Q_INVOKABLE QString previousProfileName() const { return m_previousProfileName; }
    bool isProfileModified() const { return m_profileModified; }
    bool isCurrentProfileRecipe() const;
    QString currentEditorType() const;
    static bool isDFlowTitle(const QString& title);
    static bool isAFlowTitle(const QString& title);

    // === Profile accessors ===
    const Profile& currentProfile() const { return m_currentProfile; }
    Profile currentProfileObject() const { return m_currentProfile; }
    Profile* currentProfilePtr() { return &m_currentProfile; }
    double profileTargetTemperature() const { return m_currentProfile.espressoTemperature(); }
    double profileTargetWeight() const { return m_currentProfile.targetWeight(); }
    // Profile JSON beverage_type: "espresso" (default), "filter", "pourover",
    // "tea_portafilter"…, "cleaning"/"descale"/"calibrate". Trimmed + lowercased so an
    // odd-cased or whitespace-padded value (community-authored/imported profiles) still
    // matches the QML sentence's comparisons instead of silently falling through to the
    // generic "coffee" wording. Empty never escapes (default applies).
    QString currentProfileBeverageType() const {
        const QString t = m_currentProfile.beverageType().trimmed().toLower();
        return t.isEmpty() ? QStringLiteral("espresso") : t;
    }
    // QML-visible view of Profile::isMaintenanceBeverageType (the shared tier used
    // by maincontroller / visualizeruploader / mcptools_write) for the current profile.
    bool currentProfileIsMaintenance() const {
        return Profile::isMaintenanceBeverageType(m_currentProfile.beverageType());
    }
    bool profileHasRecommendedDose() const { return m_currentProfile.hasRecommendedDose(); }
    double profileRecommendedDose() const { return m_currentProfile.recommendedDose(); }

    // === Target weight / brew-by-ratio ===
    double targetWeight() const;
    void setTargetWeight(double weight);
    bool brewByRatioActive() const;
    double brewByRatioDose() const;
    double brewByRatio() const;
    Q_INVOKABLE void activateBrewWithOverrides(double dose, double yield, double temperature, const QString& grind);
    Q_INVOKABLE void clearBrewOverrides();

    // === Profile catalog ===
    QVariantList availableProfiles() const;
    QVariantList selectedProfiles() const;
    QVariantList allBuiltInProfiles() const;
    QVariantList cleaningProfiles() const;
    QVariantList downloadedProfiles() const;
    QVariantList userCreatedProfiles() const;
    QVariantList allProfilesList() const;
    const QList<ProfileInfo>& allProfiles() const { return m_allProfiles; }

    // === Profile CRUD ===
    Q_INVOKABLE QVariantMap getCurrentProfile() const;
    Q_INVOKABLE void markProfileClean();
    Q_INVOKABLE QString titleToFilename(const QString& title) const;
    Q_INVOKABLE QString findProfileByTitle(const QString& title) const;
    Q_INVOKABLE bool profileExists(const QString& filename) const;
    Q_INVOKABLE bool isProfileInSelectedList(const QString& filename) const;
    Q_INVOKABLE void loadAutoLoadProfileIfNeeded();
    Q_INVOKABLE QString profileKnowledgeContent(const QString& profileTitle) const;
    Q_INVOKABLE bool deleteProfile(const QString& filename);
    Q_INVOKABLE QVariantMap getProfileByFilename(const QString& filename) const;

    // Recipe-wizard tea helpers (add-recipe-wizard-tea): QML-visible views of
    // the DrinkTypes header (src/core/drinktypes.h — the single source for
    // the keyword table and per-type default temps).
    Q_INVOKABLE bool teaProfileMatchesType(const QString& profileTitle, const QString& teaType) const;
    Q_INVOKABLE double defaultTeaTempC(const QString& teaType) const;
    // Does the profile-knowledge base state this profile shines with the
    // given roast level? (KB roastAffinity, resolved through the same
    // title/alias matching as the advisor's KB lookups.) The bag's roast
    // level is normalized ("Medium-Light" → "medium-light"); a localized
    // roast string simply never matches — graceful degradation, the tier
    // just loses its KB chips. Drives the wizard's recommended tier.
    Q_INVOKABLE bool kbProfileSuitsRoast(const QString& profileTitle, const QString& roastLevel) const;
    // Relative grind direction between two profiles per the KB's UGS ordering
    // ("finer"/"coarser"/"same"; "" when either UGS is unknown). Direction
    // only — never a click count (the KB's own cross-profile rule).
    Q_INVOKABLE QString grindDirectionBetween(const QString& sourceProfileTitle,
                                              const QString& targetProfileTitle) const;
    // Installed-catalog lookup: profile title → normalized beverage_type
    // ("" when the title isn't installed). MCP/web recipe surfaces resolve
    // drink-type derivation through this — recipes referencing INSTALLED
    // profiles carry no embedded profile JSON, so without the catalog a tea
    // profile would derive as espresso. The snapshot variant is for
    // background-thread closures (recipe list/get JSON): capture on the main
    // thread, pass by value — ProfileManager itself is main-thread-only.
    QString beverageTypeForTitle(const QString& profileTitle) const;
    QHash<QString, QString> beverageTypeByTitleSnapshot() const;

    // === Read-only protection ===
    Q_INVOKABLE bool isCurrentProfileReadOnly() const;
    Q_INVOKABLE bool isBuiltInFilename(const QString& filename) const;
    Q_INVOKABLE bool resetProfileToDefault(const QString& filename);

    // === Profile editing ===
    Q_INVOKABLE void uploadRecipeProfile(const QVariantMap& recipeParams);
    Q_INVOKABLE QVariantMap getOrConvertRecipeParams();
    Q_INVOKABLE void createNewRecipe(const QString& title = "New Recipe");
    Q_INVOKABLE void createNewAFlowRecipe(const QString& title = "New A-Flow Recipe");
    Q_INVOKABLE void createNewPressureProfile(const QString& title = "New Pressure Profile");
    Q_INVOKABLE void createNewFlowProfile(const QString& title = "New Flow Profile");
    Q_INVOKABLE void convertCurrentProfileToAdvanced();
    Q_INVOKABLE void createNewProfile(const QString& title = "New Profile");

    // === Frame operations (advanced editor) ===
    Q_INVOKABLE void addFrame(int afterIndex = -1);
    Q_INVOKABLE void deleteFrame(int index);
    Q_INVOKABLE void moveFrameUp(int index);
    Q_INVOKABLE void moveFrameDown(int index);
    Q_INVOKABLE void duplicateFrame(int index);
    Q_INVOKABLE void setFrameProperty(int index, const QString& property, const QVariant& value);
    Q_INVOKABLE QVariantMap getFrameAt(int index) const;
    Q_INVOKABLE int frameCount() const;

    // === Flow calibration ===
    void applyFlowCalibration();

public slots:
    void loadProfile(const QString& profileName);
    Q_INVOKABLE bool loadProfileFromJson(const QString& jsonContent);
    bool persistCurrentProfile();  // Save to downloaded folder if not already installed (no re-upload)
    void refreshProfiles();
    Q_INVOKABLE void uploadCurrentProfile();
    Q_INVOKABLE void uploadProfile(const QVariantMap& profileData);
    Q_INVOKABLE bool saveProfile(const QString& filename);
    Q_INVOKABLE bool saveProfileAs(const QString& filename, const QString& title);

    // Bake a new brew temperature into the current profile: every frame is shifted
    // by the delta from the profile's reference temperature (espressoTemperature),
    // the scalar is updated, and the profile is uploaded and saved. Same anchor as
    // the live-brew override path (uploadCurrentProfile) so save and brew agree.
    Q_INVOKABLE void applyTemperatureToProfile(double newTemperature);

    // Adaptive temperature string for the shot-plan widget / Brew Settings dialog.
    // anchorTemp is the reference the delta tag is measured from (the profile's
    // espressoTemperature normally; the active recipe's own temp in recipe mode).
    // When hasOverride, a signed delta tag (overrideTemp - anchorTemp) is appended.
    // baselineShiftC shifts the shown frame temps to a non-profile baseline: when a
    // recipe is active its own temps are the baseline, so pass the recipe temp as
    // anchorTemp AND (recipeTemp - espressoTemperature) as baselineShiftC to render
    // the recipe's actual temps (e.g. "81 · 91°C") with no profile-relative delta.
    Q_INVOKABLE QString temperatureDisplay(double anchorTemp, bool hasOverride,
                                           double overrideTemp,
                                           double baselineShiftC = 0.0) const;
    // Same adaptive string, but for an EXPLICIT frame-temperature list instead
    // of the currently loaded profile's frames (recipe-relative-temp-offset):
    // recipe cards render THEIR OWN profile's temps, which are unrelated to
    // whatever profile the machine holds. stepTempsC is a plain number list
    // (QML array); an empty list falls back to anchorTemp alone.
    Q_INVOKABLE QString temperatureDisplayForSteps(const QVariantList& stepTempsC,
                                                   double anchorTemp, bool hasOverride,
                                                   double overrideTemp,
                                                   double baselineShiftC = 0.0) const;
    Q_INVOKABLE bool duplicateProfile(const QString& sourceFilename, const QString& newTitle);
    // Rename in place: changes only the profile's display title, keeping the same
    // filename (so favorites/auto-load/selected references stay valid). Built-in
    // profiles are read-only resources and cannot be renamed — use Copy instead.
    Q_INVOKABLE bool renameProfile(const QString& filename, const QString& newTitle);

    // Communication-failure dialog support.
    bool de1CommunicationFailure() const { return m_de1CommunicationFailure; }
    Q_INVOKABLE void acknowledgeDe1CommunicationFailure();

    // See Q_PROPERTY documentation above.
    bool profileUploadRetrying() const { return m_profileUploadRetrying; }

signals:
    void currentProfileChanged();
    void profileModifiedChanged();
    void targetWeightChanged();
    void profilesChanged();
    void allBuiltInProfileListChanged();

    // Emitted when uploadCurrentProfile() is blocked during active phase.
    // Connect to ShotDebugLogger for diagnostics.
    void profileUploadBlocked(const QString& phaseString, const QString& stackTrace);

    // Emitted when loadProfile() cannot find the requested profile file.
    // The UI should show an error and prompt the user to select another profile.
    void profileLoadFailed(const QString& filename);

    // See Q_PROPERTY documentation above.
    void de1CommunicationFailureChanged();
    void profileUploadRetryingChanged();

    // Emitted when an in-progress espresso shot is aborted because a profile
    // upload just failed with a retryable reason. The DE1 was running stale
    // frames; the UI should surface a toast/warning. Mirrors the
    // MainController::shotAbortedNoScale pattern.
    void shotAbortedProfileUploadRetrying();

    // Emitted when loadAutoLoadProfileIfNeeded() finds the configured filename
    // no longer resolves to a Selected-list profile. The setting is cleared as
    // part of the same call; QML listens to surface a toast.
    //
    // Not emitted on eager-clear paths (Settings::addHiddenProfile /
    // removeSelectedBuiltInProfile / ProfileManager::deleteProfile) — those
    // clear the filename directly while the user is already on a UI that
    // makes the change obvious, so no toast is warranted.
    void autoLoadStaleCleared();

private:
    // Current profile's frames with every temperature shifted so the reference
    // temperature (espressoTemperature) becomes targetTemp. Single source of truth
    // for the override delta, shared by the live-brew and save-to-profile paths.
    QList<ProfileFrame> framesShiftedToTemperature(double targetTemp) const;

    void loadDefaultProfile();
    // Reset brew overrides for a freshly loaded profile. After startup this is
    // a genuine clear (flags go false — an override is relative to the profile
    // it was dialed against). During startup, persisted overrides survive
    // (brew-overrides spec) unless they match the incoming profile's own
    // defaults: pre-fix sessions latched a same-as-default "override" on every
    // load, so a matching persisted value is noise, not intent.
    void resetBrewOverridesForLoadedProfile();
    void migrateProfileFolders();
    void migrateProfileFormat();
    void migrateRecipeFrames();
    void migrateReadOnlyProfiles();
    void applyRecipeToScalarFields(const RecipeParams& recipe);
    void createNewProfileWithEditorType(EditorType type, const QString& title);
    QString profilesPath() const;
    QString userProfilesPath() const;
    QString downloadedProfilesPath() const;
    double getGroupTemperature() const;

    Settings* m_settings = nullptr;
    DE1Device* m_device = nullptr;
    MachineState* m_machineState = nullptr;
    ProfileStorage* m_profileStorage = nullptr;

    Profile m_currentProfile;
    QStringList m_availableProfiles;
    QMap<QString, QString> m_profileTitles;      // filename -> display title
    QMap<QString, QString> m_profileJsonCache;   // populated by refreshProfiles, consumed by loadProfile
    QList<ProfileInfo> m_allProfiles;
    QString m_baseProfileName;
    QString m_previousProfileName;
    bool m_profileModified = false;
    bool m_profileUploadPending = false;
    bool m_uploadInFlight = false;        // True while a profile upload is in progress at DE1Device
    bool m_uploadPendingAfterInFlight = false;  // True if a newer profile change arrived mid-upload
    bool m_startupLoadDone = false;

    // Auto-retry state for failed profile uploads. A failure with a retryable
    // reason (frame sequence mismatch, ACK timeout) arms
    // m_profileUploadRetryTimer with exponential backoff, capped per the
    // constants in profilemanager.cpp. On success, disconnect, or
    // supersede/queue-clear, the counter resets. After
    // kMaxUploadRetryAttempts consecutive failures, m_de1CommunicationFailure
    // flips to true so the UI can surface a "power-cycle the DE1" dialog.
    QTimer m_profileUploadRetryTimer;
    int m_profileUploadRetryAttempts = 0;
    QString m_lastUploadFailureReason;
    bool m_de1CommunicationFailure = false;
    // Cached value of (m_profileUploadRetryAttempts > 0 &&
    // m_profileUploadRetryTimer.isActive()). Updated via
    // updateProfileUploadRetrying() from every state-mutation site so the
    // NOTIFY signal fires exactly when the value changes.
    bool m_profileUploadRetrying = false;
    void updateProfileUploadRetrying();

#ifdef DECENZA_TESTING
    friend class tst_ProfileManager;
    friend class tst_McpToolsProfiles;
    friend class tst_McpToolsWrite;
#endif
};
