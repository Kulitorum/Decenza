#include "profileimporter.h"
#include "profilesavehelper.h"
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
    , m_saveHelper(new ProfileSaveHelper(controller, this))
{
    // Forward helper signals to our own signals
    connect(m_saveHelper, &ProfileSaveHelper::importSuccess, this, &ProfileImporter::importSuccess);
    connect(m_saveHelper, &ProfileSaveHelper::importFailed, this, [this](const QString& error) {
        setStatus(error);  // Show error in the page's status bar
        emit importFailed(error);
    });
    connect(m_saveHelper, &ProfileSaveHelper::duplicateFound, this, &ProfileImporter::duplicateFound);
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
            qDebug() << "ProfileImporter: Directory exists but no .tcl/.json files:" << path;
        } else {
            qDebug() << "ProfileImporter: Directory not found:" << path;
        }
    }

    qWarning() << "ProfileImporter: Could not find DE1 app profiles in any known location";
    return QString();
}

void ProfileImporter::importFromDE1App()
{
    if (m_scanning || m_importing) {
        qDebug() << "ProfileImporter: importFromDE1App - already busy";
        return;
    }
    QString path = detectDE1AppPath();
    if (path.isEmpty()) {
        setStatus("DE1 app profile folder not found");
        emit batchImportComplete(0, 0, 0);
        return;
    }
    qDebug() << "ProfileImporter: importFromDE1App - scanning" << path;
    m_autoImportAfterScan = true;
    scanProfilesFromPath(path);
}

