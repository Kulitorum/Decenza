#include "maincontroller.h"
#include "../core/settings.h"
#include "../ble/de1device.h"
#include "../machine/machinestate.h"
#include "../models/shotdatamodel.h"
#include "../network/visualizeruploader.h"
#include "../network/visualizerimporter.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QVariantMap>
#include <algorithm>

MainController::MainController(Settings* settings, DE1Device* device,
                               MachineState* machineState, ShotDataModel* shotDataModel,
                               QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_device(device)
    , m_machineState(machineState)
    , m_shotDataModel(shotDataModel)
{
    // Set up delayed settings timer (5 seconds after connection)
    m_settingsTimer.setSingleShot(true);
    m_settingsTimer.setInterval(5000);
    connect(&m_settingsTimer, &QTimer::timeout, this, &MainController::applyAllSettings);

    // Connect to shot sample updates
    if (m_device) {
        connect(m_device, &DE1Device::shotSampleReceived,
                this, &MainController::onShotSampleReceived);

        // Start delayed settings timer after device initial settings complete
        connect(m_device, &DE1Device::initialSettingsComplete, this, [this]() {
            m_settingsTimer.start();
        });
    }

    // Connect to machine state events
    if (m_machineState) {
        connect(m_machineState, &MachineState::espressoCycleStarted,
                this, &MainController::onEspressoCycleStarted);
        connect(m_machineState, &MachineState::shotEnded,
                this, &MainController::onShotEnded);
    }

    // Create visualizer uploader and importer
    m_visualizer = new VisualizerUploader(m_settings, this);
    m_visualizerImporter = new VisualizerImporter(this, this);

    // Migrate profile folders (one-time migration for existing users)
    migrateProfileFolders();

    // Load initial profile
    refreshProfiles();

    // Check for temp file (modified profile from previous session)
    QString tempPath = profilesPath() + "/_current.json";
    if (QFile::exists(tempPath)) {
        qDebug() << "Loading modified profile from temp file:" << tempPath;
        m_currentProfile = Profile::loadFromFile(tempPath);
        m_profileModified = true;
        // Get the base profile name from settings
        if (m_settings) {
            m_baseProfileName = m_settings->currentProfile();
        }
        if (m_machineState) {
            m_machineState->setTargetWeight(m_currentProfile.targetWeight());
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
}

QString MainController::currentProfileName() const {
    if (m_profileModified) {
        return "*" + m_currentProfile.title();
    }
    return m_currentProfile.title();
}

double MainController::targetWeight() const {
    return m_currentProfile.targetWeight();
}

void MainController::setTargetWeight(double weight) {
    if (m_currentProfile.targetWeight() != weight) {
        m_currentProfile.setTargetWeight(weight);
        if (m_machineState) {
            m_machineState->setTargetWeight(weight);
        }
        emit targetWeightChanged();
    }
}

QVariantList MainController::availableProfiles() const {
    QVariantList result;
    for (const QString& name : m_availableProfiles) {
        QVariantMap profile;
        profile["name"] = name;  // filename for loading
        profile["title"] = m_profileTitles.value(name, name);  // display title
        result.append(profile);
    }
    return result;
}

QVariantList MainController::selectedProfiles() const {
    QVariantList result;

    // Get selected built-in profile names from settings
    QStringList selectedBuiltIns = m_settings ? m_settings->selectedBuiltInProfiles() : QStringList();

    for (const ProfileInfo& info : m_allProfiles) {
        bool include = false;

        switch (info.source) {
        case ProfileSource::BuiltIn:
            // Only include if selected
            include = selectedBuiltIns.contains(info.filename);
            break;
        case ProfileSource::Downloaded:
        case ProfileSource::UserCreated:
            // Always include user and downloaded profiles
            include = true;
            break;
        }

        if (include) {
            QVariantMap profile;
            profile["name"] = info.filename;
            profile["title"] = info.title;
            profile["beverageType"] = info.beverageType;
            profile["source"] = static_cast<int>(info.source);
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

QVariantList MainController::allBuiltInProfiles() const {
    QVariantList result;

    // Get selected built-in profile names from settings
    QStringList selectedBuiltIns = m_settings ? m_settings->selectedBuiltInProfiles() : QStringList();

    for (const ProfileInfo& info : m_allProfiles) {
        if (info.source == ProfileSource::BuiltIn) {
            QVariantMap profile;
            profile["name"] = info.filename;
            profile["title"] = info.title;
            profile["beverageType"] = info.beverageType;
            profile["source"] = static_cast<int>(info.source);
            profile["isSelected"] = selectedBuiltIns.contains(info.filename);
            result.append(profile);
        }
    }

    return result;
}

QVariantList MainController::cleaningProfiles() const {
    QVariantList result;

    for (const ProfileInfo& info : m_allProfiles) {
        if (info.beverageType == "cleaning") {
            QVariantMap profile;
            profile["name"] = info.filename;
            profile["title"] = info.title;
            profile["beverageType"] = info.beverageType;
            profile["source"] = static_cast<int>(info.source);
            result.append(profile);
        }
    }

    return result;
}

bool MainController::deleteProfile(const QString& filename) {
    // Find the profile info
    ProfileSource source = ProfileSource::BuiltIn;
    for (const ProfileInfo& info : m_allProfiles) {
        if (info.filename == filename) {
            source = info.source;
            break;
        }
    }

    // Cannot delete built-in profiles
    if (source == ProfileSource::BuiltIn) {
        qWarning() << "Cannot delete built-in profile:" << filename;
        return false;
    }

    // Determine the file path based on source
    QString path;
    if (source == ProfileSource::Downloaded) {
        path = downloadedProfilesPath() + "/" + filename + ".json";
    } else if (source == ProfileSource::UserCreated) {
        path = userProfilesPath() + "/" + filename + ".json";
    }

    // Delete the file
    if (QFile::remove(path)) {
        qDebug() << "Deleted profile:" << path;

        // Remove from favorites if it was a favorite
        if (m_settings && m_settings->isFavoriteProfile(filename)) {
            // Find index and remove
            QVariantList favorites = m_settings->favoriteProfiles();
            for (int i = 0; i < favorites.size(); ++i) {
                if (favorites[i].toMap()["filename"].toString() == filename) {
                    m_settings->removeFavoriteProfile(i);
                    break;
                }
            }
        }

        // Refresh the profile list
        refreshProfiles();
        return true;
    } else {
        qWarning() << "Failed to delete profile:" << path;
        return false;
    }
}

QVariantMap MainController::getCurrentProfile() const {
    QVariantMap profile;
    profile["title"] = m_currentProfile.title();
    profile["target_weight"] = m_currentProfile.targetWeight();
    profile["espresso_temperature"] = m_currentProfile.espressoTemperature();
    profile["mode"] = m_currentProfile.mode() == Profile::Mode::FrameBased ? "frame_based" : "direct";

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
        step["max_flow_or_pressure"] = frame.maxFlowOrPressure;
        step["max_flow_or_pressure_range"] = frame.maxFlowOrPressureRange;
        steps.append(step);
    }
    profile["steps"] = steps;

    return profile;
}

void MainController::loadProfile(const QString& profileName) {
    QString path;
    bool found = false;

    // 1. Check user profiles first
    path = userProfilesPath() + "/" + profileName + ".json";
    if (QFile::exists(path)) {
        m_currentProfile = Profile::loadFromFile(path);
        found = true;
    }

    // 2. Check downloaded profiles
    if (!found) {
        path = downloadedProfilesPath() + "/" + profileName + ".json";
        if (QFile::exists(path)) {
            m_currentProfile = Profile::loadFromFile(path);
            found = true;
        }
    }

    // 3. Check built-in profiles
    if (!found) {
        path = ":/profiles/" + profileName + ".json";
        if (QFile::exists(path)) {
            m_currentProfile = Profile::loadFromFile(path);
            found = true;
        }
    }

    // 4. Fall back to default
    if (!found) {
        loadDefaultProfile();
    }

    // Track the base profile name (filename without extension)
    m_baseProfileName = profileName;
    bool wasModified = m_profileModified;
    m_profileModified = false;

    if (m_settings) {
        m_settings->setCurrentProfile(profileName);
    }

    if (m_machineState) {
        m_machineState->setTargetWeight(m_currentProfile.targetWeight());
    }

    // Upload to machine if connected (for frame-based mode)
    if (m_currentProfile.mode() == Profile::Mode::FrameBased) {
        uploadCurrentProfile();
    }

    emit currentProfileChanged();
    emit targetWeightChanged();
    if (wasModified) {
        emit profileModifiedChanged();
    }
}

void MainController::refreshProfiles() {
    m_availableProfiles.clear();
    m_profileTitles.clear();
    m_allProfiles.clear();

    // Helper to load profile metadata
    auto loadProfileMeta = [](const QString& path) -> QPair<QString, QString> {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            QJsonObject obj = doc.object();
            return {obj["title"].toString(), obj["beverage_type"].toString()};
        }
        return {QString(), QString()};
    };

    QStringList filters;
    filters << "*.json";

    // 1. Load built-in profiles (always available)
    QDir builtInDir(":/profiles");
    QStringList files = builtInDir.entryList(filters, QDir::Files);
    for (const QString& file : files) {
        QString name = file.left(file.length() - 5);  // Remove .json
        auto [title, beverageType] = loadProfileMeta(":/profiles/" + file);

        ProfileInfo info;
        info.filename = name;
        info.title = title.isEmpty() ? name : title;
        info.beverageType = beverageType;
        info.source = ProfileSource::BuiltIn;
        m_allProfiles.append(info);

        m_availableProfiles.append(name);
        m_profileTitles[name] = info.title;
    }

    // 2. Load downloaded profiles
    QDir downloadedDir(downloadedProfilesPath());
    files = downloadedDir.entryList(filters, QDir::Files);
    for (const QString& file : files) {
        QString name = file.left(file.length() - 5);
        auto [title, beverageType] = loadProfileMeta(downloadedDir.filePath(file));

        ProfileInfo info;
        info.filename = name;
        info.title = title.isEmpty() ? name : title;
        info.beverageType = beverageType;
        info.source = ProfileSource::Downloaded;
        m_allProfiles.append(info);

        if (!m_availableProfiles.contains(name)) {
            m_availableProfiles.append(name);
            m_profileTitles[name] = info.title;
        }
    }

    // 3. Load user-created profiles
    QDir userDir(userProfilesPath());
    files = userDir.entryList(filters, QDir::Files);
    for (const QString& file : files) {
        QString name = file.left(file.length() - 5);
        auto [title, beverageType] = loadProfileMeta(userDir.filePath(file));

        ProfileInfo info;
        info.filename = name;
        info.title = title.isEmpty() ? name : title;
        info.beverageType = beverageType;
        info.source = ProfileSource::UserCreated;
        m_allProfiles.append(info);

        if (!m_availableProfiles.contains(name)) {
            m_availableProfiles.append(name);
            m_profileTitles[name] = info.title;
        }
    }

    emit profilesChanged();
}

void MainController::uploadCurrentProfile() {
    if (m_device && m_device->isConnected()) {
        m_device->uploadProfile(m_currentProfile);
    }
}

void MainController::uploadProfile(const QVariantMap& profileData) {
    // Update current profile from QML data
    if (profileData.contains("title")) {
        m_currentProfile.setTitle(profileData["title"].toString());
    }
    if (profileData.contains("target_weight")) {
        m_currentProfile.setTargetWeight(profileData["target_weight"].toDouble());
        if (m_machineState) {
            m_machineState->setTargetWeight(m_currentProfile.targetWeight());
        }
    }

    // Update steps/frames
    if (profileData.contains("steps")) {
        m_currentProfile.steps().clear();
        QVariantList steps = profileData["steps"].toList();
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
            frame.maxFlowOrPressure = step["max_flow_or_pressure"].toDouble();
            frame.maxFlowOrPressureRange = step["max_flow_or_pressure_range"].toDouble();
            m_currentProfile.addStep(frame);
        }
    }

    // Mark as modified
    if (!m_profileModified) {
        m_profileModified = true;
        emit profileModifiedChanged();
    }

    // Save to temp file for persistence across restarts
    QString tempPath = profilesPath() + "/_current.json";
    m_currentProfile.saveToFile(tempPath);
    qDebug() << "Saved modified profile to temp file:" << tempPath;

    // Upload to machine
    uploadCurrentProfile();

    emit currentProfileChanged();
}

bool MainController::saveProfile(const QString& filename) {
    // Always save to user profiles folder
    QString path = userProfilesPath() + "/" + filename + ".json";
    bool success = m_currentProfile.saveToFile(path);
    if (success) {
        qDebug() << "Saved profile to:" << path;

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
    } else {
        qWarning() << "Failed to save profile to:" << path;
    }
    return success;
}

void MainController::markProfileClean() {
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

bool MainController::saveProfileAs(const QString& filename, const QString& title) {
    // Remember old filename for favorite update
    QString oldFilename = m_baseProfileName;

    // Update the profile title
    m_currentProfile.setTitle(title);

    // Always save to user profiles folder
    QString path = userProfilesPath() + "/" + filename + ".json";
    bool success = m_currentProfile.saveToFile(path);
    if (success) {
        qDebug() << "Saved profile as:" << path;
        m_baseProfileName = filename;
        if (m_settings) {
            m_settings->setCurrentProfile(filename);
            // Always update favorite (handles both filename and title changes)
            if (!oldFilename.isEmpty()) {
                m_settings->updateFavoriteProfile(oldFilename, filename, title);
            }
        }
        markProfileClean();
        refreshProfiles();
        emit currentProfileChanged();
    } else {
        qWarning() << "Failed to save profile to:" << path;
    }
    return success;
}

QString MainController::titleToFilename(const QString& title) const {
    // Replace accented characters
    QString result = title;
    result.replace(QChar(0xE9), 'e');  // é
    result.replace(QChar(0xE8), 'e');  // è
    result.replace(QChar(0xEA), 'e');  // ê
    result.replace(QChar(0xEB), 'e');  // ë
    result.replace(QChar(0xE1), 'a');  // á
    result.replace(QChar(0xE0), 'a');  // à
    result.replace(QChar(0xE2), 'a');  // â
    result.replace(QChar(0xE4), 'a');  // ä
    result.replace(QChar(0xED), 'i');  // í
    result.replace(QChar(0xEC), 'i');  // ì
    result.replace(QChar(0xEE), 'i');  // î
    result.replace(QChar(0xEF), 'i');  // ï
    result.replace(QChar(0xF3), 'o');  // ó
    result.replace(QChar(0xF2), 'o');  // ò
    result.replace(QChar(0xF4), 'o');  // ô
    result.replace(QChar(0xF6), 'o');  // ö
    result.replace(QChar(0xFA), 'u');  // ú
    result.replace(QChar(0xF9), 'u');  // ù
    result.replace(QChar(0xFB), 'u');  // û
    result.replace(QChar(0xFC), 'u');  // ü
    result.replace(QChar(0xF1), 'n');  // ñ
    result.replace(QChar(0xE7), 'c');  // ç

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

bool MainController::profileExists(const QString& filename) const {
    QString path = profilesPath() + "/" + filename + ".json";
    return QFile::exists(path);
}

void MainController::applySteamSettings() {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    // Send shot settings (includes steam temp/timeout)
    m_device->setShotSettings(
        m_settings->steamTemperature(),
        m_settings->steamTimeout(),
        m_settings->waterTemperature(),
        m_settings->waterVolume(),
        93.0  // Group temp (could be from settings too)
    );

    // Send steam flow via MMR
    m_device->writeMMR(0x803828, m_settings->steamFlow());
}

void MainController::applyHotWaterSettings() {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    // Send shot settings (includes water temp/volume)
    m_device->setShotSettings(
        m_settings->steamTemperature(),
        m_settings->steamTimeout(),
        m_settings->waterTemperature(),
        m_settings->waterVolume(),
        93.0  // Group temp
    );
}

void MainController::applyFlushSettings() {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    // Flush flow rate at MMR 0x803840, value × 10
    // Flush timeout at MMR 0x803848, value × 10
    int flowValue = static_cast<int>(m_settings->flushFlow() * 10);
    int secondsValue = static_cast<int>(m_settings->flushSeconds() * 10);

    m_device->writeMMR(0x803840, flowValue);
    m_device->writeMMR(0x803848, secondsValue);
}

void MainController::applyAllSettings() {
    // 1. Upload current profile (espresso)
    if (m_currentProfile.mode() == Profile::Mode::FrameBased) {
        uploadCurrentProfile();
    }

    // 2. Apply steam settings
    applySteamSettings();

    // 3. Apply hot water settings
    applyHotWaterSettings();

    // 4. Apply flush settings
    applyFlushSettings();
}

void MainController::setSteamTemperatureImmediate(double temp) {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    m_settings->setSteamTemperature(temp);

    // Send all shot settings with updated temperature
    m_device->setShotSettings(
        temp,
        m_settings->steamTimeout(),
        m_settings->waterTemperature(),
        m_settings->waterVolume(),
        93.0
    );

    qDebug() << "Steam temperature set to:" << temp;
}

void MainController::setSteamFlowImmediate(int flow) {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    m_settings->setSteamFlow(flow);

    // Send steam flow via MMR (can be changed in real-time)
    m_device->writeMMR(0x803828, flow);

    qDebug() << "Steam flow set to:" << flow;
}

void MainController::setSteamTimeoutImmediate(int timeout) {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    m_settings->setSteamTimeout(timeout);

    // Send all shot settings with updated timeout
    m_device->setShotSettings(
        m_settings->steamTemperature(),
        timeout,
        m_settings->waterTemperature(),
        m_settings->waterVolume(),
        93.0
    );

    qDebug() << "Steam timeout set to:" << timeout;
}

void MainController::startCalibrationDispense(double flowRate, double targetWeight) {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    // Create a simple calibration profile with a single flow-controlled frame
    Profile calibrationProfile;
    calibrationProfile.setTitle("Calibration");
    calibrationProfile.setTargetWeight(targetWeight);
    calibrationProfile.setMode(Profile::Mode::FrameBased);

    // Single frame: flow control at the target flow rate
    // Use volume limit so DE1 stops based on its own flow sensor (what we're calibrating)
    ProfileFrame frame;
    frame.name = "Calibration";
    frame.pump = "flow";           // Flow control mode
    frame.flow = flowRate;         // Target flow rate in mL/s
    frame.temperature = m_settings->waterTemperature();  // Use hot water temp
    frame.sensor = "water";        // Use mix temp sensor (not basket/coffee)
    frame.transition = "fast";     // Instant transition
    frame.seconds = 120.0;         // 2 minutes max timeout
    frame.volume = targetWeight;   // DE1 stops when its flow sensor thinks this much dispensed
    frame.pressure = 0;            // Not used in flow mode
    frame.maxFlowOrPressure = 0;   // No limiter needed

    calibrationProfile.addStep(frame);
    calibrationProfile.setPreinfuseFrameCount(0);  // No preinfusion

    // Disable stop-at-weight during calibration - let DE1's volume limit stop instead
    // Set a very high target so app's stop-at-weight doesn't interfere
    if (m_machineState) {
        m_machineState->setTargetWeight(999);
    }

    // Enter calibration mode (prevents navigation to espresso page)
    m_calibrationMode = true;
    emit calibrationModeChanged();

    // Tare the scale for the user before starting
    if (m_machineState) {
        m_machineState->tareScale();
    }

    // Upload calibration profile (user must press espresso button on DE1)
    m_device->uploadProfile(calibrationProfile);

    qDebug() << "=== CALIBRATION READY: flow" << flowRate << "mL/s, target" << targetWeight << "g - press espresso button ===";
}

void MainController::startVerificationDispense(double targetWeight) {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    // Create verification profile - uses FlowScale (with calibration factor) to stop
    Profile verificationProfile;
    verificationProfile.setTitle("Verification");
    verificationProfile.setTargetWeight(targetWeight);
    verificationProfile.setMode(Profile::Mode::FrameBased);

    // Single frame: flow control at medium rate, NO volume limit
    // FlowScale's calibrated weight will trigger stop-at-weight
    ProfileFrame frame;
    frame.name = "Verification";
    frame.pump = "flow";
    frame.flow = 6.0;  // Medium flow rate
    frame.temperature = m_settings->waterTemperature();
    frame.sensor = "water";
    frame.transition = "fast";
    frame.seconds = 120.0;  // Long timeout - FlowScale will stop it
    frame.volume = 0;       // NO volume limit - let FlowScale stop
    frame.pressure = 0;
    frame.maxFlowOrPressure = 0;

    verificationProfile.addStep(frame);
    verificationProfile.setPreinfuseFrameCount(0);

    // Enable stop-at-weight using FlowScale's calibrated weight
    if (m_machineState) {
        m_machineState->setTargetWeight(targetWeight);
    }

    // Enter calibration mode (prevents navigation)
    m_calibrationMode = true;
    emit calibrationModeChanged();

    // Tare the scale
    if (m_machineState) {
        m_machineState->tareScale();
    }

    // Upload profile
    m_device->uploadProfile(verificationProfile);

    qDebug() << "=== VERIFICATION READY: target" << targetWeight << "g using FlowScale - press espresso button ===";
}

void MainController::restoreCurrentProfile() {
    // Exit calibration mode
    m_calibrationMode = false;
    emit calibrationModeChanged();

    // Re-upload the user's actual profile after calibration
    if (m_device && m_device->isConnected()) {
        uploadCurrentProfile();

        // Also restore the target weight from the profile
        if (m_machineState) {
            m_machineState->setTargetWeight(m_currentProfile.targetWeight());
        }
    }
    qDebug() << "=== RESTORED PROFILE:" << m_currentProfile.title() << "===";
}

void MainController::onEspressoCycleStarted() {
    // Clear the graph when entering espresso preheating (new cycle from idle)
    // This preserves preheating data since we only clear at cycle start
    m_shotStartTime = 0;
    m_extractionStarted = false;
    m_lastFrameNumber = -1;
    m_tareDone = true;  // We tare immediately now, not at frame 0
    if (m_shotDataModel) {
        m_shotDataModel->clear();
    }
    // Tare scale immediately at cycle start (before stop-at-weight can trigger)
    // The cup is already on the scale, so we need to zero it now
    if (m_machineState) {
        m_machineState->tareScale();
    }
    qDebug() << "=== ESPRESSO CYCLE STARTED (graph cleared, scale tared) ===";
}

void MainController::onShotEnded() {
    // Upload to visualizer.coffee if enabled - only for espresso shots
    if (m_extractionStarted && m_settings && m_settings->visualizerAutoUpload() && m_shotDataModel && m_visualizer) {
        double duration = m_shotDataModel->maxTime();

        // Get final weight from shot data
        const auto& weightData = m_shotDataModel->weightData();
        double finalWeight = 0;
        if (!weightData.isEmpty()) {
            finalWeight = weightData.last().y() * 5.0;  // Undo the /5 scaling
        }

        double doseWeight = m_settings->targetWeight();  // Use target weight as dose

        qDebug() << "MainController: Shot ended, uploading to visualizer -"
                 << "Profile:" << m_currentProfile.title()
                 << "Duration:" << duration << "s"
                 << "Weight:" << finalWeight << "g";

        m_visualizer->uploadShot(m_shotDataModel, &m_currentProfile, duration, finalWeight, doseWeight);
    }

    // Note: Don't reset m_extractionStarted here - it's reset in onEspressoCycleStarted
    // Resetting here causes duplicate "extraction started" markers when entering Ending phase
}

void MainController::onShotSampleReceived(const ShotSample& sample) {
    if (!m_shotDataModel || !m_machineState) return;

    MachineState::Phase phase = m_machineState->phase();

    // Forward flow samples to MachineState for FlowScale during any dispensing phase
    bool isDispensingPhase = (phase == MachineState::Phase::Preinfusion ||
                              phase == MachineState::Phase::Pouring ||
                              phase == MachineState::Phase::Steaming ||
                              phase == MachineState::Phase::HotWater ||
                              phase == MachineState::Phase::Flushing);

    if (isDispensingPhase && m_lastSampleTime > 0) {
        double deltaTime = sample.timer - m_lastSampleTime;
        if (deltaTime > 0 && deltaTime < 1.0) {
            m_machineState->onFlowSample(sample.groupFlow, deltaTime);
        }
    }
    m_lastSampleTime = sample.timer;

    // Record shot data only during espresso phases
    bool isEspressoPhase = (phase == MachineState::Phase::EspressoPreheating ||
                           phase == MachineState::Phase::Preinfusion ||
                           phase == MachineState::Phase::Pouring ||
                           phase == MachineState::Phase::Ending);

    if (!isEspressoPhase) {
        m_shotStartTime = 0;  // Reset for next shot
        m_extractionStarted = false;
        return;
    }

    // First sample of this espresso cycle - set the base time
    if (m_shotStartTime == 0) {
        m_shotStartTime = sample.timer;
        m_lastSampleTime = sample.timer;
        qDebug() << "=== ESPRESSO PREHEATING STARTED ===";
    }

    double time = sample.timer - m_shotStartTime;

    // Mark when extraction actually starts (transition from preheating to preinfusion/pouring)
    bool isExtracting = (phase == MachineState::Phase::Preinfusion ||
                        phase == MachineState::Phase::Pouring ||
                        phase == MachineState::Phase::Ending);

    if (isExtracting && !m_extractionStarted) {
        m_extractionStarted = true;
        m_shotDataModel->markExtractionStart(time);
        qDebug() << "=== EXTRACTION STARTED at" << time << "s ===";
    }

    // Detect frame changes and add markers with frame names from profile
    // Only track during actual extraction phases (not preheating - frame numbers are unreliable then)
    if (isExtracting && sample.frameNumber >= 0 && sample.frameNumber != m_lastFrameNumber) {
        QString frameName;
        int frameIndex = sample.frameNumber;

        // Look up frame name from current profile
        const auto& steps = m_currentProfile.steps();
        if (frameIndex >= 0 && frameIndex < steps.size()) {
            frameName = steps[frameIndex].name;
        }

        // Fall back to frame number if no name
        if (frameName.isEmpty()) {
            frameName = QString("F%1").arg(frameIndex);
        }

        m_shotDataModel->addPhaseMarker(time, frameName, frameIndex);
        m_lastFrameNumber = sample.frameNumber;
        m_currentFrameName = frameName;  // Store for accessibility QML binding

        qDebug() << "Frame change:" << frameIndex << "->" << frameName << "at" << time << "s";

        // Accessibility: notify of frame change for tick sound
        emit frameChanged(frameIndex, frameName);
    }

    // Determine active pump mode for current frame (to show only active goal curve)
    double pressureGoal = sample.setPressureGoal;
    double flowGoal = sample.setFlowGoal;
    {
        int fi = sample.frameNumber;
        const auto& steps = m_currentProfile.steps();
        if (fi >= 0 && fi < steps.size()) {
            if (steps[fi].isFlowControl()) {
                pressureGoal = 0;  // Flow mode - hide pressure goal
            } else {
                flowGoal = 0;      // Pressure mode - hide flow goal
            }
        }
    }

    // Add sample data
    m_shotDataModel->addSample(time, sample.groupPressure,
                               sample.groupFlow, sample.headTemp,
                               pressureGoal, flowGoal, sample.setTempGoal,
                               sample.frameNumber);

    // Detailed logging for development (reduce frequency)
    static int logCounter = 0;
    if (++logCounter % 10 == 0) {
        qDebug().nospace()
            << "SHOT [" << QString::number(time, 'f', 1) << "s] "
            << "F#" << sample.frameNumber << " "
            << "P:" << QString::number(sample.groupPressure, 'f', 2) << " "
            << "F:" << QString::number(sample.groupFlow, 'f', 2) << " "
            << "T:" << QString::number(sample.headTemp, 'f', 1);
    }
}

void MainController::onScaleWeightChanged(double weight) {
    if (!m_shotDataModel || !m_machineState) return;

    // Only record weight during espresso phases
    MachineState::Phase phase = m_machineState->phase();
    bool isEspressoPhase = (phase == MachineState::Phase::EspressoPreheating ||
                           phase == MachineState::Phase::Preinfusion ||
                           phase == MachineState::Phase::Pouring ||
                           phase == MachineState::Phase::Ending);

    if (!isEspressoPhase) return;

    double time = m_machineState->shotTime();
    m_shotDataModel->addWeightSample(time, weight, 0);
}

void MainController::loadDefaultProfile() {
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

    m_currentProfile.addStep(preinfusion);
    m_currentProfile.addStep(extraction);
    m_currentProfile.setPreinfuseFrameCount(1);
}

QString MainController::profilesPath() const {
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    path += "/profiles";

    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    return path;
}

QString MainController::userProfilesPath() const {
    QString path = profilesPath() + "/user";
    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    return path;
}

QString MainController::downloadedProfilesPath() const {
    QString path = profilesPath() + "/downloaded";
    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    return path;
}

void MainController::migrateProfileFolders() {
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
