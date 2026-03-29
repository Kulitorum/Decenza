#include "profilemanager.h"
#include "../core/settings.h"
#include "../core/profilestorage.h"
#include "../ble/de1device.h"
#include "../ble/protocol/de1characteristics.h"
#include "../machine/machinestate.h"
#include "../profile/recipegenerator.h"
#include "../profile/recipeanalyzer.h"
#include "../profile/profilesavehelper.h"
#include "../ai/shotsummarizer.h"
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QCoreApplication>
#include <QDebug>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <tuple>

#ifndef Q_OS_WIN
#include <dlfcn.h>   // For dladdr() to resolve caller symbols
#include <unwind.h>  // For _Unwind_Backtrace stack walking

// Helper for stack unwinding
struct BacktraceState {
    void** current;
    void** end;
};

static _Unwind_Reason_Code unwindCallback(struct _Unwind_Context* context, void* arg) {
    BacktraceState* state = static_cast<BacktraceState*>(arg);
    uintptr_t pc = _Unwind_GetIP(context);
    if (pc) {
        if (state->current == state->end) {
            return _URC_END_OF_STACK;
        }
        *state->current++ = reinterpret_cast<void*>(pc);
    }
    return _URC_NO_REASON;
}

static size_t captureBacktrace(void** buffer, size_t maxFrames) {
    BacktraceState state = {buffer, buffer + maxFrames};
    _Unwind_Backtrace(unwindCallback, &state);
    return state.current - buffer;
}
#endif


ProfileManager::ProfileManager(Settings* settings, DE1Device* device,
                               MachineState* machineState,
                               ProfileStorage* profileStorage,
                               QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_device(device)
    , m_machineState(machineState)
    , m_profileStorage(profileStorage)
{
    // Retry pending profile upload when machine reaches Idle, Ready, Sleep, or
    // Heating — phases where it's safe to write a new profile to the DE1.
    if (m_machineState) {
        connect(m_machineState, &MachineState::phaseChanged, this, [this]() {
            if (!m_profileUploadPending) return;
            auto phase = m_machineState->phase();
            if (phase == MachineState::Phase::Disconnected) {
                qDebug() << "Clearing pending profile upload: device disconnected";
                m_profileUploadPending = false;
                return;
            }
            if (phase == MachineState::Phase::Idle || phase == MachineState::Phase::Ready ||
                phase == MachineState::Phase::Sleep || phase == MachineState::Phase::Heating) {
                qDebug() << "Retrying pending profile upload now that phase is" << m_machineState->phaseString();
                uploadCurrentProfile();
            }
        });
    }

    // Refresh profiles when storage permission changes (Android)
    if (m_profileStorage) {
        connect(m_profileStorage, &ProfileStorage::configuredChanged, this, [this]() {
            if (m_profileStorage->isConfigured()) {
                qDebug() << "[ProfileManager] Storage configured, refreshing profiles";
                refreshProfiles();
            }
        });
    }

    // Migrate profile folders (one-time migration for existing users)
    migrateProfileFolders();

    // Load initial profile
    refreshProfiles();

    // One-time migration: resave profiles in unified de1app-compatible format
    migrateProfileFormat();

    // One-time migration: regenerate frames for recipe-mode profiles so weight exits are applied
    migrateRecipeFrames();

    // One-time migration: rename user overrides of built-in profiles, fix broken D-Flow/A-Flow
    migrateReadOnlyProfiles();

    // Check for temp file (modified profile from previous session)
    QString tempPath = profilesPath() + "/_current.json";
    if (QFile::exists(tempPath)) {
        qDebug() << "Loading modified profile from temp file:" << tempPath;
        m_currentProfile = Profile::loadFromFile(tempPath);
        updateProfileKnowledgeBaseId();
        m_profileModified = true;
        // Get the base profile name from settings
        if (m_settings) {
            m_baseProfileName = m_settings->currentProfile();
            // Sync selectedFavoriteProfile so UI shows correct pill
            int favoriteIndex = m_settings->findFavoriteIndexByFilename(m_baseProfileName);
            m_settings->setSelectedFavoriteProfile(favoriteIndex);
            // Restore overrides — preserve persisted brew override from previous session
            if (!m_settings->hasBrewYieldOverride())
                m_settings->setBrewYieldOverride(m_currentProfile.targetWeight());
            m_settings->setTemperatureOverride(m_currentProfile.espressoTemperature());
        }
        if (m_machineState) {
            m_machineState->setTargetWeight(targetWeight());
        }
        // Upload to machine if connected
        if (m_currentProfile.mode() == Profile::Mode::FrameBased) {
            uploadCurrentProfile();
        }
    } else if (m_settings) {
        loadProfile(m_settings->currentProfile());
    } else {
        loadDefaultProfile();
    }
    m_profileJsonCache.clear();  // Free cached JSON after startup profile load
    m_startupLoadDone = true;

    // Keep MachineState in sync when yield override changes in Settings
    if (m_settings) {
        connect(m_settings, &Settings::brewOverridesChanged, this, [this]() {
            if (m_machineState) {
                m_machineState->setTargetWeight(targetWeight());
            }
            emit targetWeightChanged();
        });
        connect(m_settings, &Settings::dyeBeanWeightChanged, this, [this]() {
            emit targetWeightChanged();
        });

        // Update profile lists when selection/hidden state changes
        connect(m_settings, &Settings::selectedBuiltInProfilesChanged, this, &ProfileManager::profilesChanged);
        connect(m_settings, &Settings::hiddenProfilesChanged, this, &ProfileManager::profilesChanged);
    }
}


// === Profile state ===

QString ProfileManager::currentProfileName() const {
    if (m_profileModified) {
        // Read-only profiles get "(modified)" suffix so Visualizer uploads
        // don't confuse people seeing an altered version of a stock profile.
        if (isCurrentProfileReadOnly())
            return m_currentProfile.title() + " (modified)";
        return "*" + m_currentProfile.title();
    }
    return m_currentProfile.title();
}

bool ProfileManager::isDFlowTitle(const QString& title) {
    // Check if title indicates a D-Flow profile (matching de1app behavior)
    // Ignores leading * (modified indicator that may come from imports)
    QString t = title.startsWith(QLatin1Char('*')) ? title.mid(1) : title;
    return t.startsWith(QStringLiteral("D-Flow"), Qt::CaseInsensitive);
}

bool ProfileManager::isAFlowTitle(const QString& title) {
    // Check if title indicates an A-Flow profile (matching de1app behavior)
    // Ignores leading * (modified indicator that may come from imports)
    QString t = title.startsWith(QLatin1Char('*')) ? title.mid(1) : title;
    return t.startsWith(QStringLiteral("A-Flow"), Qt::CaseInsensitive);
}

bool ProfileManager::isCurrentProfileRecipe() const {
    return m_currentProfile.editorType() != QLatin1String("advanced");
}

QString ProfileManager::currentEditorType() const {
    // Title overrides stored editorType for D-Flow/A-Flow (safety net for imports)
    if (isDFlowTitle(m_currentProfile.title())) return QStringLiteral("dflow");
    if (isAFlowTitle(m_currentProfile.title())) return QStringLiteral("aflow");

    return m_currentProfile.editorType();
}


// === Target weight / brew-by-ratio ===

double ProfileManager::targetWeight() const {
    if (m_settings && m_settings->hasBrewYieldOverride()) {
        return m_settings->brewYieldOverride();
    }
    return m_currentProfile.targetWeight();
}

bool ProfileManager::brewByRatioActive() const {
    if (!m_settings || !m_settings->hasBrewYieldOverride())
        return false;
    return qAbs(m_settings->brewYieldOverride() - m_currentProfile.targetWeight()) > 0.1;
}

double ProfileManager::brewByRatioDose() const {
    return m_settings ? m_settings->dyeBeanWeight() : 0.0;
}

double ProfileManager::brewByRatio() const {
    if (!m_settings || !m_settings->hasBrewYieldOverride()) return 0.0;
    double dose = m_settings->dyeBeanWeight();
    return dose > 0 ? m_settings->brewYieldOverride() / dose : 0.0;
}

void ProfileManager::setTargetWeight(double weight) {
    if (m_currentProfile.targetWeight() != weight) {
        m_currentProfile.setTargetWeight(weight);
        if (m_machineState) {
            m_machineState->setTargetWeight(weight);
        }
        emit targetWeightChanged();
    }
}

void ProfileManager::activateBrewWithOverrides(double dose, double yield, double temperature, const QString& grind) {
    if (m_settings) {
        m_settings->setDyeBeanWeight(dose);
        m_settings->setDyeGrinderSetting(grind);
        m_settings->setBrewYieldOverride(yield);

        m_settings->setTemperatureOverride(temperature);
    }

    double ratio = dose > 0 ? yield / dose : 2.0;
    qDebug() << "Brew overrides activated: dose=" << dose << "g, ratio=1:" << ratio
             << "-> target=" << yield << "g";

    // MachineState sync happens via brewOverridesChanged signal connection

    // Re-upload profile with temperature applied to machine frames
    uploadCurrentProfile();
}

void ProfileManager::clearBrewOverrides() {
    if (m_settings) {
        m_settings->setBrewYieldOverride(m_currentProfile.targetWeight());
        m_settings->setTemperatureOverride(m_currentProfile.espressoTemperature());
    }
    // MachineState sync happens via brewOverridesChanged signal connection
    qDebug() << "Brew overrides reset to profile defaults, target=" << m_currentProfile.targetWeight() << "g";
}


// === Profile catalog ===

QVariantList ProfileManager::availableProfiles() const {
    QVariantList result;
    for (const ProfileInfo& info : m_allProfiles) {
        QVariantMap profile;
        profile["name"] = info.filename;  // filename for loading
        profile["title"] = info.title;    // display title
        profile["editorType"] = info.editorType;
        result.append(profile);
    }

    // Sort by title alphabetically (case-insensitive)
    std::sort(result.begin(), result.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap()["title"].toString().compare(
            b.toMap()["title"].toString(), Qt::CaseInsensitive) < 0;
    });

    return result;
}

QVariantList ProfileManager::selectedProfiles() const {
    QVariantList result;

    // Get selected built-in profile names from settings
    QStringList selectedBuiltIns = m_settings ? m_settings->selectedBuiltInProfiles() : QStringList();
    QStringList hiddenProfiles = m_settings ? m_settings->hiddenProfiles() : QStringList();

    for (const ProfileInfo& info : m_allProfiles) {
        bool include = false;

        switch (info.source) {
        case ProfileSource::BuiltIn:
            // Only include if selected
            include = selectedBuiltIns.contains(info.filename);
            break;
        case ProfileSource::Downloaded:
        case ProfileSource::UserCreated:
            // Include unless explicitly hidden
            include = !hiddenProfiles.contains(info.filename);
            break;
        }

        if (include) {
            QVariantMap profile;
            profile["name"] = info.filename;
            profile["title"] = info.title;
            profile["beverageType"] = info.beverageType;
            profile["source"] = static_cast<int>(info.source);

            profile["hasKnowledgeBase"] = info.hasKnowledgeBase;
            profile["readOnly"] = info.readOnly;
            result.append(profile);
        }
    }

    // Sort by title alphabetically (case-insensitive)
    std::sort(result.begin(), result.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap()["title"].toString().compare(
            b.toMap()["title"].toString(), Qt::CaseInsensitive) < 0;
    });

    return result;
}

QVariantList ProfileManager::allBuiltInProfiles() const {
    QVariantList result;

    for (const ProfileInfo& info : m_allProfiles) {
        if (info.source == ProfileSource::BuiltIn) {
            QVariantMap profile;
            profile["name"] = info.filename;
            profile["title"] = info.title;
            profile["beverageType"] = info.beverageType;
            profile["source"] = static_cast<int>(info.source);

            profile["hasKnowledgeBase"] = info.hasKnowledgeBase;
            profile["readOnly"] = info.readOnly;
            result.append(profile);
        }
    }

    // Sort by title alphabetically (case-insensitive)
    std::sort(result.begin(), result.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap()["title"].toString().compare(
            b.toMap()["title"].toString(), Qt::CaseInsensitive) < 0;
    });

    return result;
}

