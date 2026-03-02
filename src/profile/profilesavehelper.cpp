#include "profilesavehelper.h"
#include "../controllers/maincontroller.h"
#include "../core/profilestorage.h"
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QDebug>

ProfileSaveHelper::ProfileSaveHelper(MainController* controller, QObject* parent)
    : QObject(parent)
    , m_controller(controller)
{
}

bool ProfileSaveHelper::compareProfiles(const Profile& a, const Profile& b) const
{
    // Compare profile-level fields
    if (qAbs(a.maximumPressure() - b.maximumPressure()) > 0.1) return false;
    if (qAbs(a.maximumFlow() - b.maximumFlow()) > 0.1) return false;
    if (qAbs(a.minimumPressure() - b.minimumPressure()) > 0.1) return false;
    if (qAbs(a.tankDesiredWaterTemperature() - b.tankDesiredWaterTemperature()) > 0.1) return false;
    if (qAbs(a.maximumFlowRangeAdvanced() - b.maximumFlowRangeAdvanced()) > 0.1) return false;
    if (qAbs(a.maximumPressureRangeAdvanced() - b.maximumPressureRangeAdvanced()) > 0.1) return false;

    const auto& stepsA = a.steps();
    const auto& stepsB = b.steps();

    if (stepsA.size() != stepsB.size()) {
        return false;
    }

    for (int i = 0; i < stepsA.size(); i++) {
        const ProfileFrame& fa = stepsA[i];
        const ProfileFrame& fb = stepsB[i];

        // Compare all frame parameters that affect extraction
        if (qAbs(fa.temperature - fb.temperature) > 0.1) return false;
        if (fa.sensor != fb.sensor) return false;
        if (fa.pump != fb.pump) return false;
        if (fa.transition != fb.transition) return false;
        if (qAbs(fa.pressure - fb.pressure) > 0.1) return false;
        if (qAbs(fa.flow - fb.flow) > 0.1) return false;
        if (qAbs(fa.seconds - fb.seconds) > 0.1) return false;
        if (qAbs(fa.volume - fb.volume) > 0.1) return false;

        // Exit conditions
        if (fa.exitIf != fb.exitIf) return false;
        if (fa.exitIf) {
            if (fa.exitType != fb.exitType) return false;
            if (qAbs(fa.exitPressureOver - fb.exitPressureOver) > 0.1) return false;
            if (qAbs(fa.exitPressureUnder - fb.exitPressureUnder) > 0.1) return false;
            if (qAbs(fa.exitFlowOver - fb.exitFlowOver) > 0.1) return false;
            if (qAbs(fa.exitFlowUnder - fb.exitFlowUnder) > 0.1) return false;
        }

        // Weight exit (independent of exitIf)
        if (qAbs(fa.exitWeight - fb.exitWeight) > 0.1) return false;

        // Limiter
        if (qAbs(fa.maxFlowOrPressure - fb.maxFlowOrPressure) > 0.1) return false;
        if (qAbs(fa.maxFlowOrPressureRange - fb.maxFlowOrPressureRange) > 0.1) return false;

        // Popup notification
        if (fa.popup != fb.popup) return false;
    }

    return true;
}

QVariantMap ProfileSaveHelper::checkProfileStatus(const QString& profileTitle, const Profile* incomingProfile)
{
    QVariantMap result;
    result["exists"] = false;
    result["identical"] = false;
    result["source"] = "";
    result["filename"] = "";

    if (!m_controller) {
        return result;
    }

    QString filename = titleToFilename(profileTitle);
    result["filename"] = filename;

    ProfileStorage* storage = m_controller->profileStorage();

    // Check external/downloaded storage first
    if (storage && storage->isConfigured() && storage->profileExists(filename)) {
        result["exists"] = true;
        result["source"] = "D";  // Downloaded
    }

    // Check local downloaded folder
    QString downloadedPath = ProfileSaveHelper::downloadedProfilesPath() + "/" + filename + ".json";
    if (QFile::exists(downloadedPath)) {
        result["exists"] = true;
        result["source"] = "D";
    }

    // Check built-in profiles
    QString builtinPath = ":/profiles/" + filename + ".json";
    if (QFile::exists(builtinPath)) {
        result["exists"] = true;
        if (result["source"].toString().isEmpty()) {
            result["source"] = "B";  // Built-in (only if not already found as Downloaded)
        }
    }

    // If exists and we have incoming profile, compare frames
    if (result["exists"].toBool() && incomingProfile && incomingProfile->isValid()) {
        Profile localProfile = loadLocalProfile(filename);
        if (localProfile.isValid()) {
            bool identical = compareProfiles(*incomingProfile, localProfile);
            result["identical"] = identical;
        }
    }

    return result;
}

