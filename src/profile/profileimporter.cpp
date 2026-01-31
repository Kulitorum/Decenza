#include "profileimporter.h"
#include "../controllers/maincontroller.h"
#include "../core/settings.h"
#include "../core/profilestorage.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTimer>
#include <QRegularExpression>
#include <QDebug>

#ifdef Q_OS_IOS
#import <Foundation/Foundation.h>
#endif

ProfileImporter::ProfileImporter(MainController* controller, Settings* settings, QObject* parent)
    : QObject(parent)
    , m_controller(controller)
    , m_settings(settings)
{
}

QString ProfileImporter::detectDE1AppPath() const
{
    // Common locations for DE1 app profiles
    // Check both profiles (TCL) and profiles_v2 (JSON) directories
    QStringList possiblePaths = {
#ifdef Q_OS_ANDROID
        "/sdcard/de1plus/profiles",
        "/sdcard/de1plus/profiles_v2",
        "/storage/emulated/0/de1plus/profiles",
        "/storage/emulated/0/de1plus/profiles_v2",
        "/sdcard/Android/data/tk.tcl.wish/files/de1plus/profiles",
        "/sdcard/Android/data/tk.tcl.wish/files/de1plus/profiles_v2",
#endif
        QDir::homePath() + "/de1plus/profiles",
        QDir::homePath() + "/de1plus/profiles_v2",
        QDir::homePath() + "/Documents/de1plus/profiles",
        QDir::homePath() + "/Documents/de1plus/profiles_v2",
#ifdef Q_OS_WIN
        "C:/code/de1app/de1plus/profiles",
        "C:/code/de1app/de1plus/profiles_v2",
#endif
    };

    for (const QString& path : possiblePaths) {
        QDir dir(path);
        if (dir.exists()) {
            // Check if it contains profile files
            QStringList tclFiles = dir.entryList(QStringList() << "*.tcl", QDir::Files);
            QStringList jsonFiles = dir.entryList(QStringList() << "*.json", QDir::Files);
            if (!tclFiles.isEmpty() || !jsonFiles.isEmpty()) {
                qDebug() << "ProfileImporter: Found DE1 app profiles at" << path
                         << "with" << tclFiles.size() << "TCL and" << jsonFiles.size() << "JSON profiles";
                // Return the parent de1plus folder
                QDir parent(path);
                parent.cdUp();
                return parent.absolutePath();
            }
        }
    }

    return QString();
}

void ProfileImporter::scanProfiles()
{
    QString de1plusPath = detectDE1AppPath();
    if (de1plusPath.isEmpty()) {
        setStatus("DE1 app not found");
        m_detectedPath.clear();
        emit detectedPathChanged();
        m_availableProfiles.clear();
        emit availableProfilesChanged();
        emit scanComplete(0);
        return;
    }

    m_detectedPath = de1plusPath;
    emit detectedPathChanged();
    scanProfilesFromPath(de1plusPath);
}

void ProfileImporter::scanProfilesFromPath(const QString& path)
{
    if (m_scanning) {
        return;
    }

    m_scanning = true;
    emit isScanningChanged();

    m_pendingFiles.clear();
    m_availableProfiles.clear();
    m_processedProfiles = 0;

    // Scan profiles/ for TCL files
    QString tclPath = path + "/profiles";
    QDir tclDir(tclPath);
    if (tclDir.exists()) {
        QDirIterator tclIt(tclPath, QStringList() << "*.tcl", QDir::Files);
        while (tclIt.hasNext()) {
            m_pendingFiles.append(tclIt.next());
        }
        qDebug() << "ProfileImporter: Found" << m_pendingFiles.size() << "TCL profiles in" << tclPath;
    }

    // Scan profiles_v2/ for JSON files
    QString jsonPath = path + "/profiles_v2";
    QDir jsonDir(jsonPath);
    if (jsonDir.exists()) {
        int beforeCount = static_cast<int>(m_pendingFiles.size());
        QDirIterator jsonIt(jsonPath, QStringList() << "*.json", QDir::Files);
        while (jsonIt.hasNext()) {
            m_pendingFiles.append(jsonIt.next());
        }
        qDebug() << "ProfileImporter: Found" << (m_pendingFiles.size() - beforeCount) << "JSON profiles in" << jsonPath;
    }

    m_totalProfiles = static_cast<int>(m_pendingFiles.size());
    emit progressChanged();

    if (m_pendingFiles.isEmpty()) {
        setStatus("No profiles found");
        m_scanning = false;
        emit isScanningChanged();
        emit availableProfilesChanged();
        emit scanComplete(0);
        return;
    }

    setStatus(QString("Scanning %1 profiles...").arg(m_totalProfiles));

    // Process files asynchronously to keep UI responsive
    QTimer::singleShot(0, this, &ProfileImporter::processNextScan);
}