QVariantList ProfileManager::cleaningProfiles() const {
    QVariantList result;

    for (const ProfileInfo& info : m_allProfiles) {
        // Include both cleaning and descale profiles in this category
        if (info.beverageType == "cleaning" || info.beverageType == "descale") {
            QVariantMap profile;
            profile["name"] = info.filename;
            profile["title"] = info.title;
            profile["beverageType"] = info.beverageType;
            profile["source"] = static_cast<int>(info.source);

            profile["hasKnowledgeBase"] = info.hasKnowledgeBase;
            profile["readOnly"] = info.readOnly;
            result.append(profile);
        }
    }

    // Sort by title alphabetically (case-insensitive)
    std::sort(result.begin(), result.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap()["title"].toString().compare(
            b.toMap()["title"].toString(), Qt::CaseInsensitive) < 0;
    });

    return result;
}

QVariantList ProfileManager::downloadedProfiles() const {
    QVariantList result;

    for (const ProfileInfo& info : m_allProfiles) {
        if (info.source == ProfileSource::Downloaded) {
            QVariantMap profile;
            profile["name"] = info.filename;
            profile["title"] = info.title;
            profile["beverageType"] = info.beverageType;
            profile["source"] = static_cast<int>(info.source);

            profile["hasKnowledgeBase"] = info.hasKnowledgeBase;
            profile["readOnly"] = info.readOnly;
            result.append(profile);
        }
    }

    // Sort by title alphabetically (case-insensitive)
    std::sort(result.begin(), result.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap()["title"].toString().compare(
            b.toMap()["title"].toString(), Qt::CaseInsensitive) < 0;
    });

    return result;
}

QVariantList ProfileManager::userCreatedProfiles() const {
    QVariantList result;

    for (const ProfileInfo& info : m_allProfiles) {
        if (info.source == ProfileSource::UserCreated) {
            QVariantMap profile;
            profile["name"] = info.filename;
            profile["title"] = info.title;
            profile["beverageType"] = info.beverageType;
            profile["source"] = static_cast<int>(info.source);

            profile["hasKnowledgeBase"] = info.hasKnowledgeBase;
            profile["readOnly"] = info.readOnly;
            result.append(profile);
        }
    }

    // Sort by title alphabetically (case-insensitive)
    std::sort(result.begin(), result.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap()["title"].toString().compare(
            b.toMap()["title"].toString(), Qt::CaseInsensitive) < 0;
    });

    return result;
}

QVariantList ProfileManager::allProfilesList() const {
    QVariantList result;

    for (const ProfileInfo& info : m_allProfiles) {
        QVariantMap profile;
        profile["name"] = info.filename;
        profile["title"] = info.title;
        profile["beverageType"] = info.beverageType;
        profile["source"] = static_cast<int>(info.source);
        profile["hasKnowledgeBase"] = info.hasKnowledgeBase;
        profile["readOnly"] = info.readOnly;
        result.append(profile);
    }

    // Sort by title alphabetically (case-insensitive)
    std::sort(result.begin(), result.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap()["title"].toString().compare(
            b.toMap()["title"].toString(), Qt::CaseInsensitive) < 0;
    });

    return result;
}


// === Profile CRUD ===

QVariantMap ProfileManager::getCurrentProfile() const {
    QVariantMap profile;
    profile["title"] = m_currentProfile.title();
    profile["author"] = m_currentProfile.author();
    profile["profile_notes"] = m_currentProfile.profileNotes();
    profile["target_weight"] = m_currentProfile.targetWeight();
    profile["target_volume"] = m_currentProfile.targetVolume();
    profile["espresso_temperature"] = m_currentProfile.espressoTemperature();
    profile["mode"] = m_currentProfile.mode() == Profile::Mode::FrameBased ? "frame_based" : "direct";
    profile["has_recommended_dose"] = m_currentProfile.hasRecommendedDose();
    profile["recommended_dose"] = m_currentProfile.recommendedDose();
    profile["tank_desired_water_temperature"] = m_currentProfile.tankDesiredWaterTemperature();
    profile["maximum_flow_range_advanced"] = m_currentProfile.maximumFlowRangeAdvanced();
    profile["maximum_pressure_range_advanced"] = m_currentProfile.maximumPressureRangeAdvanced();
    profile["maximum_pressure"] = m_currentProfile.maximumPressure();
    profile["maximum_flow"] = m_currentProfile.maximumFlow();
    profile["preinfuse_frame_count"] = m_currentProfile.preinfuseFrameCount();

    QVariantList steps;
    for (const auto& frame : m_currentProfile.steps()) {
        QVariantMap step;
        step["name"] = frame.name;
        step["temperature"] = frame.temperature;
        step["sensor"] = frame.sensor;
        step["pump"] = frame.pump;
        step["transition"] = frame.transition;
        step["pressure"] = frame.pressure;
        step["flow"] = frame.flow;
        step["seconds"] = frame.seconds;
        step["volume"] = frame.volume;
        step["exit_if"] = frame.exitIf;
        step["exit_type"] = frame.exitType;
        step["exit_pressure_over"] = frame.exitPressureOver;
        step["exit_pressure_under"] = frame.exitPressureUnder;
        step["exit_flow_over"] = frame.exitFlowOver;
        step["exit_flow_under"] = frame.exitFlowUnder;
        step["exit_weight"] = frame.exitWeight;
        step["popup"] = frame.popup;
        step["max_flow_or_pressure"] = frame.maxFlowOrPressure;
        step["max_flow_or_pressure_range"] = frame.maxFlowOrPressureRange;
        steps.append(step);
    }
    profile["steps"] = steps;

    return profile;
}

void ProfileManager::markProfileClean() {
    if (m_profileModified) {
        m_profileModified = false;
        emit profileModifiedChanged();
        emit currentProfileChanged();  // Update the name (remove * prefix)

        // Remove temp file since we're now clean
        QString tempPath = profilesPath() + "/_current.json";
        QFile::remove(tempPath);
        qDebug() << "Profile marked clean, removed temp file";
    }
}

QString ProfileManager::titleToFilename(const QString& title) const {
    // Replace accented characters
    QString result = title;
    result.replace(QChar(0xE9), 'e');  // e
    result.replace(QChar(0xE8), 'e');  // e
    result.replace(QChar(0xEA), 'e');  // e
    result.replace(QChar(0xEB), 'e');  // e
    result.replace(QChar(0xE1), 'a');  // a
    result.replace(QChar(0xE0), 'a');  // a
    result.replace(QChar(0xE2), 'a');  // a
    result.replace(QChar(0xE4), 'a');  // a
    result.replace(QChar(0xED), 'i');  // i
    result.replace(QChar(0xEC), 'i');  // i
    result.replace(QChar(0xEE), 'i');  // i
    result.replace(QChar(0xEF), 'i');  // i
    result.replace(QChar(0xF3), 'o');  // o
    result.replace(QChar(0xF2), 'o');  // o
    result.replace(QChar(0xF4), 'o');  // o
    result.replace(QChar(0xF6), 'o');  // o
    result.replace(QChar(0xFA), 'u');  // u
    result.replace(QChar(0xF9), 'u');  // u
    result.replace(QChar(0xFB), 'u');  // u
    result.replace(QChar(0xFC), 'u');  // u
    result.replace(QChar(0xF1), 'n');  // n
    result.replace(QChar(0xE7), 'c');  // c

    // Replace non-alphanumeric with underscore
    QString sanitized;
    for (const QChar& c : result) {
        if (c.isLetterOrNumber()) {
            sanitized += c.toLower();
        } else {
            sanitized += '_';
        }
    }

    // Collapse multiple underscores and trim
    while (sanitized.contains("__")) {
        sanitized.replace("__", "_");
    }
    while (sanitized.startsWith('_')) sanitized.remove(0, 1);
    while (sanitized.endsWith('_')) sanitized.chop(1);

    return sanitized;
}

QString ProfileManager::findProfileByTitle(const QString& title) const {
    for (const ProfileInfo& info : m_allProfiles) {
        if (info.title == title) {
            return info.filename;
        }
    }
    return QString();
}

bool ProfileManager::profileExists(const QString& filename) const {
    if (m_availableProfiles.contains(filename))
        return true;
    // Fallback: check disk (for profiles loaded after initial scan)
    QString path = profilesPath() + "/" + filename + ".json";
    return QFile::exists(path);
}

bool ProfileManager::deleteProfile(const QString& filename) {
    // Find the profile info
    ProfileSource source = ProfileSource::BuiltIn;
    for (const ProfileInfo& info : m_allProfiles) {
        if (info.filename == filename) {
            source = info.source;
            break;
        }
    }

    bool deleted = false;

    // Always try to clean up ProfileStorage copies (even for built-in profiles,
    // since imported copies can shadow the built-in version)
    if (m_profileStorage && m_profileStorage->isConfigured()) {
        if (m_profileStorage->deleteProfile(filename)) {
            qDebug() << "Deleted profile from ProfileStorage:" << filename;
            deleted = true;
        }
    }

    // Always try to clean up local folder copies
    QString userPath = userProfilesPath() + "/" + filename + ".json";
    if (QFile::remove(userPath)) {
        qDebug() << "Deleted profile from user storage:" << userPath;
        deleted = true;
    }
    QString downloadedPath = downloadedProfilesPath() + "/" + filename + ".json";
    if (QFile::remove(downloadedPath)) {
        qDebug() << "Deleted profile from downloaded storage:" << downloadedPath;
        deleted = true;
    }

    // Built-in profiles can't be fully deleted (they'll still show from QRC),
    // but we cleaned up any local overrides above
    if (source == ProfileSource::BuiltIn) {
        if (deleted) {
            qDebug() << "Cleaned up local override for built-in profile:" << filename;
            refreshProfiles();
        }
        return false;
    }

    if (deleted) {
        // Remove from favorites if it was a favorite
        if (m_settings && m_settings->isFavoriteProfile(filename)) {
            // Find index and remove
            QVariantList favorites = m_settings->favoriteProfiles();
            for (qsizetype i = 0; i < favorites.size(); ++i) {
                if (favorites[i].toMap()["filename"].toString() == filename) {
                    m_settings->removeFavoriteProfile(i);
                    break;
                }
            }
        }

        // Refresh the profile list
        refreshProfiles();
        return true;
    }

    qWarning() << "Failed to delete profile:" << filename;
    return false;
}

// === Read-only protection ===

bool ProfileManager::isCurrentProfileReadOnly() const {
    // A profile is read-only if its own read_only flag is set (from TCL/JSON)
    if (m_currentProfile.isReadOnly()) return true;
    // Or if it was loaded from built-in resources without a user override
    for (const ProfileInfo& info : m_allProfiles) {
        if (info.filename == m_baseProfileName)
            return info.source == ProfileSource::BuiltIn;
    }
    return false;
}

bool ProfileManager::isBuiltInFilename(const QString& filename) const {
    return QFile::exists(QStringLiteral(":/profiles/") + filename + QStringLiteral(".json"));
}

bool ProfileManager::resetProfileToDefault(const QString& filename) {
    if (!isBuiltInFilename(filename)) return false;

    // Remove user copies from all storage locations
    if (m_profileStorage && m_profileStorage->isConfigured()) {
        m_profileStorage->deleteProfile(filename);
    }
    QFile::remove(userProfilesPath() + "/" + filename + ".json");
    QFile::remove(downloadedProfilesPath() + "/" + filename + ".json");

    // Refresh and reload from the built-in QRC resource
    refreshProfiles();
    loadProfile(filename);
    qDebug() << "Reset built-in profile to default:" << filename;
    return true;
}