Profile ProfileSaveHelper::loadLocalProfile(const QString& filename) const
{
    ProfileStorage* storage = m_controller ? m_controller->profileStorage() : nullptr;

    // Try profile storage first
    if (storage && storage->isConfigured() && storage->profileExists(filename)) {
        QString content = storage->readProfile(filename);
        if (!content.isEmpty()) {
            return Profile::loadFromJsonString(content);
        }
    }

    // Try local downloaded folder
    QString localPath = ProfileSaveHelper::downloadedProfilesPath() + "/" + filename + ".json";
    if (QFile::exists(localPath)) {
        return Profile::loadFromFile(localPath);
    }

    // Try built-in profiles
    QString builtinPath = ":/profiles/" + filename + ".json";
    if (QFile::exists(builtinPath)) {
        return Profile::loadFromFile(builtinPath);
    }

    return Profile();  // Empty/invalid profile
}

int ProfileSaveHelper::saveProfile(const Profile& profile, const QString& filename)
{
    QString fullPath = ProfileSaveHelper::downloadedProfilesPath() + "/" + filename + ".json";

    // Check for duplicates in downloaded folder
    if (QFile::exists(fullPath)) {
        m_pendingProfile = profile;
        m_pendingFilename = filename;
        qDebug() << "ProfileSaveHelper: Duplicate found for" << profile.title();
        emit duplicateFound(profile.title(), filename);
        return 0;  // Waiting for user decision
    }

    // Also check built-in profiles
    QString builtinPath = ":/profiles/" + filename + ".json";
    if (QFile::exists(builtinPath)) {
        m_pendingProfile = profile;
        m_pendingFilename = filename;
        qDebug() << "ProfileSaveHelper: Matches built-in profile" << profile.title();
        emit duplicateFound(profile.title(), filename);
        return 0;  // Waiting for user decision
    }

    if (profile.saveToFile(fullPath)) {
        qDebug() << "ProfileSaveHelper: Saved" << profile.title() << "to" << fullPath;
        if (m_controller) {
            m_controller->refreshProfiles();
        }
        return 1;  // Success
    }

    qWarning() << "ProfileSaveHelper: Failed to save" << profile.title();
    return -1;  // Failed
}

void ProfileSaveHelper::saveOverwrite()
{
    if (!m_pendingProfile.isValid()) {
        qWarning() << "ProfileSaveHelper::saveOverwrite: Pending profile is not valid";
        emit importFailed("No pending profile to overwrite");
        return;
    }

    QString destDir = ProfileSaveHelper::downloadedProfilesPath();
    QString fullPath = destDir + "/" + m_pendingFilename + ".json";

    // Verify directory exists
    QDir dir(destDir);
    if (!dir.exists()) {
        qWarning() << "ProfileSaveHelper::saveOverwrite: Directory does not exist:" << destDir;
        emit importFailed("Failed to overwrite: destination folder does not exist");
        m_pendingProfile = Profile();
        m_pendingFilename.clear();
        return;
    }

    qDebug() << "ProfileSaveHelper::saveOverwrite: Saving to" << fullPath;

    if (m_pendingProfile.saveToFile(fullPath)) {
        emit importSuccess(m_pendingProfile.title());
        if (m_controller) {
            m_controller->refreshProfiles();
        }
    } else {
        qWarning() << "ProfileSaveHelper::saveOverwrite: saveToFile() failed for" << fullPath;
        emit importFailed("Failed to overwrite: " + m_pendingProfile.title() + " (check app permissions)");
    }

    m_pendingProfile = Profile();
    m_pendingFilename.clear();
}