void ProfileImporter::processNextScan()
{
    if (m_pendingFiles.isEmpty()) {
        // Scanning complete
        m_scanning = false;
        emit isScanningChanged();

        // Sort by title
        std::sort(m_availableProfiles.begin(), m_availableProfiles.end(),
                  [](const QVariant& a, const QVariant& b) {
                      return a.toMap()["title"].toString().toLower() <
                             b.toMap()["title"].toString().toLower();
                  });

        setStatus(QString("Found %1 profiles").arg(m_availableProfiles.size()));
        emit availableProfilesChanged();
        emit scanComplete(static_cast<int>(m_availableProfiles.size()));
        return;
    }

    // Process batch of files
    int batchSize = 10;
    for (int i = 0; i < batchSize && !m_pendingFiles.isEmpty(); i++) {
        QString filePath = m_pendingFiles.takeFirst();
        QString filename = QFileInfo(filePath).fileName();
        bool isTcl = filePath.endsWith(".tcl", Qt::CaseInsensitive);

        // Load the profile
        Profile profile;
        if (isTcl) {
            profile = Profile::loadFromTclFile(filePath);
        } else {
            profile = Profile::loadFromFile(filePath);
        }

        if (!profile.isValid() || profile.title().isEmpty()) {
            qDebug() << "ProfileImporter: Skipping invalid profile" << filename;
            m_processedProfiles++;
            continue;
        }

        QVariantMap entry;
        entry["sourcePath"] = filePath;
        entry["filename"] = filename;
        entry["title"] = profile.title();
        entry["author"] = profile.author();
        entry["frameCount"] = profile.steps().size();
        entry["format"] = isTcl ? "TCL" : "JSON";
        entry["beverageType"] = profile.beverageType();

        // Check local status
        QVariantMap status = checkProfileStatus(profile.title(), &profile);
        entry["exists"] = status["exists"];
        entry["identical"] = status["identical"];
        entry["source"] = status["source"];
        entry["localFilename"] = status["filename"];

        // Determine import status
        if (!status["exists"].toBool()) {
            entry["status"] = "new";
        } else if (status["identical"].toBool()) {
            entry["status"] = "identical";
        } else {
            entry["status"] = "different";
        }

        m_availableProfiles.append(entry);
        m_processedProfiles++;
    }

    emit progressChanged();

    // Update status periodically
    if (m_processedProfiles % 20 == 0) {
        setStatus(QString("Scanning... %1/%2").arg(m_processedProfiles).arg(m_totalProfiles));
    }

    // Continue processing
    QTimer::singleShot(0, this, &ProfileImporter::processNextScan);
}

QVariantMap ProfileImporter::checkProfileStatus(const QString& profileTitle, const Profile* incomingProfile)
{
    QVariantMap result;
    result["exists"] = false;
    result["identical"] = false;
    result["source"] = "";
    result["filename"] = "";

    if (!m_controller) {
        return result;
    }

    // Generate expected filename
    QString filename = generateFilename(profileTitle);
    result["filename"] = filename;

    // Check if profile exists in any location
    ProfileStorage* storage = m_controller->profileStorage();

    // Check external/downloaded storage first
    if (storage && storage->isConfigured() && storage->profileExists(filename)) {
        result["exists"] = true;
        result["source"] = "D";  // Downloaded
    }

    // Check local downloaded folder
    QString downloadedPath = downloadedProfilesPath() + "/" + filename + ".json";
    if (QFile::exists(downloadedPath)) {
        result["exists"] = true;
        result["source"] = "D";
    }

    // Check built-in profiles
    QString builtinPath = ":/profiles/" + filename + ".json";
    if (QFile::exists(builtinPath)) {
        result["exists"] = true;
        result["source"] = "B";  // Built-in
    }

    // If exists and we have incoming profile, compare frames
    if (result["exists"].toBool() && incomingProfile && incomingProfile->isValid()) {
        Profile localProfile = loadLocalProfile(filename);
        if (localProfile.isValid()) {
            bool identical = compareProfileFrames(*incomingProfile, localProfile);
            result["identical"] = identical;
        }
    }

    return result;
}