QVariantMap ProfileManager::getProfileByFilename(const QString& filename) const {
    Profile profile;
    bool found = false;

    // 1. Check ProfileStorage first (SAF folder on Android)
    if (m_profileStorage && m_profileStorage->isConfigured()) {
        QString jsonContent = m_profileStorage->readProfile(filename);
        if (!jsonContent.isEmpty()) {
            profile = Profile::loadFromJsonString(jsonContent);
            found = true;
        }
    }

    // 2. Check user profiles (local fallback)
    if (!found) {
        QString path = userProfilesPath() + "/" + filename + ".json";
        if (QFile::exists(path)) {
            profile = Profile::loadFromFile(path);
            found = true;
        }
    }

    // 3. Check downloaded profiles (local fallback)
    if (!found) {
        QString path = downloadedProfilesPath() + "/" + filename + ".json";
        if (QFile::exists(path)) {
            profile = Profile::loadFromFile(path);
            found = true;
        }
    }

    // 4. Check built-in profiles
    if (!found) {
        QString path = ":/profiles/" + filename + ".json";
        if (QFile::exists(path)) {
            profile = Profile::loadFromFile(path);
            found = true;
        }
    }

    if (!found) {
        return QVariantMap();  // Return empty map if not found
    }

    // Backfill empty notes from built-in profile (handles imported copies from before notes were added)
    if (profile.profileNotes().isEmpty()) {
        QString builtInPath = ":/profiles/" + filename + ".json";
        if (QFile::exists(builtInPath)) {
            Profile builtIn = Profile::loadFromFile(builtInPath);
            if (!builtIn.profileNotes().isEmpty()) {
                profile.setProfileNotes(builtIn.profileNotes());
            }
        }
    }

    // Build result map (same format as getCurrentProfile)
    QVariantMap result;
    result["title"] = profile.title();
    result["author"] = profile.author();
    result["profile_notes"] = profile.profileNotes();
    result["target_weight"] = profile.targetWeight();
    result["target_volume"] = profile.targetVolume();
    result["espresso_temperature"] = profile.espressoTemperature();
    result["mode"] = profile.mode() == Profile::Mode::FrameBased ? "frame_based" : "direct";
    result["has_recommended_dose"] = profile.hasRecommendedDose();
    result["recommended_dose"] = profile.recommendedDose();
    result["tank_desired_water_temperature"] = profile.tankDesiredWaterTemperature();
    result["maximum_flow_range_advanced"] = profile.maximumFlowRangeAdvanced();
    result["maximum_pressure_range_advanced"] = profile.maximumPressureRangeAdvanced();
    result["maximum_pressure"] = profile.maximumPressure();
    result["maximum_flow"] = profile.maximumFlow();
    result["preinfuse_frame_count"] = profile.preinfuseFrameCount();

    QVariantList steps;
    for (const auto& frame : profile.steps()) {
        QVariantMap step;
        step["name"] = frame.name;
        step["temperature"] = frame.temperature;
        step["sensor"] = frame.sensor;
        step["pump"] = frame.pump;
        step["transition"] = frame.transition;
        step["pressure"] = frame.pressure;
        step["flow"] = frame.flow;
        step["seconds"] = frame.seconds;
        step["volume"] = frame.volume;
        step["exit_if"] = frame.exitIf;
        step["exit_type"] = frame.exitType;
        step["exit_pressure_over"] = frame.exitPressureOver;
        step["exit_pressure_under"] = frame.exitPressureUnder;
        step["exit_flow_over"] = frame.exitFlowOver;
        step["exit_flow_under"] = frame.exitFlowUnder;
        step["exit_weight"] = frame.exitWeight;
        step["popup"] = frame.popup;
        step["max_flow_or_pressure"] = frame.maxFlowOrPressure;
        step["max_flow_or_pressure_range"] = frame.maxFlowOrPressureRange;
        steps.append(step);
    }
    result["steps"] = steps;

    return result;
}


// === Profile loading ===

void ProfileManager::loadProfile(const QString& profileName) {
    QString path;
    bool found = false;

    // Resolve profile name: could be title or filename (MQTT publishes titles)
    QString resolvedName = profileName;

    // First, check if it's a title (most common case from MQTT)
    for (auto it = m_profileTitles.begin(); it != m_profileTitles.end(); ++it) {
        if (it.value() == profileName) {
            resolvedName = it.key();  // Found filename for this title
            qDebug() << "ProfileManager::loadProfile: Resolved title" << profileName << "to filename" << resolvedName;
            break;
        }
    }

    // If not found as title, assume it's already a filename (fallback)

    // 1. Check ProfileStorage first (SAF folder on Android)
    if (m_profileStorage && m_profileStorage->isConfigured()) {
        // Use cached JSON from refreshProfiles() if available (avoids double-read at startup)
        QString jsonContent = m_profileJsonCache.take(resolvedName);
        if (jsonContent.isEmpty()) {
            jsonContent = m_profileStorage->readProfile(resolvedName);
        }
        if (!jsonContent.isEmpty()) {
            m_currentProfile = Profile::loadFromJsonString(jsonContent);
            found = true;
            qDebug() << "Loaded profile from ProfileStorage:" << resolvedName;
        }
    }

    // 2. Check user profiles (local fallback)
    if (!found) {
        path = userProfilesPath() + "/" + resolvedName + ".json";
        if (QFile::exists(path)) {
            m_currentProfile = Profile::loadFromFile(path);
            found = true;
        }
    }

    // 3. Check downloaded profiles (local fallback)
    if (!found) {
        path = downloadedProfilesPath() + "/" + resolvedName + ".json";
        if (QFile::exists(path)) {
            m_currentProfile = Profile::loadFromFile(path);
            found = true;
        }
    }

    // 4. Check built-in profiles
    if (!found) {
        path = ":/profiles/" + resolvedName + ".json";
        if (QFile::exists(path)) {
            m_currentProfile = Profile::loadFromFile(path);
            found = true;
        }
    }

    // 5. Fall back to default
    if (!found) {
        qWarning() << "ProfileManager::loadProfile: Profile not found:" << profileName << "(resolved:" << resolvedName << ") — loading default";
        loadDefaultProfile();
    }

    // Backfill empty notes from built-in profile (handles imported copies from before notes were added)
    if (found && m_currentProfile.profileNotes().isEmpty()) {
        QString builtInPath = ":/profiles/" + resolvedName + ".json";
        if (QFile::exists(builtInPath)) {
            Profile builtIn = Profile::loadFromFile(builtInPath);
            if (!builtIn.profileNotes().isEmpty()) {
                m_currentProfile.setProfileNotes(builtIn.profileNotes());
            }
        }
    }

    updateProfileKnowledgeBaseId();

    // Save current profile as previous before switching (only if new profile was found)
    if (found && !m_baseProfileName.isEmpty() && m_baseProfileName != resolvedName)
        m_previousProfileName = m_baseProfileName;

    // Track the base profile name (filename without extension)
    m_baseProfileName = resolvedName;
    bool wasModified = m_profileModified;
    m_profileModified = false;

    // Remove stale temp file so next startup loads the correct profile
    if (wasModified) {
        QString tempPath = profilesPath() + "/_current.json";
        QFile::remove(tempPath);
    }

    if (m_settings) {
        m_settings->setCurrentProfile(resolvedName);
        // Sync selectedFavoriteProfile with the loaded profile
        // This ensures the UI shows the correct pill as selected, or -1 if not a favorite
        int favoriteIndex = m_settings->findFavoriteIndexByFilename(resolvedName);
        qDebug() << "loadProfile:" << resolvedName << "favoriteIndex=" << favoriteIndex;
        m_settings->setSelectedFavoriteProfile(favoriteIndex);
    }

    // Initialize yield and temperature from the new profile
    // These are "shot plan" settings that always reflect the current plan
    if (m_settings) {
        // On startup, preserve persisted brew override from previous session.
        // On profile switch (after startup), reset to profile default.
        if (m_startupLoadDone || !m_settings->hasBrewYieldOverride())
            m_settings->setBrewYieldOverride(m_currentProfile.targetWeight());
        m_settings->setTemperatureOverride(m_currentProfile.espressoTemperature());

        // Apply recommended dose from profile if set
        // Deferred to next event loop to avoid QML signal cascade during profile load
        if (m_currentProfile.hasRecommendedDose() && m_currentProfile.recommendedDose() > 0) {
            double dose = m_currentProfile.recommendedDose();
            QTimer::singleShot(0, this, [this, dose]() {
                if (m_settings) {
                    m_settings->setDyeBeanWeight(dose);
                }
            });
        }
    }

    if (m_machineState) {
        m_machineState->setTargetWeight(targetWeight());
        m_machineState->setTargetVolume(m_currentProfile.targetVolume());
        m_machineState->setProfileType(m_currentProfile.profileType());
    }

    // Upload to machine if connected (for frame-based mode)
    if (m_currentProfile.mode() == Profile::Mode::FrameBased) {
        uploadCurrentProfile();
    }

    // Apply per-profile flow calibration if auto-cal is enabled
    applyFlowCalibration();

    emit currentProfileChanged();
    emit targetWeightChanged();
    if (wasModified) {
        emit profileModifiedChanged();
    }
}

bool ProfileManager::loadProfileFromJson(const QString& jsonContent) {
    if (jsonContent.isEmpty()) {
        qWarning() << "loadProfileFromJson: Empty JSON content";
        return false;
    }

    m_currentProfile = Profile::loadFromJsonString(jsonContent);
    updateProfileKnowledgeBaseId();

    if (m_currentProfile.title().isEmpty() || m_currentProfile.steps().isEmpty()) {
        qWarning() << "loadProfileFromJson: Failed to parse profile JSON";
        return false;
    }

    // Use title as base name since this profile isn't from a file
    m_baseProfileName = m_currentProfile.title();
    m_profileModified = false;

    if (m_settings) {
        // Set selectedFavoriteProfile to -1 to show non-favorite pill
        // Profiles loaded from JSON (e.g., shot history) are typically not in favorites
        m_settings->setSelectedFavoriteProfile(-1);

        // Initialize yield and temperature from the new profile
        m_settings->setBrewYieldOverride(m_currentProfile.targetWeight());
        m_settings->setTemperatureOverride(m_currentProfile.espressoTemperature());
    }

    if (m_machineState) {
        m_machineState->setTargetWeight(m_currentProfile.targetWeight());
        m_machineState->setTargetVolume(m_currentProfile.targetVolume());
        m_machineState->setProfileType(m_currentProfile.profileType());
    }

    // Upload to machine if connected (for frame-based mode)
    if (m_currentProfile.mode() == Profile::Mode::FrameBased) {
        uploadCurrentProfile();
    }

    qDebug() << "Loaded profile from JSON:" << m_currentProfile.title()
             << "with" << m_currentProfile.steps().size() << "steps";

    emit currentProfileChanged();
    emit targetWeightChanged();
    return true;
}