void ProfileImporter::scanProfiles()
{
    QString de1plusPath = detectDE1AppPath();
    if (de1plusPath.isEmpty()) {
        // Distinguish "no de1plus folder at all" from "folder exists but no profiles"
        bool de1plusExists = false;
#ifdef Q_OS_ANDROID
        de1plusExists = QDir("/sdcard/de1plus").exists() ||
                        QDir("/storage/emulated/0/de1plus").exists() ||
                        QDir("/sdcard/Android/data/tk.tcl.wish/files/de1plus").exists();
#endif
        if (de1plusExists) {
            setStatus(tr("DE1 app found but no profiles directory — use Import File to import individual profiles"));
        } else {
            setStatus(tr("DE1 app not found"));
        }
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

void ProfileImporter::scanProfilesFromUrl(const QUrl& folderUrl)
{
    scanProfilesFromPath(folderUrl.toLocalFile());
}

void ProfileImporter::scanProfilesFromPath(const QString& path)
{
    if (m_scanning) {
        return;
    }

    // Update detectedPath so the header shows the folder being scanned
    m_detectedPath = path;
    emit detectedPathChanged();

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
        m_autoImportAfterScan = false;  // reset — no scan will complete to clear this
        emit isScanningChanged();
        emit availableProfilesChanged();
        emit scanComplete(0);
        return;
    }

    setStatus(QString("Scanning %1 profiles...").arg(m_totalProfiles));

    // Process files asynchronously to keep UI responsive
    QTimer::singleShot(0, this, &ProfileImporter::onProcessNextScan);
}

void ProfileImporter::onProcessNextScan()
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

        if (m_autoImportAfterScan) {
            m_autoImportAfterScan = false;
            if (!m_availableProfiles.isEmpty()) {
                qDebug() << "ProfileImporter: auto-importing" << m_availableProfiles.size() << "new profiles";
                importAllNew();
            } else {
                qDebug() << "ProfileImporter: no new profiles to import";
            }
        }
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
            qWarning() << "ProfileImporter: Skipping invalid profile" << filename;
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
        QVariantMap status = m_saveHelper->checkProfileStatus(profile.title(), &profile);
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
    QTimer::singleShot(0, this, &ProfileImporter::onProcessNextScan);
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

    QString filename = m_saveHelper->titleToFilename(profile.title());
    ProfileSaveHelper::SaveResult result = m_saveHelper->saveProfile(profile, filename);

    if (result == ProfileSaveHelper::SaveResult::Saved) {
        // Saved successfully
        setStatus("Imported: " + profile.title());
        m_importing = false;
        emit isImportingChanged();
        emit importSuccess(profile.title());
    } else if (result == ProfileSaveHelper::SaveResult::PendingResolution) {
        // Duplicate found - waiting for user decision (signal already emitted by helper)
    } else {
        // Failed — helper already logged; surface to UI and reset state
        setStatus("Failed to save: " + profile.title());
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

    QString filename = m_saveHelper->titleToFilename(profile.title());
    QString fullPath = ProfileSaveHelper::downloadedProfilesPath() + "/" + filename + ".json";

    // Force overwrite - don't check for duplicates
    if (profile.saveToFile(fullPath)) {
        setStatus("Re-imported: " + profile.title());
        m_importing = false;
        emit isImportingChanged();
        emit importSuccess(profile.title());
        if (m_controller) {
            m_controller->profileManager()->refreshProfiles();
        }
    } else {
        setStatus("Failed to save: " + profile.title());
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
    QString filename = m_saveHelper->titleToFilename(newName);

    QString fullPath = ProfileSaveHelper::downloadedProfilesPath() + "/" + filename + ".json";
    if (profile.saveToFile(fullPath)) {
        setStatus("Imported: " + newName);
        m_importing = false;
        emit isImportingChanged();
        emit importSuccess(newName);
        if (m_controller) {
            m_controller->profileManager()->refreshProfiles();
        }
    } else {
        setStatus("Failed to save: " + newName);
        m_importing = false;
        emit isImportingChanged();
        emit importFailed("Failed to save profile: " + newName);
    }
}

void ProfileImporter::saveOverwrite()
{
    // Reset importing state before the helper emits its signal so QML sees
    // isImporting == false at the moment onImportSuccess/onImportFailed fires.
    m_importing = false;
    emit isImportingChanged();
    m_saveHelper->saveOverwrite();
}

void ProfileImporter::saveAsNew()
{
    m_importing = false;
    emit isImportingChanged();
    m_saveHelper->saveAsNew();
}

void ProfileImporter::saveWithNewName(const QString& newName)
{
    m_importing = false;
    emit isImportingChanged();
    m_saveHelper->saveWithNewName(newName);
}

void ProfileImporter::cancelImport()
{
    m_saveHelper->cancelPending();
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
        } else if (status == "different") {
            if (overwriteExisting || entry["source"].toString() == "B") {
                // Built-in "different" profiles are always queued — they need
                // a rename on the way in rather than an overwrite.
                m_importQueue.append(entry["sourcePath"].toString());
            }
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

    QTimer::singleShot(0, this, &ProfileImporter::onProcessNextImport);
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

    QTimer::singleShot(0, this, &ProfileImporter::onProcessNextImport);
}

void ProfileImporter::onProcessNextImport()
{
    if (m_importQueue.isEmpty()) {
        // Batch import complete
        m_importing = false;
        emit isImportingChanged();

        setStatus(QString("Imported %1, skipped %2, failed %3")
                  .arg(m_batchImported).arg(m_batchSkipped).arg(m_batchFailed));

        if (m_controller) {
            m_controller->profileManager()->refreshProfiles();
        }

        // Re-scan to update statuses, using the same path as the original scan
        if (!m_detectedPath.isEmpty()) {
            scanProfilesFromPath(m_detectedPath);
        } else {
            scanProfiles();
        }

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
        qWarning() << "ProfileImporter: Failed to load profile from" << sourcePath << "(invalid or empty)";
        QTimer::singleShot(0, this, &ProfileImporter::onProcessNextImport);
        return;
    }

    QString filename = m_saveHelper->titleToFilename(profile.title());
    QString downloadedPath = ProfileSaveHelper::downloadedProfilesPath() + "/" + filename + ".json";

    ProfileStorage* storage = m_controller ? m_controller->profileStorage() : nullptr;
    bool inStorage = storage && storage->isConfigured() && storage->profileExists(filename);
    bool inDownloaded = QFile::exists(downloadedPath);
    bool inBuiltin = QFile::exists(":/profiles/" + filename + ".json");

    // Built-in profiles are read-only. If this profile's filename matches a
    // built-in, rename it before saving so we never write a downloaded file
    // with the same name as a built-in.
    if (inBuiltin && !inStorage && !inDownloaded) {
        profile.setTitle(profile.title() + " (imported)");
        filename = m_saveHelper->titleToFilename(profile.title());
        downloadedPath = ProfileSaveHelper::downloadedProfilesPath() + "/" + filename + ".json";
        inDownloaded = QFile::exists(downloadedPath);
        qDebug() << "ProfileImporter: renaming built-in collision to" << profile.title();
    }

    qDebug() << "ProfileImporter: batch processing" << profile.title()
             << "- inStorage:" << inStorage << "inDownloaded:" << inDownloaded;

    // Handle existing files
    if (inStorage || inDownloaded) {
        if (m_batchOverwrite) {
            bool saved = false;
            if (inStorage) {
                // Write to the same location loadLocalProfile() reads from, so re-scan sees the update
                saved = storage->writeProfile(filename, profile.toJsonString());
                qDebug() << "ProfileImporter: overwrote in ProfileStorage:" << filename << "ok:" << saved;
            } else {
                saved = profile.saveToFile(downloadedPath);
                qDebug() << "ProfileImporter: overwrote in downloaded:" << downloadedPath << "ok:" << saved;
            }
            if (saved) {
                m_batchImported++;
            } else {
                qWarning() << "ProfileImporter: Failed to overwrite" << profile.title();
                m_batchFailed++;
            }
        } else {
            m_batchSkipped++;
        }
    } else {
        // New file - save to downloaded folder
        if (profile.saveToFile(downloadedPath)) {
            m_batchImported++;
        } else {
            qWarning() << "ProfileImporter: Failed to save" << profile.title() << "to" << downloadedPath;
            m_batchFailed++;
        }
    }

    // Update status periodically
    if (m_processedProfiles % 5 == 0) {
        setStatus(QString("Importing... %1/%2").arg(m_processedProfiles).arg(m_totalProfiles));
    }

    QTimer::singleShot(0, this, &ProfileImporter::onProcessNextImport);
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

    if (!profile.isValid()) {
        qWarning() << "ProfileImporter::refreshProfileStatus: Cannot reload profile from" << sourcePath;
        entry["status"] = "error";
        m_availableProfiles[index] = entry;
        emit availableProfilesChanged();
        return;
    }

    QVariantMap status = m_saveHelper->checkProfileStatus(profile.title(), &profile);
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

void ProfileImporter::setStatus(const QString& message)
{
    if (m_statusMessage != message) {
        m_statusMessage = message;
        emit statusMessageChanged();
    }
}