bool ProfileImporter::compareProfileFrames(const Profile& a, const Profile& b) const
{
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
    }

    return true;
}

Profile ProfileImporter::loadLocalProfile(const QString& filename) const
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
    QString localPath = downloadedProfilesPath() + "/" + filename + ".json";
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

QString ProfileImporter::generateFilename(const QString& title) const
{
    if (title.isEmpty()) {
        return "unnamed_profile";
    }

    QString filename = title.toLower();

    // Replace special characters and spaces with underscores
    filename.replace(QRegularExpression("[^a-z0-9]+"), "_");

    // Remove leading/trailing underscores
    filename.replace(QRegularExpression("^_+|_+$"), "");

    // Collapse multiple underscores
    filename.replace(QRegularExpression("_+"), "_");

    // Limit length
    if (filename.length() > 50) {
        filename = filename.left(50);
    }

    if (filename.isEmpty()) {
        filename = "profile";
    }

    return filename;
}

QString ProfileImporter::downloadedProfilesPath() const
{
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    path += "/profiles/downloaded";

    // Ensure directory exists
    QDir dir(path);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            qWarning() << "ProfileImporter: Failed to create directory:" << path;
        }
    }

    return path;
}

void ProfileImporter::importProfile(const QString& sourcePath)
{
    if (m_importing) {
        return;
    }

    m_importing = true;
    emit isImportingChanged();

    // Load the profile
    bool isTcl = sourcePath.endsWith(".tcl", Qt::CaseInsensitive);
    Profile profile;
    if (isTcl) {
        profile = Profile::loadFromTclFile(sourcePath);
    } else {
        profile = Profile::loadFromFile(sourcePath);
    }

    if (!profile.isValid() || profile.title().isEmpty()) {
        setStatus("Failed to load profile");
        m_importing = false;
        emit isImportingChanged();
        emit importFailed("Failed to load profile from " + QFileInfo(sourcePath).fileName());
        return;
    }

    QString filename = generateFilename(profile.title());
    int result = saveProfile(profile, filename);

    if (result == 1) {
        // Saved successfully
        setStatus("Imported: " + profile.title());
        m_importing = false;
        emit isImportingChanged();
        emit importSuccess(profile.title());
        if (m_controller) {
            m_controller->refreshProfiles();
        }
    } else if (result == 0) {
        // Duplicate found - waiting for user decision
        m_pendingProfile = profile;
        m_pendingSourcePath = sourcePath;
        emit duplicateFound(profile.title(), filename);
    } else {
        // Failed
        m_importing = false;
        emit isImportingChanged();
        emit importFailed("Failed to save profile: " + profile.title());
    }
}

void ProfileImporter::importProfileFromUrl(const QUrl& fileUrl)
{
    QString localPath;

#ifdef Q_OS_IOS
    // On iOS, FileDialog returns security-scoped URLs from UIDocumentPicker.
    // We must access the security-scoped resource, copy the file to temp, then release.
    NSURL* nsUrl = fileUrl.toNSURL();
    bool accessGranted = [nsUrl startAccessingSecurityScopedResource];

    localPath = fileUrl.toLocalFile();
    if (localPath.isEmpty()) {
        localPath = fileUrl.toString(QUrl::PreferLocalFile);
    }

    // Copy to temp so we can release the security scope before importProfile reads it
    QString tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                       + "/" + QFileInfo(localPath).fileName();
    QFile::remove(tempPath);
    bool copied = QFile::copy(localPath, tempPath);

    if (accessGranted) {
        [nsUrl stopAccessingSecurityScopedResource];
    }

    if (!copied) {
        setStatus("Failed to access file");
        emit importFailed("Could not read the selected file");
        return;
    }
    localPath = tempPath;
#else
    localPath = fileUrl.toLocalFile();
    if (localPath.isEmpty()) {
        localPath = fileUrl.toString(QUrl::PreferLocalFile);
    }
#endif

    importProfile(localPath);
}