void ProfileManager::refreshProfiles() {
    m_availableProfiles.clear();
    m_profileTitles.clear();
    m_allProfiles.clear();
    m_profileJsonCache.clear();

    // Helper to extract profile metadata from a JSON object
    // Returns: title, beverageType, hasKnowledgeBase, editorType, readOnly
    auto extractProfileMeta = [](const QJsonObject& obj) -> std::tuple<QString, QString, bool, QString, bool> {
        QString title = obj["title"].toString();

        // Derive editor type from title + profileType (matching Profile::editorType())
        QString editorType;
        QString t = title.startsWith(QLatin1Char('*')) ? title.mid(1) : title;
        if (t.startsWith(QStringLiteral("D-Flow"), Qt::CaseInsensitive))
            editorType = QStringLiteral("dflow");
        else if (t.startsWith(QStringLiteral("A-Flow"), Qt::CaseInsensitive))
            editorType = QStringLiteral("aflow");
        else {
            QString profileType = obj["legacy_profile_type"].toString();
            if (profileType.isEmpty()) profileType = obj["profile_type"].toString();
            if (profileType == QLatin1String("settings_2a"))
                editorType = QStringLiteral("pressure");
            else if (profileType == QLatin1String("settings_2b"))
                editorType = QStringLiteral("flow");
            else
                editorType = QStringLiteral("advanced");
        }

        bool hasKb = obj.contains("knowledge_base_id");
        if (!hasKb) {
            hasKb = !ShotSummarizer::computeProfileKbId(title, editorType).isEmpty();
        }
        bool readOnly = (obj["read_only"].toInt(0) == 1);
        return {title, obj["beverage_type"].toString(), hasKb, editorType, readOnly};
    };

    // Helper to load profile metadata from file path
    auto loadProfileMeta = [&extractProfileMeta](const QString& path) -> std::tuple<QString, QString, bool, QString, bool> {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            return extractProfileMeta(doc.object());
        }
        return {QString(), QString(), false, QStringLiteral("advanced"), false};
    };

    // Helper to load profile metadata from JSON string
    auto loadProfileMetaFromJson = [&extractProfileMeta](const QString& jsonContent) -> std::tuple<QString, QString, bool, QString, bool> {
        QJsonDocument doc = QJsonDocument::fromJson(jsonContent.toUtf8());
        return extractProfileMeta(doc.object());
    };

    QStringList filters;
    filters << "*.json";

    // 1. Load built-in profiles (always available)
    QDir builtInDir(":/profiles");
    QStringList files = builtInDir.entryList(filters, QDir::Files);
    for (const QString& file : files) {
        QString name = file.left(file.length() - 5);  // Remove .json
        auto [title, beverageType, hasKnowledgeBase, editorType, readOnly] = loadProfileMeta(":/profiles/" + file);

        ProfileInfo info;
        info.filename = name;
        info.title = title.isEmpty() ? name : title;
        info.beverageType = beverageType;
        info.editorType = editorType;
        info.source = ProfileSource::BuiltIn;

        info.hasKnowledgeBase = hasKnowledgeBase;
        info.readOnly = true;  // Built-in profiles are always read-only
        m_allProfiles.append(info);

        m_availableProfiles.append(name);
        m_profileTitles[name] = info.title;
    }

    // 2. Load profiles from ProfileStorage (SAF folder or fallback)
    // ProfileStorage takes loading priority over built-in (loadProfile checks it first),
    // so if a copy exists here it should override the built-in entry in the list too.
    if (m_profileStorage) {
        QStringList storageProfiles = m_profileStorage->listProfiles();
        for (const QString& name : storageProfiles) {
            QString jsonContent = m_profileStorage->readProfile(name);
            if (jsonContent.isEmpty()) {
                continue;
            }

            // Cache for loadProfile() to avoid re-reading from storage
            m_profileJsonCache[name] = jsonContent;

            auto [title, beverageType, hasKnowledgeBase, editorType, readOnly] = loadProfileMetaFromJson(jsonContent);

            ProfileInfo info;
            info.filename = name;
            info.title = title.isEmpty() ? name : title;
            info.beverageType = beverageType;
            info.editorType = editorType;
            info.source = ProfileSource::UserCreated;
    
            info.hasKnowledgeBase = hasKnowledgeBase;
            info.readOnly = readOnly;

            if (m_availableProfiles.contains(name)) {
                // Override built-in entry so list matches what loadProfile() actually loads
                for (qsizetype i = 0; i < m_allProfiles.size(); ++i) {
                    if (m_allProfiles[i].filename == name) {
                        m_allProfiles[i] = info;
                        break;
                    }
                }
            } else {
                m_allProfiles.append(info);
                m_availableProfiles.append(name);
            }
            m_profileTitles[name] = info.title;
        }
    }

    // 3. Load downloaded profiles (legacy local folder)
    QDir downloadedDir(downloadedProfilesPath());
    files = downloadedDir.entryList(filters, QDir::Files);
    for (const QString& file : files) {
        QString name = file.left(file.length() - 5);
        if (m_availableProfiles.contains(name)) {
            continue;  // Skip if already loaded from ProfileStorage
        }

        auto [title, beverageType, hasKnowledgeBase, editorType, readOnly] = loadProfileMeta(downloadedDir.filePath(file));

        ProfileInfo info;
        info.filename = name;
        info.title = title.isEmpty() ? name : title;
        info.beverageType = beverageType;
        info.editorType = editorType;
        info.source = ProfileSource::Downloaded;

        info.hasKnowledgeBase = hasKnowledgeBase;
        info.readOnly = readOnly;
        m_allProfiles.append(info);

        m_availableProfiles.append(name);
        m_profileTitles[name] = info.title;
    }

    // 4. Load user-created profiles (legacy local folder)
    QDir userDir(userProfilesPath());
    files = userDir.entryList(filters, QDir::Files);
    for (const QString& file : files) {
        QString name = file.left(file.length() - 5);
        if (m_availableProfiles.contains(name)) {
            continue;  // Skip if already loaded from ProfileStorage
        }

        auto [title, beverageType, hasKnowledgeBase, editorType, readOnly] = loadProfileMeta(userDir.filePath(file));

        ProfileInfo info;
        info.filename = name;
        info.title = title.isEmpty() ? name : title;
        info.beverageType = beverageType;
        info.editorType = editorType;
        info.source = ProfileSource::UserCreated;

        info.hasKnowledgeBase = hasKnowledgeBase;
        info.readOnly = readOnly;
        m_allProfiles.append(info);

        m_availableProfiles.append(name);
        m_profileTitles[name] = info.title;
    }

    emit profilesChanged();
    emit allBuiltInProfileListChanged();
}


// === Profile upload ===

void ProfileManager::uploadCurrentProfile() {
    // Guard: Don't upload profile during active operations - this corrupts the running shot!
    if (m_machineState) {
        auto phase = m_machineState->phase();
        bool isActivePhase = (phase == MachineState::Phase::EspressoPreheating ||
                              phase == MachineState::Phase::Preinfusion ||
                              phase == MachineState::Phase::Pouring ||
                              phase == MachineState::Phase::Ending ||
                              phase == MachineState::Phase::Steaming ||
                              phase == MachineState::Phase::HotWater ||
                              phase == MachineState::Phase::Flushing ||
                              phase == MachineState::Phase::Descaling ||
                              phase == MachineState::Phase::Cleaning);

        if (isActivePhase) {
            qWarning() << "uploadCurrentProfile() BLOCKED during active phase:"
                       << m_machineState->phaseString();

            QString stackTrace = "Stack trace:\n";
#ifndef Q_OS_WIN
            void* stack[15];
            size_t frameCount = captureBacktrace(stack, 15);
            for (size_t i = 0; i < frameCount; i++) {
                Dl_info info;
                QString frameLine;
                if (dladdr(stack[i], &info) && info.dli_sname) {
                    frameLine = QString("  #%1: %2 (+%3)")
                        .arg(i)
                        .arg(info.dli_sname)
                        .arg(reinterpret_cast<quintptr>(stack[i]) - reinterpret_cast<quintptr>(info.dli_saddr));
                } else {
                    frameLine = QString("  #%1: 0x%2")
                        .arg(i)
                        .arg(reinterpret_cast<quintptr>(stack[i]), 0, 16);
                }
                stackTrace += frameLine + "\n";
                qWarning().noquote() << frameLine;
            }
#else
            stackTrace += "  (not available on Windows)\n";
#endif
            emit profileUploadBlocked(m_machineState->phaseString(), stackTrace);
            m_profileUploadPending = true;
            return;  // Don't upload!
        }
    }

    if (m_device && m_device->isConnected()) {
        m_profileUploadPending = false;
        double groupTemp;

        // Apply temperature override as delta offset (preserves per-frame differences)
        if (m_settings && m_settings->hasTemperatureOverride()) {
            Profile modifiedProfile = m_currentProfile;
            double overrideTemp = m_settings->temperatureOverride();
            double delta = overrideTemp - m_currentProfile.espressoTemperature();
            QList<ProfileFrame> steps = modifiedProfile.steps();
            for (int i = 0; i < steps.size(); ++i) {
                steps[i].temperature += delta;
            }
            modifiedProfile.setSteps(steps);
            modifiedProfile.setEspressoTemperature(overrideTemp);
            qDebug() << "Uploading profile with temperature override:" << overrideTemp
                     << "C (delta:" << delta << "C)";
            m_device->uploadProfile(modifiedProfile);
            groupTemp = overrideTemp;
        } else {
            m_device->uploadProfile(m_currentProfile);
            groupTemp = m_currentProfile.espressoTemperature();
        }

        // Update shot settings with the profile's target temperature
        // This controls what temperature the machine heats to in Ready state
        if (m_settings) {
            double steamTemp = (m_settings->steamDisabled() || !m_settings->keepSteamHeaterOn()) ? 0.0 : m_settings->steamTemperature();
            m_device->setShotSettings(
                steamTemp,
                m_settings->steamTimeout(),
                m_settings->waterTemperature(),
                m_settings->waterVolume(),
                groupTemp
            );
            qDebug() << "Set group temp to" << groupTemp << "C for profile" << m_currentProfile.title();
        }
    } else if (m_profileUploadPending) {
        qDebug() << "uploadCurrentProfile: device not connected, keeping pending flag for later retry";
    }
}

void ProfileManager::uploadProfile(const QVariantMap& profileData) {
    // Update current profile from QML data
    if (profileData.contains("title")) {
        m_currentProfile.setTitle(profileData["title"].toString());
    }
    if (profileData.contains("author")) {
        m_currentProfile.setAuthor(profileData["author"].toString());
    }
    if (profileData.contains("profile_notes")) {
        m_currentProfile.setProfileNotes(profileData["profile_notes"].toString());
    }
    if (profileData.contains("espresso_temperature")) {
        double newTemp = profileData["espresso_temperature"].toDouble();
        m_currentProfile.setEspressoTemperature(newTemp);
        // Sync temperature override so uploadCurrentProfile doesn't apply wrong delta
        if (m_settings) {
            m_settings->setTemperatureOverride(newTemp);
        }
    }
    if (profileData.contains("target_weight")) {
        double newWeight = profileData["target_weight"].toDouble();
        m_currentProfile.setTargetWeight(newWeight);
        if (m_machineState) {
            m_machineState->setTargetWeight(newWeight);
        }
        // Sync yield override so it reflects the new profile target
        if (m_settings) {
            m_settings->setBrewYieldOverride(newWeight);
        }
    }
    if (profileData.contains("target_volume")) {
        m_currentProfile.setTargetVolume(profileData["target_volume"].toDouble());
        if (m_machineState) {
            m_machineState->setTargetVolume(m_currentProfile.targetVolume());
            m_machineState->setProfileType(m_currentProfile.profileType());
        }
    }
    if (profileData.contains("has_recommended_dose")) {
        m_currentProfile.setHasRecommendedDose(profileData["has_recommended_dose"].toBool());
    }
    if (profileData.contains("recommended_dose")) {
        m_currentProfile.setRecommendedDose(profileData["recommended_dose"].toDouble());
    }
    if (profileData.contains("tank_desired_water_temperature")) {
        m_currentProfile.setTankDesiredWaterTemperature(profileData["tank_desired_water_temperature"].toDouble());
    }
    if (profileData.contains("maximum_flow_range_advanced")) {
        m_currentProfile.setMaximumFlowRangeAdvanced(profileData["maximum_flow_range_advanced"].toDouble());
    }
    if (profileData.contains("maximum_pressure_range_advanced")) {
        m_currentProfile.setMaximumPressureRangeAdvanced(profileData["maximum_pressure_range_advanced"].toDouble());
    }
    if (profileData.contains("preinfuse_frame_count")) {
        m_currentProfile.setPreinfuseFrameCount(profileData["preinfuse_frame_count"].toInt());
    }

    // Update steps/frames - build new list atomically to avoid any reference issues
    if (profileData.contains("steps")) {
        QList<ProfileFrame> newSteps;
        QVariantList steps = profileData["steps"].toList();
        newSteps.reserve(steps.size());
        for (const QVariant& stepVar : steps) {
            QVariantMap step = stepVar.toMap();
            ProfileFrame frame;
            frame.name = step["name"].toString();
            frame.temperature = step["temperature"].toDouble();
            frame.sensor = step["sensor"].toString();
            frame.pump = step["pump"].toString();
            frame.transition = step["transition"].toString();
            frame.pressure = step["pressure"].toDouble();
            frame.flow = step["flow"].toDouble();
            frame.seconds = step["seconds"].toDouble();
            frame.volume = step["volume"].toDouble();
            frame.exitIf = step["exit_if"].toBool();
            frame.exitType = step["exit_type"].toString();
            frame.exitPressureOver = step["exit_pressure_over"].toDouble();
            frame.exitPressureUnder = step["exit_pressure_under"].toDouble();
            frame.exitFlowOver = step["exit_flow_over"].toDouble();
            frame.exitFlowUnder = step["exit_flow_under"].toDouble();
            frame.exitWeight = step["exit_weight"].toDouble();
            frame.popup = step["popup"].toString();
            frame.maxFlowOrPressure = step["max_flow_or_pressure"].toDouble();
            frame.maxFlowOrPressureRange = step["max_flow_or_pressure_range"].toDouble();
            newSteps.append(frame);
        }
        // Replace all steps atomically
        m_currentProfile.setSteps(newSteps);

        qDebug() << "uploadProfile: Updated" << newSteps.size() << "steps";
        for (int i = 0; i < newSteps.size(); i++) {
            qDebug() << "  Frame" << i << ":" << newSteps[i].name << "temp=" << newSteps[i].temperature;
        }
    }

    // Mark as modified
    if (!m_profileModified) {
        m_profileModified = true;
        emit profileModifiedChanged();
    }

    // Save to temp file for persistence across restarts
    QString tempPath = profilesPath() + "/_current.json";
    if (!m_currentProfile.saveToFile(tempPath)) {
        qWarning() << "Failed to save modified profile to temp file:" << tempPath;
    }

    // NOTE: BLE upload deferred to editor exit (QML calls uploadCurrentProfile() explicitly).
    // This avoids flooding the DE1 with BLE writes on every slider tick. See #557.

    emit currentProfileChanged();
}

