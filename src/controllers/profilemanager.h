#pragma once

#include <QObject>
#include <QVariantList>
#include <QMap>
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
};

/**
 * ProfileManager owns the profile lifecycle: catalog, load, save, edit,
 * and BLE upload coordination. Extracted from MainController to enable
 * isolated testing of profile/MCP functionality.
 *
 * Dependencies: Settings, DE1Device, MachineState, ProfileStorage
 * Does NOT depend on: MQTT, ShotServer, ShotHistory, Visualizer, AI, Network
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
    Q_INVOKABLE bool deleteProfile(const QString& filename);
    Q_INVOKABLE QVariantMap getProfileByFilename(const QString& filename) const;

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
    void refreshProfiles();
    Q_INVOKABLE void uploadCurrentProfile();
    Q_INVOKABLE void uploadProfile(const QVariantMap& profileData);
    Q_INVOKABLE bool saveProfile(const QString& filename);
    Q_INVOKABLE bool saveProfileAs(const QString& filename, const QString& title);

signals:
    void currentProfileChanged();
    void profileModifiedChanged();
    void targetWeightChanged();
    void profilesChanged();
    void allBuiltInProfileListChanged();

    // Emitted when uploadCurrentProfile() is blocked during active phase.
    // Connect to ShotDebugLogger for diagnostics.
    void profileUploadBlocked(const QString& phaseString, const QString& stackTrace);

private:
    void loadDefaultProfile();
    void updateProfileKnowledgeBaseId();
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
    bool m_startupLoadDone = false;

#ifdef DECENZA_TESTING
    friend class tst_ProfileManager;
    friend class tst_McpToolsProfiles;
    friend class tst_McpToolsWrite;
#endif
};