void ProfileImporter::forceImportProfile(const QString& sourcePath)
{
    if (m_importing) {
        return;
    }

    m_importing = true;
    emit isImportingChanged();

    // Load the profile
    bool isTcl = sourcePath.endsWith(".tcl", Qt::CaseInsensitive);
    Profile profile;
    if (isTcl) {
        profile = Profile::loadFromTclFile(sourcePath);
    } else {
        profile = Profile::loadFromFile(sourcePath);
    }

    if (!profile.isValid() || profile.title().isEmpty()) {
        setStatus("Failed to load profile");
        m_importing = false;
        emit isImportingChanged();
        emit importFailed("Failed to load profile from " + QFileInfo(sourcePath).fileName());
        return;
    }

    QString filename = generateFilename(profile.title());
    QString fullPath = downloadedProfilesPath() + "/" + filename + ".json";

    // Force overwrite - don't check for duplicates
    if (profile.saveToFile(fullPath)) {
        setStatus("Re-imported: " + profile.title());
        m_importing = false;
        emit isImportingChanged();
        emit importSuccess(profile.title());
        if (m_controller) {
            m_controller->refreshProfiles();
        }
    } else {
        m_importing = false;
        emit isImportingChanged();
        emit importFailed("Failed to save profile: " + profile.title());
    }
}

void ProfileImporter::importProfileWithName(const QString& sourcePath, const QString& newName)
{
    if (m_importing) {
        return;
    }

    m_importing = true;
    emit isImportingChanged();

    // Load the profile
    bool isTcl = sourcePath.endsWith(".tcl", Qt::CaseInsensitive);
    Profile profile;
    if (isTcl) {
        profile = Profile::loadFromTclFile(sourcePath);
    } else {
        profile = Profile::loadFromFile(sourcePath);
    }

    if (!profile.isValid()) {
        setStatus("Failed to load profile");
        m_importing = false;
        emit isImportingChanged();
        emit importFailed("Failed to load profile from " + QFileInfo(sourcePath).fileName());
        return;
    }

    // Set new name
    profile.setTitle(newName);
    QString filename = generateFilename(newName);

    QString fullPath = downloadedProfilesPath() + "/" + filename + ".json";
    if (profile.saveToFile(fullPath)) {
        setStatus("Imported: " + newName);
        m_importing = false;
        emit isImportingChanged();
        emit importSuccess(newName);
        if (m_controller) {
            m_controller->refreshProfiles();
        }
    } else {
        m_importing = false;
        emit isImportingChanged();
        emit importFailed("Failed to save profile: " + newName);
    }
}

int ProfileImporter::saveProfile(const Profile& profile, const QString& filename)
{
    QString fullPath = downloadedProfilesPath() + "/" + filename + ".json";

    // Check for duplicates
    if (QFile::exists(fullPath)) {
        qDebug() << "ProfileImporter: Duplicate found for" << profile.title();
        return 0;  // Waiting for user decision
    }

    // Also check built-in profiles
    QString builtinPath = ":/profiles/" + filename + ".json";
    if (QFile::exists(builtinPath)) {
        qDebug() << "ProfileImporter: Matches built-in profile" << profile.title();
        return 0;  // Waiting for user decision
    }

    if (profile.saveToFile(fullPath)) {
        qDebug() << "ProfileImporter: Saved" << profile.title() << "to" << fullPath;
        return 1;  // Success
    }

    qWarning() << "ProfileImporter: Failed to save" << profile.title();
    return -1;  // Failed
}

void ProfileImporter::saveOverwrite()
{
    if (!m_pendingProfile.isValid()) {
        qWarning() << "ProfileImporter::saveOverwrite: Pending profile is not valid";
        m_importing = false;
        emit isImportingChanged();
        return;
    }

    QString filename = generateFilename(m_pendingProfile.title());
    QString destDir = downloadedProfilesPath();
    QString fullPath = destDir + "/" + filename + ".json";

    // Verify directory exists
    QDir dir(destDir);
    if (!dir.exists()) {
        qWarning() << "ProfileImporter::saveOverwrite: Directory does not exist:" << destDir;
        emit importFailed("Failed to overwrite: destination folder does not exist");
        m_pendingProfile = Profile();
        m_pendingSourcePath.clear();
        m_importing = false;
        emit isImportingChanged();
        return;
    }

    qDebug() << "ProfileImporter::saveOverwrite: Saving to" << fullPath;

    if (m_pendingProfile.saveToFile(fullPath)) {
        setStatus("Overwritten: " + m_pendingProfile.title());
        emit importSuccess(m_pendingProfile.title());
        if (m_controller) {
            m_controller->refreshProfiles();
        }
    } else {
        qWarning() << "ProfileImporter::saveOverwrite: saveToFile() failed for" << fullPath;
        emit importFailed("Failed to overwrite: " + m_pendingProfile.title() + " (check app permissions)");
    }

    m_pendingProfile = Profile();
    m_pendingSourcePath.clear();
    m_importing = false;
    emit isImportingChanged();
}