bool ProfileManager::saveProfile(const QString& filename) {
    // Prevent saving over read-only profiles
    if (isCurrentProfileReadOnly()) {
        qWarning() << "ProfileManager::saveProfile: Cannot save read-only profile in place:" << filename;
        return false;
    }

    bool success = false;

    // Try ProfileStorage first (SAF on Android), then fall back to local file
    if (m_profileStorage && m_profileStorage->isConfigured()) {
        success = m_profileStorage->writeProfile(filename, m_currentProfile.toJsonString());
        if (success) {
            qDebug() << "Saved profile to ProfileStorage:" << filename;
        }
    }

    if (!success) {
        // Fall back to local file
        QString path = userProfilesPath() + "/" + filename + ".json";
        success = m_currentProfile.saveToFile(path);
        if (success) {
            qDebug() << "Saved profile to local file:" << path;
        } else {
            qWarning() << "Failed to save profile to:" << path;
        }
    }

    if (success) {
        // If saving a built-in profile, auto-select it and update favorites
        if (m_settings) {
            // Check if this was originally a built-in
            bool wasBuiltIn = false;
            for (const ProfileInfo& info : m_allProfiles) {
                if (info.filename == m_baseProfileName && info.source == ProfileSource::BuiltIn) {
                    wasBuiltIn = true;
                    break;
                }
            }

            // If it was a built-in and is in favorites, the favorite now points to user copy
            if (wasBuiltIn && m_settings->isFavoriteProfile(m_baseProfileName)) {
                m_settings->updateFavoriteProfile(m_baseProfileName, filename, m_currentProfile.title());
            }
        }

        m_baseProfileName = filename;
        markProfileClean();
        refreshProfiles();

        // Re-upload profile to machine to ensure it's synced after save
        if (m_currentProfile.mode() == Profile::Mode::FrameBased) {
            uploadCurrentProfile();
        }
    }
    return success;
}

bool ProfileManager::saveProfileAs(const QString& filename, const QString& title) {
    // Prevent saving with a built-in profile filename
    if (isBuiltInFilename(filename)) {
        qWarning() << "ProfileManager::saveProfileAs: Cannot overwrite built-in profile filename:" << filename;
        return false;
    }

    // Remember old filename for favorite update
    QString oldFilename = m_baseProfileName;

    // Clear read-only flag on Save As copies — user copies are always editable
    m_currentProfile.setReadOnly(0);

    // Update the profile title
    m_currentProfile.setTitle(title);

    bool success = false;

    // Try ProfileStorage first (SAF on Android), then fall back to local file
    if (m_profileStorage && m_profileStorage->isConfigured()) {
        success = m_profileStorage->writeProfile(filename, m_currentProfile.toJsonString());
        if (success) {
            qDebug() << "Saved profile as to ProfileStorage:" << filename;
        }
    }

    if (!success) {
        // Fall back to local file
        QString path = userProfilesPath() + "/" + filename + ".json";
        success = m_currentProfile.saveToFile(path);
        if (success) {
            qDebug() << "Saved profile as to local file:" << path;
        } else {
            qWarning() << "Failed to save profile to:" << path;
        }
    }

    if (success) {
        m_baseProfileName = filename;
        if (m_settings) {
            m_settings->setCurrentProfile(filename);

            // Handle favorites based on whether this is a true "Save As" or just "Save"
            if (!oldFilename.isEmpty() && oldFilename != filename) {
                // True "Save As" - keep original favorite, add new profile to favorites
                m_settings->addFavoriteProfile(title, filename);
            } else if (!oldFilename.isEmpty()) {
                // Same filename - just update the title if it changed
                m_settings->updateFavoriteProfile(oldFilename, filename, title);
            } else {
                // New profile (no old filename) - add to favorites
                m_settings->addFavoriteProfile(title, filename);
            }
        }
        markProfileClean();
        refreshProfiles();

        // Re-upload profile to machine to ensure it's synced after save
        // This catches edge cases where the previous upload may not have completed
        if (m_currentProfile.mode() == Profile::Mode::FrameBased) {
            uploadCurrentProfile();
        }

        emit currentProfileChanged();
    }
    return success;
}


// === Profile editing (recipe/frame) ===

void ProfileManager::uploadRecipeProfile(const QVariantMap& recipeParams) {
    RecipeParams recipe = RecipeParams::fromVariantMap(recipeParams);

    // Validate recipe parameters before generating frames
    QStringList issues = recipe.validate();
    if (!issues.isEmpty()) {
        qWarning() << "RecipeParams validation issues:" << issues.join("; ");
    }
    recipe.clamp();  // Ensure values are within hardware limits

    const QString& pt = m_currentProfile.profileType();
    bool isSimpleProfile = (pt == QLatin1String("settings_2a") || pt == QLatin1String("settings_2b"));

    if (isSimpleProfile) {
        // Simple profile path: write RecipeParams back to scalar fields and
        // regenerate frames using the de1app-compatible generators.
        applyRecipeToScalarFields(recipe);
        m_currentProfile.regenerateSimpleFrames();
        m_currentProfile.setTargetWeight(recipe.targetWeight);
        m_currentProfile.setTargetVolume(recipe.targetVolume);
    } else {
        // Recipe/D-Flow/A-Flow path
        RecipeParams oldRecipe = m_currentProfile.recipeParams();
        bool needFrameRegen = m_currentProfile.steps().isEmpty()
                           || !oldRecipe.frameAffectingFieldsEqual(recipe);

        m_currentProfile.setRecipeParams(recipe);

        if (needFrameRegen) {
            m_currentProfile.regenerateFromRecipe();
        } else {
            m_currentProfile.setTargetWeight(recipe.targetWeight);
            m_currentProfile.setTargetVolume(recipe.targetVolume);
        }
    }

    // Sync overrides so uploadCurrentProfile doesn't apply wrong delta
    // and shot plan text shows correct values (not stale overrides)
    if (m_settings) {
        m_settings->setTemperatureOverride(m_currentProfile.espressoTemperature());
        m_settings->setBrewYieldOverride(m_currentProfile.targetWeight());
    }

    // Sync stop targets to MachineState so SAW/volume checks use current values
    if (m_machineState) {
        m_machineState->setTargetWeight(m_currentProfile.targetWeight());
        m_machineState->setTargetVolume(m_currentProfile.targetVolume());
        m_machineState->setProfileType(m_currentProfile.profileType());
    }

    // Mark as modified
    if (!m_profileModified) {
        m_profileModified = true;
        emit profileModifiedChanged();
    }
    emit currentProfileChanged();
    emit targetWeightChanged();

    // NOTE: BLE upload deferred to editor exit (QML calls uploadCurrentProfile() explicitly).
    // This avoids flooding the DE1 with BLE writes on every slider tick. See #557.

    qDebug() << "Recipe profile updated with" << m_currentProfile.steps().size() << "frames (BLE upload deferred)";
}

void ProfileManager::applyRecipeToScalarFields(const RecipeParams& recipe) {
    // Common preinfusion fields
    m_currentProfile.setPreinfusionTime(recipe.preinfusionTime);
    m_currentProfile.setPreinfusionFlowRate(recipe.preinfusionFlowRate);
    m_currentProfile.setPreinfusionStopPressure(recipe.preinfusionStopPressure);

    // Temperature presets from per-step temperatures
    m_currentProfile.setTemperaturePresets({
        recipe.tempStart, recipe.tempPreinfuse,
        recipe.tempHold, recipe.tempDecline
    });

    // Compute tempStepsEnabled: true if any step temp differs from another
    bool tempsDiffer = !qFuzzyCompare(recipe.tempStart, recipe.tempPreinfuse)
                    || !qFuzzyCompare(recipe.tempStart, recipe.tempHold)
                    || !qFuzzyCompare(recipe.tempStart, recipe.tempDecline);
    m_currentProfile.setTempStepsEnabled(tempsDiffer);

    // espressoHoldTime/espressoDeclineTime are used by both generators as the
    // holdTime/declineTime parameters -- always set them regardless of profile type
    m_currentProfile.setEspressoHoldTime(recipe.holdTime);
    m_currentProfile.setEspressoDeclineTime(recipe.simpleDeclineTime);

    const QString& pt = m_currentProfile.profileType();
    if (pt == QLatin1String("settings_2a")) {
        m_currentProfile.setEspressoPressure(recipe.espressoPressure);
        m_currentProfile.setPressureEnd(recipe.pressureEnd);
        m_currentProfile.setMaximumFlow(recipe.limiterValue);
        m_currentProfile.setMaximumFlowRangeDefault(recipe.limiterRange);
    } else {
        // settings_2b -- also set flow-specific hold/decline time fields
        m_currentProfile.setFlowProfileHoldTime(recipe.holdTime);
        m_currentProfile.setFlowProfileDeclineTime(recipe.simpleDeclineTime);
        m_currentProfile.setFlowProfileHold(recipe.holdFlow);
        m_currentProfile.setFlowProfileDecline(recipe.flowEnd);
        m_currentProfile.setMaximumPressure(recipe.limiterValue);
        m_currentProfile.setMaximumPressureRangeDefault(recipe.limiterRange);
    }

    // Set espressoTemperature from the first preset (will be synced from first frame
    // after regenerateSimpleFrames, but set it here for consistency)
    m_currentProfile.setEspressoTemperature(recipe.tempStart);
}