void ProfileSaveHelper::saveAsNew()
{
    if (!m_pendingProfile.isValid()) {
        emit importFailed("No pending profile to save");
        return;
    }

    QString baseTitle = m_pendingProfile.title();
    QString baseFilename = titleToFilename(baseTitle);
    QString dlPath = ProfileSaveHelper::downloadedProfilesPath();
    QString duplicatePath = dlPath + "/" + baseFilename + ".json";

    // Check if there's a duplicate to differentiate from
    if (QFile::exists(duplicatePath) || QFile::exists(":/profiles/" + baseFilename + ".json")) {
        Profile existingProfile = Profile::loadFromFile(duplicatePath);
        if (!existingProfile.isValid() && QFile::exists(":/profiles/" + baseFilename + ".json")) {
            existingProfile = Profile::loadFromFile(":/profiles/" + baseFilename + ".json");
        }

        QString newTitle;
        QString newFilename;

        // Strategy 1: Different author
        if (existingProfile.isValid() &&
            !m_pendingProfile.author().isEmpty() &&
            !existingProfile.author().isEmpty() &&
            m_pendingProfile.author() != existingProfile.author()) {
            newTitle = baseTitle + " (by " + m_pendingProfile.author() + ")";
            newFilename = titleToFilename(newTitle);
        }
        // Strategy 2: Different step count
        else if (existingProfile.isValid() &&
                 m_pendingProfile.steps().size() != existingProfile.steps().size()) {
            newTitle = baseTitle + " (" + QString::number(m_pendingProfile.steps().size()) + " steps)";
            newFilename = titleToFilename(newTitle);
        }
        // Fallback: Numbered suffix
        else {
            int counter = 2;
            do {
                newTitle = baseTitle + " (" + QString::number(counter) + ")";
                newFilename = titleToFilename(newTitle);
                counter++;
            } while (QFile::exists(dlPath + "/" + newFilename + ".json") ||
                     QFile::exists(":/profiles/" + newFilename + ".json"));
        }

        // Check if the descriptive name is already taken
        if (QFile::exists(dlPath + "/" + newFilename + ".json") ||
            QFile::exists(":/profiles/" + newFilename + ".json")) {
            // Fall back to numbered suffix
            int counter = 2;
            do {
                newTitle = baseTitle + " (" + QString::number(counter) + ")";
                newFilename = titleToFilename(newTitle);
                counter++;
            } while (QFile::exists(dlPath + "/" + newFilename + ".json") ||
                     QFile::exists(":/profiles/" + newFilename + ".json"));
        }

        m_pendingProfile.setTitle(newTitle);
        baseFilename = newFilename;
    }

    // At this point baseFilename is unique
    QString newTitle = m_pendingProfile.title();
    QString fullPath = dlPath + "/" + baseFilename + ".json";

    if (m_pendingProfile.saveToFile(fullPath)) {
        emit importSuccess(newTitle);
        if (m_controller) {
            m_controller->refreshProfiles();
        }
    } else {
        emit importFailed("Failed to save: " + newTitle);
    }

    m_pendingProfile = Profile();
    m_pendingFilename.clear();
}

void ProfileSaveHelper::saveWithNewName(const QString& newName)
{
    if (!m_pendingProfile.isValid() || newName.isEmpty()) {
        emit importFailed(newName.isEmpty() ? "Profile name cannot be empty" : "No pending profile to save");
        return;
    }

    m_pendingProfile.setTitle(newName);

    QString filename = titleToFilename(newName);
    QString dlPath = ProfileSaveHelper::downloadedProfilesPath();

    // Auto-deduplicate if name already taken
    if (QFile::exists(dlPath + "/" + filename + ".json") ||
        QFile::exists(":/profiles/" + filename + ".json")) {
        int counter = 2;
        QString newFilename;
        do {
            newFilename = filename + "_" + QString::number(counter++);
        } while (QFile::exists(dlPath + "/" + newFilename + ".json") ||
                 QFile::exists(":/profiles/" + newFilename + ".json"));
        filename = newFilename;
    }

    QString fullPath = dlPath + "/" + filename + ".json";
    if (m_pendingProfile.saveToFile(fullPath)) {
        emit importSuccess(m_pendingProfile.title());
        if (m_controller) {
            m_controller->refreshProfiles();
        }
    } else {
        qWarning() << "ProfileSaveHelper::saveWithNewName: Failed to save:" << filename;
        emit importFailed("Failed to save profile");
    }

    m_pendingProfile = Profile();
    m_pendingFilename.clear();
}

void ProfileSaveHelper::cancelPending()
{
    m_pendingProfile = Profile();
    m_pendingFilename.clear();
}

bool ProfileSaveHelper::hasPending() const
{
    return m_pendingProfile.isValid();
}

QString ProfileSaveHelper::downloadedProfilesPath()
{
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    path += "/profiles/downloaded";

    // Ensure directory exists
    QDir dir(path);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            qWarning() << "ProfileSaveHelper: Failed to create directory:" << path;
        }
    }

    return path;
}

QString ProfileSaveHelper::titleToFilename(const QString& title) const
{
    if (m_controller) {
        return m_controller->titleToFilename(title);
    }
    // Fallback: simple sanitization (shouldn't happen in practice)
    QString filename = title.toLower();
    filename.replace(QRegularExpression("[^a-z0-9]+"), "_");
    filename.replace(QRegularExpression("^_+|_+$"), "");
    filename.replace(QRegularExpression("_+"), "_");
    if (filename.length() > 50) filename = filename.left(50);
    if (filename.isEmpty()) filename = "profile";
    return filename;
}