void ProfileImporter::saveAsNew()
{
    if (!m_pendingProfile.isValid()) {
        m_importing = false;
        emit isImportingChanged();
        return;
    }

    // Generate unique name
    QString baseTitle = m_pendingProfile.title();
    QString baseFilename = generateFilename(baseTitle);
    QString newFilename = baseFilename;
    int counter = 1;

    while (QFile::exists(downloadedProfilesPath() + "/" + newFilename + ".json") ||
           QFile::exists(":/profiles/" + newFilename + ".json")) {
        newFilename = baseFilename + "_" + QString::number(counter);
        counter++;
    }

    // Update title to match
    QString newTitle = baseTitle + " (" + QString::number(counter - 1) + ")";
    m_pendingProfile.setTitle(newTitle);

    QString fullPath = downloadedProfilesPath() + "/" + newFilename + ".json";
    if (m_pendingProfile.saveToFile(fullPath)) {
        setStatus("Saved as: " + newTitle);
        emit importSuccess(newTitle);
        if (m_controller) {
            m_controller->refreshProfiles();
        }
    } else {
        emit importFailed("Failed to save: " + newTitle);
    }

    m_pendingProfile = Profile();
    m_pendingSourcePath.clear();
    m_importing = false;
    emit isImportingChanged();
}

void ProfileImporter::saveWithNewName(const QString& newName)
{
    if (!m_pendingProfile.isValid() || newName.isEmpty()) {
        m_importing = false;
        emit isImportingChanged();
        return;
    }

    m_pendingProfile.setTitle(newName);
    QString filename = generateFilename(newName);
    QString fullPath = downloadedProfilesPath() + "/" + filename + ".json";

    if (m_pendingProfile.saveToFile(fullPath)) {
        setStatus("Saved as: " + newName);
        emit importSuccess(newName);
        if (m_controller) {
            m_controller->refreshProfiles();
        }
    } else {
        emit importFailed("Failed to save: " + newName);
    }

    m_pendingProfile = Profile();
    m_pendingSourcePath.clear();
    m_importing = false;
    emit isImportingChanged();
}

void ProfileImporter::cancelImport()
{
    m_pendingProfile = Profile();
    m_pendingSourcePath.clear();
    m_importing = false;
    emit isImportingChanged();
    setStatus("Import cancelled");
}

void ProfileImporter::importAllNew()
{
    importAll(false);
}

void ProfileImporter::importAll(bool overwriteExisting)
{
    if (m_importing || m_availableProfiles.isEmpty()) {
        return;
    }

    m_importing = true;
    emit isImportingChanged();

    m_importQueue.clear();
    m_batchOverwrite = overwriteExisting;
    m_batchImported = 0;
    m_batchSkipped = 0;
    m_batchFailed = 0;

    // Build queue of profiles to import
    for (const QVariant& var : m_availableProfiles) {
        QVariantMap entry = var.toMap();
        QString status = entry["status"].toString();

        if (status == "new") {
            m_importQueue.append(entry["sourcePath"].toString());
        } else if (status == "different" && overwriteExisting) {
            m_importQueue.append(entry["sourcePath"].toString());
        }
        // Skip identical profiles
    }

    if (m_importQueue.isEmpty()) {
        setStatus("No new profiles to import");
        m_importing = false;
        emit isImportingChanged();
        emit batchImportComplete(0, 0, 0);
        return;
    }

    m_totalProfiles = static_cast<int>(m_importQueue.size());
    m_processedProfiles = 0;
    emit progressChanged();

    setStatus(QString("Importing %1 profiles...").arg(m_totalProfiles));

    QTimer::singleShot(0, this, &ProfileImporter::processNextImport);
}