QVariantMap ProfileManager::getOrConvertRecipeParams() {
    const QString& et = m_currentProfile.editorType();

    // D-Flow/A-Flow with stored recipe params: return them directly
    if ((et == QLatin1String("dflow") || et == QLatin1String("aflow"))
        && m_currentProfile.recipeParams().targetWeight > 0) {
        // Ensure editorType matches title (handles profiles saved with wrong type)
        RecipeParams params = m_currentProfile.recipeParams();
        if (isAFlowTitle(m_currentProfile.title()) && params.editorType != EditorType::AFlow) {
            params.editorType = EditorType::AFlow;
            m_currentProfile.setRecipeParams(params);
        }
        return m_currentProfile.recipeParams().toVariantMap();
    }

    // D-Flow/A-Flow profiles from de1app (no stored recipe): extract from frames on-the-fly
    if (isDFlowTitle(m_currentProfile.title()) || isAFlowTitle(m_currentProfile.title())
        || et == QLatin1String("dflow") || et == QLatin1String("aflow")) {
        RecipeParams params = RecipeAnalyzer::extractRecipeParams(m_currentProfile);
        if (isAFlowTitle(m_currentProfile.title())) {
            params.editorType = EditorType::AFlow;
        }
        return params.toVariantMap();
    }

    // Simple profiles (settings_2a/2b): populate RecipeParams from scalar fields
    const QString& pt = m_currentProfile.profileType();
    if (pt == QLatin1String("settings_2a") || pt == QLatin1String("settings_2b")) {
        RecipeParams params;
        params.targetWeight = m_currentProfile.targetWeight();
        params.targetVolume = m_currentProfile.targetVolume();
        params.fillTemperature = m_currentProfile.espressoTemperature();
        params.pourTemperature = m_currentProfile.espressoTemperature();
        // Temperature presets: [0]=Start, [1]=Preinfuse, [2]=Hold, [3]=Decline
        const auto& presets = m_currentProfile.temperaturePresets();
        double baseTemp = m_currentProfile.espressoTemperature();
        params.tempStart = presets.value(0, baseTemp);
        params.tempPreinfuse = presets.value(1, baseTemp);
        params.tempHold = presets.value(2, baseTemp);
        params.tempDecline = presets.value(3, baseTemp);
        params.preinfusionTime = m_currentProfile.preinfusionTime();
        params.preinfusionFlowRate = m_currentProfile.preinfusionFlowRate();
        params.preinfusionStopPressure = m_currentProfile.preinfusionStopPressure();
        // Both settings_2a and settings_2b use espressoHoldTime/espressoDeclineTime
        // for frame generation. De1app's flow_to_advanced_list uses these same fields,
        // NOT flow_profile_hold_time/flow_profile_decline_time (which are GUI-only metadata).
        params.holdTime = m_currentProfile.espressoHoldTime();
        params.simpleDeclineTime = m_currentProfile.espressoDeclineTime();
        if (pt == QLatin1String("settings_2a")) {
            params.editorType = EditorType::Pressure;
            params.espressoPressure = m_currentProfile.espressoPressure();
            params.pressureEnd = m_currentProfile.pressureEnd();
            params.limiterValue = m_currentProfile.maximumFlow();
            params.limiterRange = m_currentProfile.maximumFlowRangeDefault();
        } else {
            params.editorType = EditorType::Flow;
            params.holdFlow = m_currentProfile.flowProfileHold();
            params.flowEnd = m_currentProfile.flowProfileDecline();
            params.limiterValue = m_currentProfile.maximumPressure();
            params.limiterRange = m_currentProfile.maximumPressureRangeDefault();
        }
        return params.toVariantMap();
    }

    // Advanced profile — no recipe params, return defaults
    return RecipeParams().toVariantMap();
}

void ProfileManager::createNewRecipe(const QString& title) {
    createNewProfileWithEditorType(EditorType::DFlow, title);
}

void ProfileManager::createNewAFlowRecipe(const QString& title) {
    createNewProfileWithEditorType(EditorType::AFlow, title);
}

void ProfileManager::createNewPressureProfile(const QString& title) {
    createNewProfileWithEditorType(EditorType::Pressure, title);
}

void ProfileManager::createNewFlowProfile(const QString& title) {
    createNewProfileWithEditorType(EditorType::Flow, title);
}

void ProfileManager::createNewProfileWithEditorType(EditorType type, const QString& title) {
    RecipeParams recipe;
    recipe.editorType = type;
    recipe.applyEditorDefaults();
    recipe.clamp();  // Ensure values are within hardware limits

    m_currentProfile = RecipeGenerator::createProfile(recipe, title);
    updateProfileKnowledgeBaseId();
    m_baseProfileName = "";
    m_profileModified = true;

    if (m_settings) {
        m_settings->setSelectedFavoriteProfile(-1);
        m_settings->setBrewYieldOverride(m_currentProfile.targetWeight());
        m_settings->setTemperatureOverride(m_currentProfile.espressoTemperature());
    }
    if (m_machineState) {
        m_machineState->setTargetWeight(m_currentProfile.targetWeight());
        m_machineState->setTargetVolume(m_currentProfile.targetVolume());
        m_machineState->setProfileType(m_currentProfile.profileType());
    }

    emit currentProfileChanged();
    emit profileModifiedChanged();
    emit targetWeightChanged();
    emit profilesChanged();
    emit allBuiltInProfileListChanged();

    uploadCurrentProfile();
    qDebug() << "Created new" << editorTypeToString(type) << "profile:" << title;
}

void ProfileManager::convertCurrentProfileToAdvanced() {
    // Convert to advanced mode: set profileType to settings_2c and strip
    // D-Flow/A-Flow title prefix so editorType() derives as "advanced".
    // The frames are already generated and are preserved as-is.
    m_currentProfile.setProfileType(QStringLiteral("settings_2c"));

    // Strip D-Flow/A-Flow prefix (case-insensitive, matching isDFlowTitle/isAFlowTitle).
    // setTitle() already strips leading '*', so title() never has one.
    QString title = m_currentProfile.title();
    auto stripPrefix = [&](const QString& prefix) {
        // title starts with prefix (case-insensitive) — strip it
        QString after = title.mid(prefix.length());
        if (after.startsWith(QLatin1String(" / ")))
            after = after.mid(3);
        else if (after.startsWith(QLatin1Char('/')))
            after = after.mid(1).trimmed();
        else
            after = after.trimmed();
        return after.isEmpty() ? QStringLiteral("Advanced Profile") : after;
    };

    if (isDFlowTitle(title)) {
        m_currentProfile.setTitle(stripPrefix(QStringLiteral("D-Flow")));
    } else if (isAFlowTitle(title)) {
        m_currentProfile.setTitle(stripPrefix(QStringLiteral("A-Flow")));
    }

    m_profileModified = true;

    emit currentProfileChanged();
    emit profileModifiedChanged();

    qDebug() << "Converted profile to Advanced mode:" << m_currentProfile.title();
}

void ProfileManager::createNewProfile(const QString& title) {
    // Create a new profile with a single default frame
    m_currentProfile = Profile();
    m_currentProfile.setTitle(title);
    m_currentProfile.setAuthor("");
    m_currentProfile.setProfileNotes("");
    m_currentProfile.setBeverageType("espresso");
    m_currentProfile.setProfileType("settings_2c");
    m_currentProfile.setTargetWeight(36.0);
    m_currentProfile.setTargetVolume(0.0);
    m_currentProfile.setEspressoTemperature(93.0);


    // Add a single default extraction frame
    ProfileFrame defaultFrame;
    defaultFrame.name = "Extraction";
    defaultFrame.temperature = 93.0;
    defaultFrame.sensor = "coffee";
    defaultFrame.pump = "pressure";
    defaultFrame.transition = "fast";
    defaultFrame.pressure = 9.0;
    defaultFrame.flow = 2.0;
    defaultFrame.seconds = 60.0;
    defaultFrame.volume = 0;
    defaultFrame.exitIf = false;
    if (!m_currentProfile.addStep(defaultFrame)) {
        qWarning() << "createNewBlankProfile: failed to add default frame";
    }

    m_baseProfileName = "";
    m_profileModified = true;

    if (m_settings) {
        m_settings->setSelectedFavoriteProfile(-1);  // New profile, not in favorites
        m_settings->setBrewYieldOverride(m_currentProfile.targetWeight());
        m_settings->setTemperatureOverride(m_currentProfile.espressoTemperature());
    }
    if (m_machineState) {
        m_machineState->setTargetWeight(m_currentProfile.targetWeight());
        m_machineState->setTargetVolume(m_currentProfile.targetVolume());
        m_machineState->setProfileType(m_currentProfile.profileType());
    }

    emit currentProfileChanged();
    emit profileModifiedChanged();
    emit targetWeightChanged();

    uploadCurrentProfile();
    qDebug() << "Created new blank profile:" << title;
}


// === Frame operations (advanced editor) ===

void ProfileManager::addFrame(int afterIndex) {
    if (m_currentProfile.steps().size() >= Profile::MAX_FRAMES) {
        qWarning() << "Cannot add frame: maximum" << Profile::MAX_FRAMES << "frames reached";
        return;
    }

    // Create a new default frame
    ProfileFrame newFrame;
    newFrame.name = QString("Step %1").arg(m_currentProfile.steps().size() + 1);
    newFrame.temperature = 93.0;
    newFrame.sensor = "coffee";
    newFrame.pump = "pressure";
    newFrame.transition = "fast";
    newFrame.pressure = 9.0;
    newFrame.flow = 2.0;
    newFrame.seconds = 30.0;
    newFrame.volume = 0;
    newFrame.exitIf = false;

    bool added = false;
    if (afterIndex < 0 || static_cast<qsizetype>(afterIndex) >= m_currentProfile.steps().size()) {
        // Add at end
        added = m_currentProfile.addStep(newFrame);
    } else {
        // Insert after specified index
        added = m_currentProfile.insertStep(afterIndex + 1, newFrame);
    }
    if (!added) {
        qWarning() << "Failed to add frame: maximum frame count reached (" << Profile::MAX_FRAMES << ")";
        return;
    }

    if (!m_profileModified) {
        m_profileModified = true;
        emit profileModifiedChanged();
    }
    emit currentProfileChanged();

    uploadCurrentProfile();
    qDebug() << "Added frame at index" << (afterIndex + 1) << ", total frames:" << m_currentProfile.steps().size();
}

void ProfileManager::deleteFrame(int index) {
    if (index < 0 || static_cast<qsizetype>(index) >= m_currentProfile.steps().size()) {
        qWarning() << "Cannot delete frame: invalid index" << index;
        return;
    }

    // Don't allow deleting the last frame
    if (m_currentProfile.steps().size() <= 1) {
        qWarning() << "Cannot delete the last frame";
        return;
    }

    if (!m_currentProfile.removeStep(index)) {
        qWarning() << "deleteFrame: removeStep failed for index" << index;
        return;
    }


    if (!m_profileModified) {
        m_profileModified = true;
        emit profileModifiedChanged();
    }
    emit currentProfileChanged();

    uploadCurrentProfile();
    qDebug() << "Deleted frame at index" << index << ", total frames:" << m_currentProfile.steps().size();
}

void ProfileManager::moveFrameUp(int index) {
    if (index <= 0 || static_cast<qsizetype>(index) >= m_currentProfile.steps().size()) {
        return;  // Can't move up if already at top or invalid
    }

    m_currentProfile.moveStep(index, index - 1);


    if (!m_profileModified) {
        m_profileModified = true;
        emit profileModifiedChanged();
    }
    emit currentProfileChanged();

    uploadCurrentProfile();
    qDebug() << "Moved frame from" << index << "to" << (index - 1);
}

void ProfileManager::moveFrameDown(int index) {
    if (index < 0 || static_cast<qsizetype>(index) >= m_currentProfile.steps().size() - 1) {
        return;  // Can't move down if already at bottom or invalid
    }

    m_currentProfile.moveStep(index, index + 1);


    if (!m_profileModified) {
        m_profileModified = true;
        emit profileModifiedChanged();
    }
    emit currentProfileChanged();

    uploadCurrentProfile();
    qDebug() << "Moved frame from" << index << "to" << (index + 1);
}

