#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include "profile.h"

class MainController;

/**
 * ProfileSaveHelper - Shared profile save/compare/deduplicate logic
 *
 * Extracted from ProfileImporter and VisualizerImporter to unify the
 * duplicate detection, comparison, and saving behavior. Both importers
 * delegate to this helper for all save-related operations.
 */
class ProfileSaveHelper : public QObject {
    Q_OBJECT

public:
    explicit ProfileSaveHelper(MainController* controller, QObject* parent = nullptr);

    // Compare two profiles for equality (profile-level fields + all frames)
    bool compareProfiles(const Profile& a, const Profile& b) const;

    // Check if a profile exists locally and whether it's identical
    // Returns map with: exists, identical, source ("D"/"B"), filename
    QVariantMap checkProfileStatus(const QString& profileTitle, const Profile* incomingProfile = nullptr);

    // Load a local profile by filename (cascade: ProfileStorage -> downloaded -> built-in)
    Profile loadLocalProfile(const QString& filename) const;

    // Save profile to downloaded folder with duplicate detection
    // Returns: 1 = saved, 0 = waiting for user (duplicate), -1 = failed
    int saveProfile(const Profile& profile, const QString& filename);

    // Duplicate resolution actions (operate on m_pendingProfile)
    void saveOverwrite();
    void saveAsNew();
    void saveWithNewName(const QString& newName);
    void cancelPending();

    // Whether there is a pending profile awaiting duplicate resolution
    bool hasPending() const;

    // Path to the downloaded profiles folder (ensures directory exists)
    static QString downloadedProfilesPath();

    // Convert profile title to filename using MainController::titleToFilename()
    QString titleToFilename(const QString& title) const;

signals:
    void importSuccess(const QString& profileTitle);
    void importFailed(const QString& error);
    void duplicateFound(const QString& profileTitle, const QString& existingPath);

private:
    MainController* m_controller = nullptr;

    // Pending profile for duplicate resolution
    Profile m_pendingProfile;
    QString m_pendingFilename;
};
