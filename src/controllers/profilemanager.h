#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QMap>
#include <QHash>
#include <QSet>
#include <QtQml/qqmlregistration.h>
#include "../profile/profile.h"

class Settings;
class DE1Device;
class MachineState;
class ProfileStorage;
class SteamHeaterPolicy;
class QQmlEngine;
class QJSEngine;

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
    // When the profile reached its KB entry by SHAPE rather than by title
    // (change: resolve-profile-kb-by-shape), the canonical display name of the
    // entry it matched — so a surface can say "Based on Londinium" rather than
    // silently applying someone else's knowledge. Empty when the profile
    // resolved by title (nothing to explain: its own name said so), when it
    // resolved to nothing, and when the shape matched SEVERAL entries at once
    // — there is no single profile to name then, and the analysis facts still
    // transfer without the identity claim.
    QString kbDerivedFrom;
    // Every candidate the resolution produced — one id for a title match or a
    // unique shape match, SEVERAL when the shape matched more than one
    // documented profile.
    //
    // The set is what the analysis actually consumes: an ambiguous match still
    // transfers suppression flags by union and agreed facts by unanimity, so a
    // shot's badges and summary ARE shaped by KB knowledge in that case. The
    // indicator is therefore driven by this, not by kbId — a user is entitled
    // to know what the badges are based on even when we cannot name a single
    // source. What the ambiguity withholds is the IDENTITY claim: no "Based
    // on X", because there is no one X.
    QStringList kbIds;
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

    // Compile-time QML registration, replacing the setContextProperty("ProfileManager", …) that
    // main.cpp used to do. A context property is invisible to qmllint, qmlcachegen and the
    // language server, so every `ProfileManager.x` in QML was unchecked. Full rationale in
    // src/controllers/maincontroller.h.
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString currentProfileName READ currentProfileName NOTIFY currentProfileChanged)
    // The UNDECORATED title — what `shots.profile_name` is written from
    // (ShotHistoryStorage::saveShot: `profile->title()`). currentProfileName
    // above is a DISPLAY string and prefixes "*" (or appends " (modified)" for a
    // read-only profile) once the profile is edited, so it must never be used as
    // a query term: matching it against stored shots returns nothing the moment
    // the user nudges a dose. Use this for lookups, that one for labels.
    Q_PROPERTY(QString currentProfileTitle READ currentProfileTitle NOTIFY currentProfileChanged)
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

    // Every installed profile TITLE, for QML to test whether a recipe's stored
    // profileTitle still resolves.
    //
    // A PROPERTY, not a Q_INVOKABLE, and that is the whole point: a binding
    // re-evaluates when a NOTIFY fires for a property it READ. Calling
    // findProfileByTitle() from a binding records no dependency, so a recipe
    // card would keep showing a deleted profile as fine until the page was
    // rebuilt — the exact freeze translate() had before it became a property
    // (see TranslationManager::translate for the worked case). Reading this
    // property makes the binding depend on profilesChanged, so deleting or
    // importing a profile updates every card already on screen.
    Q_PROPERTY(QStringList installedProfileTitles READ installedProfileTitles NOTIFY profilesChanged)
    // No Q_PROPERTY for currentProfilePtr. `Profile` is a plain C++ class — no Q_OBJECT, no
    // Q_GADGET — so QML could never have read a member through the pointer; the property
    // resolved to an opaque handle and nothing in qml/ ever referenced it (verified by grep,
    // and tst_profilemanager already lists the name among the identifiers that must NOT appear
    // as MainController.x). Registering this class turns that dead property into an
    // `unresolved-type` diagnostic, and giving Profile a Q_GADGET to satisfy it would mean
    // annotating ~50 accessors to expose something no caller wants. The accessor below stays:
    // it is used from maincontroller.cpp, in C++ only.
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
    // QML_SINGLETON hooks. The engine does not create this object: MainController owns it and
    // main.cpp publishes the pointer before QQmlEngine::load(). See maincontroller.h.
    static void setQmlInstance(ProfileManager *instance);
    static ProfileManager *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    // `steamHeaterPolicy` is THE steam-target derivation (see steamheaterpolicy.h).
    // Passing it is how uploadCurrentProfile() avoids re-deriving the steam
    // temperature itself, which it used to do from an incomplete set of inputs.
    //
    // Not defaulted, and no fallback instance: a privately-built policy has no
    // recipe-intent provider and no event permission, so it would resolve a
    // DIFFERENT answer — a second oracle inside the change whose whole purpose
    // is that there is only one. Null is honest instead: uploadCurrentProfile()
    // then leaves the steam field alone rather than inventing a value.
    explicit ProfileManager(Settings* settings, DE1Device* device,
                           MachineState* machineState,
                           ProfileStorage* profileStorage = nullptr,
                           SteamHeaterPolicy* steamHeaterPolicy = nullptr,
                           QObject* parent = nullptr);

    // === Profile state ===
    QString currentProfileName() const;
    QString currentProfileTitle() const { return m_currentProfile.title(); }
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

    // Sets the per-profile dose and enables it in one step, for callers that have a
    // value rather than a value plus a toggle — the MCP `dose` parameter and the Dose
    // control on both recipe editors. Setting a dose without enabling it would store a
    // number nothing reads, which is what the retired recipe-block `dose` did.
    //
    // Q_INVOKABLE because RecipeEditorPage and SimpleProfileEditorPage call it
    // directly: their `updateRecipe(key, value)` idiom routes through RecipeParams,
    // which no longer carries a dose, so the slider has to write the profile field.
    // Passing 0 CLEARS the recommendation rather than recommending zero grams.
    Q_INVOKABLE void setCurrentProfileRecommendedDose(double doseG);

    // === Target weight / brew-by-ratio ===
    // The yield ladder's single evaluation point (add-yield-ratio-anchor):
    // resolves the session anchor {value, mode} against the effective dose,
    // falling back to the profile's target_weight. Always returns plain
    // grams — a ratio never crosses MachineState.
    double targetWeight() const;
    void setTargetWeight(double weight);
    // True iff the session anchor's mode is "ratio" — read from the stored
    // mode, never inferred by comparing grams against the profile target.
    bool brewByRatioActive() const;
    // The canonical effective dose for ratio math and display: the latched
    // dose during a shot, else the live dyeBeanWeight. 0 = no dose known
    // (callers render a bare ratio and resolution falls back to the profile).
    double brewByRatioDose() const;
    double brewByRatio() const;
    // Arm the session overrides from Brew Settings OK. The yield arrives as a
    // spec: value + mode ("none" | "absolute" | "ratio"). The legacy 4-arg
    // form (MCP machine_start_espresso) anchors an absolute.
    // `rpm` < 0 leaves the live RPM untouched (the common case); >= 0 sets it
    // (variable-RPM grinders). RPM is independent of the grind setting.
    Q_INVOKABLE void activateBrewWithOverrides(double dose, double yieldValue,
                                               const QString& yieldMode,
                                               double temperature, const QString& grind,
                                               int rpm = -1);
    Q_INVOKABLE void activateBrewWithOverrides(double dose, double yield, double temperature, const QString& grind);
    Q_INVOKABLE void clearBrewOverrides();

    // Arm (or clear) ONLY the brew-temperature override and push it to the machine.
    // The temperature-only slice of activateBrewWithOverrides: set-or-clear the
    // override against the profile baseline (tapping the profile's own temperature
    // disarms rather than pins a redundant override), then re-upload. The
    // brew-bar Temp Quick-Select widget uses this so a picked value actually
    // reaches the DE1 — a bare Settings.brew.temperatureOverride write only takes
    // effect on the next uploadCurrentProfile().
    Q_INVOKABLE void applyTemperatureOverride(double temperatureC);

    // Shot latch (add-yield-ratio-anchor Decision 9): the resolved target AND
    // the dose are frozen at espressoCycleStarted and released at shot end,
    // so NOTHING — a dose write, a bean switch, a recipe activation, an
    // MCP/web anchor write, a profile load — can move the live SAW target
    // mid-shot. Latching the dose alone was not enough: every other input to
    // the resolution stayed live (see targetWeight()). Event-driven (called
    // from main.cpp's cycle handlers), never a timer.
    void latchForShot();
    void releaseShotLatch();

    // The snapshot the last shot STARTED with — the resolved grams that
    // actually ran plus the anchor that produced them. Deliberately survives
    // releaseShotLatch(): the shot-save path runs AFTER settling (well after
    // the latch releases) and must record what ran, not re-read a session
    // that may have drifted mid-shot (a dose capture while the cup fills, a
    // bean switch). Valid once any shot has started this session.
    bool hasShotSnapshot() const { return m_shotSnapshotValid; }
    double latchedTargetG() const { return m_latchedTargetG; }
    QString latchedYieldMode() const { return m_latchedYieldMode; }
    double latchedYieldAnchorValue() const { return m_latchedYieldAnchorValue; }
    // The effective flow calibration multiplier the shot was PULLED at.
    // Latched rather than read at save time because
    // MainController::computeAutoFlowCalibration() runs at shot end BEFORE
    // the save (maincontroller.cpp:4078 vs :4261) and can write a new
    // per-profile multiplier first — a save-time read would record a value
    // the shot never ran under, and only on the shots where it changed.
    // 0.0 means "not recorded" (no settings object at latch time, an
    // imported shot, or a fake/dev shot); 1.0 is a real multiplier and must
    // never stand in for unknown.
    double latchedFlowCalibration() const { return m_latchedFlowCalibration; }

    // === Profile catalog ===
    QVariantList availableProfiles() const;
    QVariantList selectedProfiles() const;
    QVariantList allBuiltInProfiles() const;
    QVariantList cleaningProfiles() const;
    QVariantList downloadedProfiles() const;
    QVariantList userCreatedProfiles() const;
    QVariantList allProfilesList() const;

    // Exact titles as stored in the catalog — the same strings
    // findProfileByTitle() compares against, so a QML membership test agrees
    // with what activation will actually resolve.
    QStringList installedProfileTitles() const {
        QStringList titles;
        titles.reserve(m_allProfiles.size());
        for (const ProfileInfo& info : m_allProfiles)
            titles.append(info.title);
        return titles;
    }
    const QList<ProfileInfo>& allProfiles() const { return m_allProfiles; }

    // === Profile CRUD ===
    Q_INVOKABLE QVariantMap getCurrentProfile() const;
    Q_INVOKABLE void markProfileClean();
    Q_INVOKABLE QString titleToFilename(const QString& title) const;
    Q_INVOKABLE QString findProfileByTitle(const QString& title) const;
    // Installed-catalog metadata for a profile title, for read-only display
    // surfaces (e.g. the recipe wizard's Profile summary card) that want the
    // scan-time metadata without a per-call file read. Returns an empty map
    // when the title isn't installed. Keys: filename, title, editorType
    // ("dflow"/"aflow"/"pressure"/"flow"/"advanced"), beverageType,
    // hasKnowledgeBase (bool), espressoTemperatureC, targetWeightG.
    Q_INVOKABLE QVariantMap profileCatalogInfoForTitle(const QString& title) const;
    Q_INVOKABLE bool profileExists(const QString& filename) const;
    Q_INVOKABLE bool isProfileInSelectedList(const QString& filename) const;
    Q_INVOKABLE void loadAutoLoadProfileIfNeeded();
    Q_INVOKABLE QString profileKnowledgeContent(const QString& profileTitle) const;

    // Whether the sparkle indicator should light for this profile.
    //
    // Asks the CONTENT path rather than resolving separately, so the indicator
    // and the dialog it opens cannot disagree — a lit sparkle that opens an
    // empty dialog is the exact failure this shares one function to prevent.
    // The shot pages previously gated on the shot's persisted profileKbId,
    // which is a different question: that column is empty for pre-change rows
    // and for any profile whose shape matched several entries, and it says
    // nothing about whether the profile is still in the catalog.
    Q_INVOKABLE bool profileHasKnowledge(const QString& profileTitle) const;

    // Canonical display names of the KB entries this profile matched, but ONLY
    // when it matched more than one. Empty for a single match (the dialog
    // titles itself with the profile) and for no match.
    //
    // Exists so the dialog can say which profiles it is showing without the
    // C++ side inventing an English sentence: the KB prose is untranslated
    // English by nature, but the explanatory line around it is UI text and
    // belongs in QML where TranslationManager can reach it.
    Q_INVOKABLE QStringList profileKbCandidateNames(const QString& profileTitle) const;

    // Canonical name of the KB entry this profile reached by SHAPE, or empty.
    //
    // Empty covers three distinct cases, all of which mean "show nothing":
    // the profile resolved by title (its own name already says where it came
    // from), it resolved to nothing, or its shape matched several entries and
    // no single identity was established. Surfaces render this as a derivation
    // ("Based on X") because it is an inference from the profile's structure,
    // not a claim its title made.
    //
    // Takes a title because that is what a shot record stores. A profile the
    // user has since deleted is not in the catalog and returns empty.
    Q_INVOKABLE QString profileKbDerivedFrom(const QString& profileTitle) const;

    // The dial-in differences between a profile and the bundled profile whose
    // knowledge is being shown for it (change: summarize-profile-changes-from-builtin).
    //
    // Keys: hasBase (bool), unchanged (bool), rows (QVariantList), and — only
    // when hasBase is true — baseTitle and baseKbId. Each row carries
    // { kind, unit, frameIndex, frameName, numeric, oldValue, newValue,
    // oldText, newText }; `unit` is the token the surface turns into a suffix
    // and is NOT inferable downstream (the frame limiter is a max flow on a
    // pressure frame and a max pressure on a flow frame).
    //
    // Three outcomes the surface must tell apart, and they are NOT the same:
    //   hasBase false            - no comparison was possible; show nothing.
    //   hasBase true, unchanged  - a copy of the bundled profile with nothing
    //                              dialled differently; say so, because it
    //                              means the knowledge applies unqualified.
    //   hasBase true, rows       - the differences.
    //
    // No user-visible text here. `kind` is a stable identifier QML turns into a
    // translated label; values are raw, so QML can apply the user's temperature
    // unit through the display helper the rest of the app uses.
    Q_INVOKABLE QVariantMap profileDialInDiff(const QString& profileTitle) const;

    // Same, for a profile stored with a SHOT rather than one in the catalog.
    //
    // A separate entry point rather than a flag because the source genuinely
    // differs: a shot must be compared against the profile it was pulled with,
    // not against whatever that catalog entry has been edited into since.
    // Unparseable JSON yields hasBase false, the same as any other profile that
    // cannot be compared.
    Q_INVOKABLE QVariantMap profileDialInDiffForJson(const QString& profileJson) const;
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
    // Installed-catalog lookup: profile title (trimmed, lower-cased) → base
    // espresso_temperature (°C). Read-only web/MCP recipe surfaces fold this
    // with the recipe's stored offset to show the RESULTING brew temperature
    // (a recipe carries only the offset). Same snapshot-on-main-thread contract
    // as beverageTypeByTitleSnapshot for background-thread closures.
    QHash<QString, double> espressoTempByTitleSnapshot() const;

    // Installed titles, EXACT — not trimmed or lower-cased like the two
    // snapshots above. Those are lookups where a near-match is better than
    // nothing; this one answers "will this recipe activate", and activation
    // resolves through findProfileByTitle, which compares exactly. Reusing a
    // case-folded key here would let the web call a recipe fine that the app
    // marks as missing its profile — the surface drift CLAUDE.md warns about.
    // Same snapshot-on-main-thread contract as the two above.
    QSet<QString> installedTitlesSnapshot() const {
        QSet<QString> titles;
        titles.reserve(m_allProfiles.size());
        for (const ProfileInfo& info : m_allProfiles)
            titles.insert(info.title);
        return titles;
    }

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
    // Returns whether the REQUESTED profile became the active one. False when it
    // was refused as unreadable (the previously active profile is kept), and when
    // no profile of that name was found (the default is loaded instead) — in both
    // cases the machine is not brewing what the caller asked for.
    //
    // Deliberately NOT [[nodiscard]]. Being a slot, it produces no diagnostic at
    // QML call sites at all; it would force `(void)` at six C++ sites, none of
    // which wants the value. The value exists for MCP, which has no screen.
    //
    // Only the refusal half is actually shown to a user: profileRefusedUnreadable
    // drives ProfileRefusedDialog.qml. The not-found half emits profileLoadFailed,
    // which as of this writing has ZERO handlers — nothing reaches the screen and
    // the machine silently switches to default. That is a real gap, recorded here
    // rather than fixed, because it is a UI change and this is not a UI change.
    bool loadProfile(const QString& profileName);
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

    // Emitted when a profile was really deleted, carrying its TITLE — recipes
    // and shots reference profiles by title, not by filename, and the filename
    // is all deleteProfile() is given.
    //
    // Deletion is the one lifecycle event that changes what a title resolves to
    // without changing what is loaded, so currentProfileChanged does not fire
    // and nothing downstream would otherwise notice. ProfileManager does not
    // know what references a profile; it reports the fact and lets owners
    // decide (MainController deactivates a recipe pinned to it).
    //
    // NOT emitted when deleteProfile() merely cleaned up a local override of a
    // built-in profile: the title still resolves afterwards, to the built-in
    // version, so nothing pointing at it has broken.
    void profileDeleted(const QString& title);

    // Emitted when loadProfile() found the file but REFUSED it: this build
    // cannot promise the profile brews what it describes, so activating it would
    // pour a different shot silently. The previously active profile stays
    // active — nothing is switched — and main.qml opens ProfileRefusedDialog.
    //
    // The two key lists are passed raw rather than pre-formatted because
    // Profile::validationErrors() is untranslated English; QML composes the
    // user-facing text so it follows the app language. Either list may be empty
    // (a profile can also be refused for having no steps at all, or more than
    // MAX_FRAMES), so the dialog must handle "refused with no keys to name".
    void profileRefusedUnreadable(const QString& filename, const QString& title,
                                  const QStringList& unsupportedStepKeys,
                                  const QStringList& malformedValues);

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
    // Catalog lookup by title for the four KB surfaces. Returns nullptr when
    // no profile has that title, and also when two do and disagree about their
    // KB resolution — see the definition for why picking one is wrong.
    const ProfileInfo* findProfileByTitleForKb(const QString& profileTitle) const;

    // The non-consuming half of the four-step profile lookup: SAF storage, user
    // folder, downloaded folder, `:/profiles`. Returns an invalid Profile when
    // the filename is in none of them.
    //
    // loadProfile() deliberately does NOT use this and keeps its own copy of the
    // walk: its step 1 reads m_profileJsonCache with take(), which CONSUMES the
    // startup cache entry, and it tracks an Origin the callers here have no use
    // for. A shared helper would either consume that cache on a read-only query
    // or need a flag that makes it two functions wearing one name.
    Profile loadProfileByFilename(const QString& filename, bool* found = nullptr) const;

    // Shared body of the two profileDialInDiff* invokables.
    static QVariantMap dialInDiffFor(const Profile& p);

    static ProfileManager *s_qmlInstance;

    // Current profile's frames with every temperature shifted so the reference
    // temperature (espressoTemperature) becomes targetTemp. Single source of truth
    // for the override delta, shared by the live-brew and save-to-profile paths.
    QList<ProfileFrame> framesShiftedToTemperature(double targetTemp) const;

    void loadDefaultProfile();

    // Rewrite a just-loaded profile in the canonical encoding, but only where that
    // is provably lossless. Runs on load rather than as a one-time migration: the
    // user can drop a profile into the folder at any time, so a pass that completes
    // would miss every later arrival. `filePath` empty means the ProfileStorage
    // (SAF) tier, which is written through ProfileStorage rather than QFile.
    // Built-in profiles are never passed here — `:/profiles/` is a read-only resource.
    void upgradeStoredEncoding(const QString& resolvedName,
                               const QString& filePath,
                               const Profile& loaded);

    // Reset brew overrides for a freshly loaded profile. After startup this is
    // a genuine clear (flags go false — an override is relative to the profile
    // it was dialed against). During startup, persisted overrides survive
    // (brew-overrides spec) unless they match the incoming profile's own
    // defaults: pre-fix sessions latched a same-as-default "override" on every
    // load, so a matching persisted value is noise, not intent.
    void resetBrewOverridesForLoadedProfile();
    // Apply the loaded profile's recommended dose to the live dose — but only
    // when the dose ladder names the profile as the owner, i.e. no active
    // recipe or bag supplies one (dose-source-precedence). Also a no-op during
    // the startup load, where the ladder cannot yet be answered and the live
    // dose is already persisted.
    void applyRecommendedDoseIfProfileOwnsIt();
    void migrateProfileFolders();
    void migrateProfileFormat();
    // One-time upgrade: remove the `recipe` block from already-saved profiles,
    // promoting a set dose to recommended_dose. Replaces migrateRecipeFrames().
    void stripStoredRecipeBlocks();

    enum class WriteBack { Written, Refused, Failed, NotWritable };

    // Rewrites a profile over whichever stored copy it came from, but ONLY when
    // re-serializing it loses nothing.
    //
    // The point is that it reads the "before" bytes from the SAME place the write
    // will land. Splitting those two decisions is what let a storage-tier write go
    // unaudited in two separate places here; keeping them in one function makes that
    // divergence impossible rather than merely fixed.
    //
    // An unreadable "before" is a REFUSAL, not a clean audit — otherwise the read
    // failure silently disables the check it was supposed to gate.
    //
    // `excludedKey` is dropped from both sides for a repair whose whole purpose is to
    // change one key. Refusal detail lands in *parityOut so each caller keeps its own
    // wording and log level.
    WriteBack writeProfileBackIfLossless(const QString& resolvedName, const QString& filePath,
                                         bool preferStorage, const Profile& profile,
                                         const QString& excludedKey, QStringList* parityOut);
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
    SteamHeaterPolicy* m_steamHeaterPolicy = nullptr;
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

    // Shot latch state (add-yield-ratio-anchor Decision 9). While latched,
    // targetWeight() answers with m_latchedTargetG (the resolved grams the
    // shot started with) and brewByRatioDose() with m_latchedDoseG, so the
    // running shot's target is immune to every late write.
    bool m_shotLatched = false;
    double m_latchedDoseG = 0.0;
    double m_latchedTargetG = 0.0;
    // The anchor that produced m_latchedTargetG. Snapshot alongside it so the
    // shot record's intent and outcome can never disagree — see
    // hasShotSnapshot(). m_shotSnapshotValid is set on the first latch and
    // never cleared; m_shotLatched is the freeze flag and clears at shot end.
    bool m_shotSnapshotValid = false;
    QString m_latchedYieldMode = QStringLiteral("none");
    double m_latchedYieldAnchorValue = 0.0;
    // See latchedFlowCalibration(). 0.0 = not recorded, never "1.0".
    double m_latchedFlowCalibration = 0.0;

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