void ProfileManager::duplicateFrame(int index) {
    if (index < 0 || static_cast<qsizetype>(index) >= m_currentProfile.steps().size()) {
        qWarning() << "Cannot duplicate frame: invalid index" << index;
        return;
    }

    if (m_currentProfile.steps().size() >= Profile::MAX_FRAMES) {
        qWarning() << "Cannot duplicate frame: maximum" << Profile::MAX_FRAMES << "frames reached";
        return;
    }

    ProfileFrame copy = m_currentProfile.steps().at(index);
    copy.name = copy.name + " (copy)";
    if (!m_currentProfile.insertStep(index + 1, copy)) {
        qWarning() << "duplicateFrame: insertStep failed at index" << (index + 1);
        return;
    }


    if (!m_profileModified) {
        m_profileModified = true;
        emit profileModifiedChanged();
    }
    emit currentProfileChanged();

    uploadCurrentProfile();
    qDebug() << "Duplicated frame at index" << index;
}

void ProfileManager::setFrameProperty(int index, const QString& property, const QVariant& value) {
    if (index < 0 || static_cast<qsizetype>(index) >= m_currentProfile.steps().size()) {
        qWarning() << "setFrameProperty: invalid index" << index;
        return;
    }

    ProfileFrame frame = m_currentProfile.steps().at(index);

    // Basic properties
    if (property == "name") frame.name = value.toString();
    else if (property == "temperature") frame.temperature = value.toDouble();
    else if (property == "sensor") frame.sensor = value.toString();
    else if (property == "pump") frame.pump = value.toString();
    else if (property == "transition") frame.transition = value.toString();
    else if (property == "pressure") frame.pressure = value.toDouble();
    else if (property == "flow") frame.flow = value.toDouble();
    else if (property == "seconds") frame.seconds = value.toDouble();
    else if (property == "volume") frame.volume = value.toDouble();
    // Exit conditions
    else if (property == "exitIf") frame.exitIf = value.toBool();
    else if (property == "exitType") frame.exitType = value.toString();
    else if (property == "exitPressureOver") frame.exitPressureOver = value.toDouble();
    else if (property == "exitPressureUnder") frame.exitPressureUnder = value.toDouble();
    else if (property == "exitFlowOver") frame.exitFlowOver = value.toDouble();
    else if (property == "exitFlowUnder") frame.exitFlowUnder = value.toDouble();
    else if (property == "exitWeight") frame.exitWeight = value.toDouble();
    // Limiter
    else if (property == "maxFlowOrPressure") frame.maxFlowOrPressure = value.toDouble();
    else if (property == "maxFlowOrPressureRange") frame.maxFlowOrPressureRange = value.toDouble();
    // Popup message
    else if (property == "popup") frame.popup = value.toString();
    else {
        qWarning() << "setFrameProperty: unknown property" << property;
        return;
    }

    m_currentProfile.setStepAt(index, frame);


    if (!m_profileModified) {
        m_profileModified = true;
        emit profileModifiedChanged();
    }
    emit currentProfileChanged();

    uploadCurrentProfile();
}

QVariantMap ProfileManager::getFrameAt(int index) const {
    if (index < 0 || static_cast<qsizetype>(index) >= m_currentProfile.steps().size()) {
        return QVariantMap();
    }

    const ProfileFrame& frame = m_currentProfile.steps().at(index);
    QVariantMap map;

    // Basic properties
    map["name"] = frame.name;
    map["temperature"] = frame.temperature;
    map["sensor"] = frame.sensor;
    map["pump"] = frame.pump;
    map["transition"] = frame.transition;
    map["pressure"] = frame.pressure;
    map["flow"] = frame.flow;
    map["seconds"] = frame.seconds;
    map["volume"] = frame.volume;

    // Exit conditions
    map["exitIf"] = frame.exitIf;
    map["exitType"] = frame.exitType;
    map["exitPressureOver"] = frame.exitPressureOver;
    map["exitPressureUnder"] = frame.exitPressureUnder;
    map["exitFlowOver"] = frame.exitFlowOver;
    map["exitFlowUnder"] = frame.exitFlowUnder;
    map["exitWeight"] = frame.exitWeight;

    // Limiter
    map["maxFlowOrPressure"] = frame.maxFlowOrPressure;
    map["maxFlowOrPressureRange"] = frame.maxFlowOrPressureRange;

    // Popup
    map["popup"] = frame.popup;

    return map;
}

int ProfileManager::frameCount() const {
    return static_cast<int>(m_currentProfile.steps().size());
}


// === Flow calibration ===

void ProfileManager::applyFlowCalibration() {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    double multiplier = m_settings->effectiveFlowCalibration(m_baseProfileName);
    m_device->setFlowCalibrationMultiplier(multiplier);
}


// === Private helpers ===

void ProfileManager::loadDefaultProfile() {
    m_currentProfile = Profile();
    m_currentProfile.setTitle("Default");
    m_currentProfile.setTargetWeight(36.0);

    // Create a simple default profile
    ProfileFrame preinfusion;
    preinfusion.name = "Preinfusion";
    preinfusion.pump = "pressure";
    preinfusion.pressure = 4.0;
    preinfusion.temperature = 93.0;
    preinfusion.seconds = 10.0;
    preinfusion.exitIf = true;
    preinfusion.exitType = "pressure_over";
    preinfusion.exitPressureOver = 3.0;

    ProfileFrame extraction;
    extraction.name = "Extraction";
    extraction.pump = "pressure";
    extraction.pressure = 9.0;
    extraction.temperature = 93.0;
    extraction.seconds = 30.0;

    if (!m_currentProfile.addStep(preinfusion)) {
        qWarning() << "loadDefaultProfile: failed to add preinfusion frame";
    }
    if (!m_currentProfile.addStep(extraction)) {
        qWarning() << "loadDefaultProfile: failed to add extraction frame";
    }
    m_currentProfile.setPreinfuseFrameCount(1);

    if (m_settings) {
        m_settings->setSelectedFavoriteProfile(-1);  // Default profile, not in favorites
        m_settings->setBrewYieldOverride(m_currentProfile.targetWeight());
        m_settings->setTemperatureOverride(m_currentProfile.espressoTemperature());
    }
}

void ProfileManager::updateProfileKnowledgeBaseId()
{
    // If already set (persisted in profile JSON from a previous Save As), keep it
    if (!m_currentProfile.knowledgeBaseId().isEmpty()) return;

    m_currentProfile.setKnowledgeBaseId(
        ShotSummarizer::computeProfileKbId(m_currentProfile.title(), m_currentProfile.editorType()));
}

QString ProfileManager::profilesPath() const {
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    path += "/profiles";

    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    return path;
}

QString ProfileManager::userProfilesPath() const {
    QString path = profilesPath() + "/user";
    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    return path;
}

QString ProfileManager::downloadedProfilesPath() const {
    QString path = profilesPath() + "/downloaded";
    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    return path;
}

double ProfileManager::getGroupTemperature() const {
    if (m_settings && m_settings->hasTemperatureOverride()) {
        double temp = m_settings->temperatureOverride();
        qDebug() << "getGroupTemperature: using override" << temp << "C";
        return temp;
    }
    return m_currentProfile.espressoTemperature();
}

void ProfileManager::migrateProfileFolders() {
    QString basePath = profilesPath();
    QString userPath = basePath + "/user";
    QString downloadedPath = basePath + "/downloaded";

    // Check if migration already done (user folder exists and has content, or we've done it before)
    QDir userDir(userPath);
    QDir downloadedDir(downloadedPath);

    // If user folder already exists, migration was already done
    if (userDir.exists()) {
        // Just ensure downloaded folder exists too
        if (!downloadedDir.exists()) {
            downloadedDir.mkpath(".");
        }
        return;
    }

    qDebug() << "Migrating profile folders...";

    // Create both folders
    userDir.mkpath(".");
    downloadedDir.mkpath(".");

    // Move all existing .json files (except _current.json) from profiles/ to profiles/user/
    QDir baseDir(basePath);
    QStringList filters;
    filters << "*.json";
    QStringList files = baseDir.entryList(filters, QDir::Files);

    for (const QString& file : files) {
        if (file == "_current.json") {
            continue;  // Skip the temp file
        }

        QString srcPath = basePath + "/" + file;
        QString dstPath = userPath + "/" + file;

        if (QFile::rename(srcPath, dstPath)) {
            qDebug() << "Migrated profile:" << file;
        } else {
            qWarning() << "Failed to migrate profile:" << file;
        }
    }

    qDebug() << "Profile folder migration complete";
}

void ProfileManager::migrateProfileFormat() {
    // One-time migration: resave user/downloaded profiles in the unified de1app-compatible
    // JSON format so they can be shared directly with de1app users.
    if (m_settings && m_settings->value("profile_format_migrated", false).toBool()) {
        return;  // Already done
    }

    qDebug() << "Migrating profile JSON format to de1app-compatible v2...";
    int migrated = 0;
    int failed = 0;

    // Helper: check and resave a single file if it lacks the "version" field
    auto migrateFile = [&](const QString& filePath) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning() << "migrateProfileFormat: Cannot open profile:" << filePath
                       << "-" << file.errorString();
            failed++;
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (doc.isNull()) {
            qWarning() << "migrateProfileFormat: Invalid JSON in profile:" << filePath;
            failed++;
            return;
        }

        QJsonObject obj = doc.object();
        if (obj.contains("version")) return;  // Already in v2 format

        Profile profile = Profile::fromJson(doc);
        if (profile.title().isEmpty() || profile.steps().isEmpty()) {
            qWarning() << "migrateProfileFormat: Profile has empty title or steps:" << filePath;
            failed++;
            return;
        }

        if (profile.saveToFile(filePath)) {
            migrated++;
        } else {
            qWarning() << "migrateProfileFormat: Failed to write migrated profile:" << filePath;
            failed++;
        }
    };

    // Migrate user profiles
    QDir userDir(userProfilesPath());
    for (const QString& file : userDir.entryList({"*.json"}, QDir::Files)) {
        if (file == "_current.json") continue;
        migrateFile(userDir.filePath(file));
    }

    // Migrate downloaded profiles
    QDir downloadedDir(downloadedProfilesPath());
    for (const QString& file : downloadedDir.entryList({"*.json"}, QDir::Files)) {
        migrateFile(downloadedDir.filePath(file));
    }

    // Migrate ProfileStorage profiles (Android SAF)
    if (m_profileStorage && m_profileStorage->isConfigured()) {
        for (const QString& name : m_profileStorage->listProfiles()) {
            QString jsonContent = m_profileStorage->readProfile(name);
            if (jsonContent.isEmpty()) continue;

            QJsonDocument doc = QJsonDocument::fromJson(jsonContent.toUtf8());
            if (doc.isNull()) {
                qWarning() << "migrateProfileFormat: Invalid JSON in SAF profile:" << name;
                failed++;
                continue;
            }

            QJsonObject obj = doc.object();
            if (obj.contains("version")) continue;

            Profile profile = Profile::fromJson(doc);
            if (profile.title().isEmpty() || profile.steps().isEmpty()) {
                qWarning() << "migrateProfileFormat: SAF profile has empty title or steps:" << name;
                failed++;
                continue;
            }

            if (m_profileStorage->writeProfile(name, profile.toJsonString())) {
                migrated++;
            } else {
                qWarning() << "migrateProfileFormat: Failed to write SAF profile:" << name;
                failed++;
            }
        }
    }

    if (failed > 0) {
        qWarning() << "Profile format migration incomplete:" << migrated << "updated,"
                   << failed << "failed. Will retry on next launch.";
    } else {
        if (m_settings) {
            m_settings->setValue("profile_format_migrated", true);
        }
        qDebug() << "Profile format migration complete:" << migrated << "profiles updated";
    }
}