void ProfileImporter::updateAllDifferent()
{
    if (m_importing || m_availableProfiles.isEmpty()) {
        return;
    }

    m_importing = true;
    emit isImportingChanged();

    m_importQueue.clear();
    m_batchOverwrite = true;  // Always overwrite for updates
    m_batchImported = 0;
    m_batchSkipped = 0;
    m_batchFailed = 0;

    // Build queue of profiles with "different" status only
    for (const QVariant& var : m_availableProfiles) {
        QVariantMap entry = var.toMap();
        QString status = entry["status"].toString();

        if (status == "different") {
            m_importQueue.append(entry["sourcePath"].toString());
        }
    }

    if (m_importQueue.isEmpty()) {
        setStatus("No profiles to update");
        m_importing = false;
        emit isImportingChanged();
        emit batchImportComplete(0, 0, 0);
        return;
    }

    m_totalProfiles = static_cast<int>(m_importQueue.size());
    m_processedProfiles = 0;
    emit progressChanged();

    setStatus(QString("Updating %1 profiles...").arg(m_totalProfiles));

    QTimer::singleShot(0, this, &ProfileImporter::processNextImport);
}

void ProfileImporter::processNextImport()
{
    if (m_importQueue.isEmpty()) {
        // Batch import complete
        m_importing = false;
        emit isImportingChanged();

        setStatus(QString("Imported %1, skipped %2, failed %3")
                  .arg(m_batchImported).arg(m_batchSkipped).arg(m_batchFailed));

        if (m_controller) {
            m_controller->refreshProfiles();
        }

        // Re-scan to update statuses
        scanProfiles();

        emit batchImportComplete(m_batchImported, m_batchSkipped, m_batchFailed);
        return;
    }

    QString sourcePath = m_importQueue.takeFirst();
    m_processedProfiles++;
    emit progressChanged();

    // Load the profile
    bool isTcl = sourcePath.endsWith(".tcl", Qt::CaseInsensitive);
    Profile profile;
    if (isTcl) {
        profile = Profile::loadFromTclFile(sourcePath);
    } else {
        profile = Profile::loadFromFile(sourcePath);
    }

    if (!profile.isValid() || profile.title().isEmpty()) {
        m_batchFailed++;
        qDebug() << "ProfileImporter: Failed to load" << sourcePath;
        QTimer::singleShot(0, this, &ProfileImporter::processNextImport);
        return;
    }

    QString filename = generateFilename(profile.title());
    QString fullPath = downloadedProfilesPath() + "/" + filename + ".json";

    // Handle existing files
    if (QFile::exists(fullPath)) {
        if (m_batchOverwrite) {
            // Overwrite
            if (profile.saveToFile(fullPath)) {
                m_batchImported++;
            } else {
                m_batchFailed++;
            }
        } else {
            m_batchSkipped++;
        }
    } else {
        // New file
        if (profile.saveToFile(fullPath)) {
            m_batchImported++;
        } else {
            m_batchFailed++;
        }
    }

    // Update status periodically
    if (m_processedProfiles % 5 == 0) {
        setStatus(QString("Importing... %1/%2").arg(m_processedProfiles).arg(m_totalProfiles));
    }

    QTimer::singleShot(0, this, &ProfileImporter::processNextImport);
}

void ProfileImporter::refreshProfileStatus(int index)
{
    if (index < 0 || index >= m_availableProfiles.size()) {
        return;
    }

    QVariantMap entry = m_availableProfiles[index].toMap();
    QString sourcePath = entry["sourcePath"].toString();

    // Reload and recheck status
    bool isTcl = sourcePath.endsWith(".tcl", Qt::CaseInsensitive);
    Profile profile;
    if (isTcl) {
        profile = Profile::loadFromTclFile(sourcePath);
    } else {
        profile = Profile::loadFromFile(sourcePath);
    }

    if (profile.isValid()) {
        QVariantMap status = checkProfileStatus(profile.title(), &profile);
        entry["exists"] = status["exists"];
        entry["identical"] = status["identical"];
        entry["source"] = status["source"];
        entry["localFilename"] = status["filename"];

        if (!status["exists"].toBool()) {
            entry["status"] = "new";
        } else if (status["identical"].toBool()) {
            entry["status"] = "identical";
        } else {
            entry["status"] = "different";
        }

        m_availableProfiles[index] = entry;
        emit availableProfilesChanged();
    }
}

void ProfileImporter::setStatus(const QString& message)
{
    if (m_statusMessage != message) {
        m_statusMessage = message;
        emit statusMessageChanged();
    }
}