void ProfileManager::migrateRecipeFrames() {
    // One-time migration: regenerate frames for recipe-mode profiles so that
    // per-frame weight exits (e.g. infuseWeight) are actually applied.
    // Previously, infuseByWeight=false caused infuseWeight to be silently ignored
    // when generating frames, leaving exitWeight=0 on the Infusing frame.
    if (m_settings && m_settings->value("recipe_frames_migrated", false).toBool()) {
        return;
    }

    qDebug() << "Migrating recipe-mode profile frames...";
    int migrated = 0;
    int failed = 0;

    auto migrateFile = [&](const QString& filePath) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning() << "migrateRecipeFrames: cannot open" << filePath;
            failed++;
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (doc.isNull()) return;

        QJsonObject obj = doc.object();
        // Identify recipe profiles: check is_recipe_mode (legacy) or recipe block (current)
        if (!obj.value("is_recipe_mode").toBool() && !obj.contains("recipe")) return;

        Profile profile = Profile::fromJson(doc);
        if (profile.title().isEmpty()) return;

        profile.regenerateFromRecipe();

        // Guard against degenerate regeneration result
        const QList<ProfileFrame>& steps = profile.steps();
        if (steps.isEmpty() || (steps.size() == 1 && steps.first().name == "empty")) {
            qWarning() << "migrateRecipeFrames: regeneration produced degenerate result for"
                       << filePath << "- skipping to avoid data loss";
            failed++;
            return;
        }

        if (profile.saveToFile(filePath)) {
            qDebug() << "migrateRecipeFrames: regenerated" << filePath;
            migrated++;
        } else {
            qWarning() << "migrateRecipeFrames: failed to write" << filePath;
            failed++;
        }
    };

    QDir userDir(userProfilesPath());
    for (const QString& file : userDir.entryList({"*.json"}, QDir::Files)) {
        if (file == "_current.json") continue;
        migrateFile(userDir.filePath(file));
    }

    QDir downloadedDir(downloadedProfilesPath());
    for (const QString& file : downloadedDir.entryList({"*.json"}, QDir::Files)) {
        migrateFile(downloadedDir.filePath(file));
    }

    if (m_profileStorage && m_profileStorage->isConfigured()) {
        for (const QString& name : m_profileStorage->listProfiles()) {
            QString jsonContent = m_profileStorage->readProfile(name);
            if (jsonContent.isEmpty()) continue;
            QJsonDocument doc = QJsonDocument::fromJson(jsonContent.toUtf8());
            QJsonObject sObj = doc.object();
            if (doc.isNull() || (!sObj.value("is_recipe_mode").toBool() && !sObj.contains("recipe"))) continue;

            Profile profile = Profile::fromJson(doc);
            if (profile.title().isEmpty()) continue;

            profile.regenerateFromRecipe();

            // Guard against degenerate regeneration result
            const QList<ProfileFrame>& steps = profile.steps();
            if (steps.isEmpty() || (steps.size() == 1 && steps.first().name == "empty")) {
                qWarning() << "migrateRecipeFrames: degenerate result for SAF profile:" << name;
                failed++;
                continue;
            }

            if (m_profileStorage->writeProfile(name, profile.toJsonString())) {
                migrated++;
            } else {
                qWarning() << "migrateRecipeFrames: failed to write SAF profile:" << name;
                failed++;
            }
        }
    }

    if (failed > 0) {
        qWarning() << "Recipe frame migration incomplete:" << migrated << "updated,"
                   << failed << "failed. Will retry on next launch.";
    } else {
        if (m_settings) m_settings->setValue("recipe_frames_migrated", true);
        qDebug() << "Recipe frame migration complete:" << migrated << "profiles updated";
    }
}

void ProfileManager::migrateReadOnlyProfiles() {
    // One-time migration: rename user profiles that shadow built-in profiles,
    // and detect broken D-Flow/A-Flow profiles with wrong frame counts.
    // v2: re-run for 1.6.1 — the v1 migration missed profiles whose format was
    // changed by migrateProfileFormat() before compareProfiles() ran.
    if (m_settings && m_settings->value("readonly_profiles_migrated_v2", false).toBool()) {
        return;
    }

    int renamed = 0;
    int broken = 0;
    int failed = 0;

    // Helper: migrate a single profile file
    auto migrateFile = [&](const QString& filePath, const QString& filename,
                           bool isStorage) {
        // Load profile
        Profile profile;
        if (isStorage) {
            QString jsonContent = m_profileStorage ? m_profileStorage->readProfile(filename) : QString();
            if (jsonContent.isEmpty()) return;
            profile = Profile::loadFromJsonString(jsonContent);
        } else {
            profile = Profile::loadFromFile(filePath);
        }
        if (profile.title().isEmpty()) return;

        bool needsSave = false;
        QString newFilename = filename;
        QString newTitle = profile.title();

        // 4b: Handle user copies that shadow built-in profiles
        if (isBuiltInFilename(filename)) {
            // Compare user copy against built-in using the unified comparison
            // (same logic as de1app import and device migration).
            Profile builtIn = Profile::loadFromFile(QStringLiteral(":/profiles/") + filename + QStringLiteral(".json"));
            bool isModified = !ProfileSaveHelper::compareProfiles(profile, builtIn);
            // compareProfiles doesn't check title — check separately
            if (!isModified && profile.title() != builtIn.title()) isModified = true;

            if (isModified) {
                // User actually changed something — rename to preserve their edits
                newTitle = profile.title() + " (user)";
                profile.setTitle(newTitle);
                profile.setReadOnly(0);  // User copy is editable
                newFilename = titleToFilename(newTitle);
                needsSave = true;

                qDebug() << "migrateReadOnlyProfiles: renamed modified user override:"
                         << filename << "->" << newFilename;
                renamed++;
            } else {
                // Unmodified copy of built-in — just delete it, built-in will take over
                if (isStorage && m_profileStorage && m_profileStorage->isConfigured()) {
                    m_profileStorage->deleteProfile(filename);
                } else {
                    QFile::remove(filePath);
                }
                qDebug() << "migrateReadOnlyProfiles: deleted unmodified shadow of built-in:"
                         << filename;
                return;  // No further processing needed
            }
        }

        // 4c: Fix simple profiles (settings_2a/2b) with stale stored steps.
        // De1app regenerates frames from scalar parameters at upload time — stored
        // steps are irrelevant. Clear them so Decenza regenerates from scalars.
        // Also fix settings_2b profiles where espressoHoldTime was incorrectly
        // populated from flowProfileHoldTime (the old getOrConvertRecipeParams bug).
        QString profileType = profile.profileType();
        bool isSimple = (profileType == QLatin1String("settings_2a") || profileType == QLatin1String("settings_2b"));

        if (isSimple && !profile.steps().isEmpty()) {
            // For settings_2b: if espressoHoldTime is 0 but flowProfileHoldTime is not,
            // the profile was corrupted by the old editor bug. Fix by swapping.
            if (profileType == QLatin1String("settings_2b")) {
                if (qFuzzyIsNull(profile.espressoHoldTime()) && profile.flowProfileHoldTime() > 0) {
                    profile.setEspressoHoldTime(profile.flowProfileHoldTime());
                    qDebug() << "migrateReadOnlyProfiles: fixed settings_2b hold time for" << filename
                             << "from flowProfileHoldTime:" << profile.flowProfileHoldTime();
                }
                if (qFuzzyIsNull(profile.espressoDeclineTime()) && profile.flowProfileDeclineTime() > 0) {
                    profile.setEspressoDeclineTime(profile.flowProfileDeclineTime());
                    qDebug() << "migrateReadOnlyProfiles: fixed settings_2b decline time for" << filename
                             << "from flowProfileDeclineTime:" << profile.flowProfileDeclineTime();
                }
            }

            // Clear stale stored steps — they'll be regenerated from scalars on load
            profile.setSteps({});
            profile.regenerateSimpleFrames();
            needsSave = true;
            qDebug() << "migrateReadOnlyProfiles: regenerated simple profile frames for" << filename;
        }

        // 4d: Detect broken D-Flow/A-Flow profiles (wrong frame count)
        qsizetype stepCount = profile.steps().size();
        bool isDFlow = isDFlowTitle(newTitle);
        bool isAFlow = isAFlowTitle(newTitle);

        if (isDFlow && stepCount != 3) {
            // Strip D-Flow prefix and mark as broken
            QString stripped = newTitle;
            if (stripped.startsWith(QLatin1String("D-Flow / ")))
                stripped = stripped.mid(9);
            else if (stripped.startsWith(QLatin1String("D-Flow /")))
                stripped = stripped.mid(8).trimmed();
            newTitle = stripped + " (broken)";
            profile.setTitle(newTitle);

            newFilename = titleToFilename(newTitle);
            needsSave = true;

            qWarning() << "migrateReadOnlyProfiles: broken D-Flow profile"
                       << filename << "has" << stepCount << "frames (expected 3),"
                       << "renamed to:" << newTitle;
            broken++;
        } else if (isAFlow && stepCount != 9) {
            // Strip A-Flow prefix and mark as broken
            QString stripped = newTitle;
            if (stripped.startsWith(QLatin1String("A-Flow / ")))
                stripped = stripped.mid(9);
            else if (stripped.startsWith(QLatin1String("A-Flow /")))
                stripped = stripped.mid(8).trimmed();
            newTitle = stripped + " (broken)";
            profile.setTitle(newTitle);

            newFilename = titleToFilename(newTitle);
            needsSave = true;

            qWarning() << "migrateReadOnlyProfiles: broken A-Flow profile"
                       << filename << "has" << stepCount << "frames (expected 9),"
                       << "renamed to:" << newTitle;
            broken++;
        }

        if (!needsSave) return;

        // Save with new filename
        bool saved = false;
        if (isStorage && m_profileStorage && m_profileStorage->isConfigured()) {
            saved = m_profileStorage->writeProfile(newFilename, profile.toJsonString());
            if (saved && newFilename != filename) {
                m_profileStorage->deleteProfile(filename);
            }
        } else {
            QString dir = QFileInfo(filePath).absolutePath();
            QString newPath = dir + "/" + newFilename + ".json";
            saved = profile.saveToFile(newPath);
            if (saved && newFilename != filename) {
                QFile::remove(filePath);
            }
        }

        if (!saved) {
            qWarning() << "migrateReadOnlyProfiles: failed to save" << newFilename;
            failed++;
            return;
        }

        // Update favorites and currentProfile AFTER all renames are finalized
        // (fixes issue where shadow-rename + broken-detection would leave
        // favorites pointing to intermediate filename)
        if (newFilename != filename) {
            if (m_settings) {
                if (m_settings->isFavoriteProfile(filename)) {
                    m_settings->updateFavoriteProfile(filename, newFilename, newTitle);
                }
                if (m_settings->currentProfile() == filename) {
                    m_settings->setCurrentProfile(newFilename);
                    qDebug() << "migrateReadOnlyProfiles: updated currentProfile:"
                             << filename << "->" << newFilename;
                }
            }
        }
    };

    // Migrate user profiles (legacy local folder)
    QDir userDir(userProfilesPath());
    QStringList filters;
    filters << "*.json";
    QStringList files = userDir.entryList(filters, QDir::Files);
    for (const QString& file : files) {
        QString name = file.left(file.length() - 5);
        migrateFile(userDir.filePath(file), name, false);
    }

    // Migrate downloaded profiles (legacy local folder)
    QDir downloadedDir(downloadedProfilesPath());
    files = downloadedDir.entryList(filters, QDir::Files);
    for (const QString& file : files) {
        QString name = file.left(file.length() - 5);
        migrateFile(downloadedDir.filePath(file), name, false);
    }

    // Migrate ProfileStorage (SAF on Android)
    if (m_profileStorage && m_profileStorage->isConfigured()) {
        QStringList storageProfiles = m_profileStorage->listProfiles();
        for (const QString& name : storageProfiles) {
            migrateFile(QString(), name, true);
        }
    }

    if (failed > 0) {
        qWarning() << "Read-only profile migration incomplete:" << renamed << "renamed,"
                   << broken << "broken," << failed << "failed. Will retry on next launch.";
    } else {
        if (m_settings) m_settings->setValue("readonly_profiles_migrated_v2", true);
        qDebug() << "Read-only profile migration complete:" << renamed << "renamed,"
                 << broken << "broken profiles detected";

        // Refresh profiles list after migration
        if (renamed > 0 || broken > 0) {
            refreshProfiles();
        }
    }
}


