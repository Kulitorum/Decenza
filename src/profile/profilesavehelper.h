#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>
#include <optional>
#include "profile.h"

class MainController;

/**
 * ProfileSaveHelper - Shared profile save/compare/deduplicate logic
 *
 * Extracted from ProfileImporter and VisualizerImporter to unify the
 * duplicate detection, comparison, and saving behavior. Both importers
 * delegate to this helper for all save-related operations.
 *
 * State model: the helper is either idle (no pending profile) or awaiting
 * duplicate resolution (has pending profile). Resolution methods
 * (saveOverwrite, saveAsNew, saveWithNewName) are only valid after
 * saveProfile() returns SaveResult::PendingResolution. Check hasPending()
 * before calling them. cancelPending() returns the helper to idle state.
 */
class ProfileSaveHelper : public QObject {
    Q_OBJECT

public:
    enum class SaveResult {
        Saved,             // Profile saved successfully
        PendingResolution, // Duplicate found — awaiting user decision
        Failed             // Save failed (check importFailed signal)
    };

    explicit ProfileSaveHelper(MainController* controller, QObject* parent = nullptr);

    // Compare two profiles for equality (profile-level fields + all frames).
    // Returns false if either profile has no steps.
    bool compareProfiles(const Profile& a, const Profile& b) const;

    // Check if a profile exists locally and whether it's identical
    // Returns map with: exists, identical, source ("D"/"B"), filename
    QVariantMap checkProfileStatus(const QString& profileTitle, const Profile* incomingProfile = nullptr);

    // Load a local profile by filename (cascade: ProfileStorage -> downloaded -> built-in)
    Profile loadLocalProfile(const QString& filename) const;

    // Save profile to downloaded folder with duplicate detection.
    // On PendingResolution, a duplicateFound signal is emitted — caller must
    // wait for the user and then call saveOverwrite/saveAsNew/saveWithNewName.
    [[nodiscard]] SaveResult saveProfile(const Profile& profile, const QString& filename);

    // Duplicate resolution actions — only valid when hasPending() is true
    void saveOverwrite();
    void saveAsNew();
    void saveWithNewName(const QString& newName);
    void cancelPending();

    // Whether there is a pending profile awaiting duplicate resolution
    bool hasPending() const;

    // Path to the downloaded profiles folder.
    // Returns empty string if the directory does not exist and cannot be created.
    static QString downloadedProfilesPath();

    // Convert profile title to filename using MainController::titleToFilename()
    QString titleToFilename(const QString& title) const;

signals:
    void importSuccess(const QString& profileTitle);
    void importFailed(const QString& error);
    void duplicateFound(const QString& profileTitle, const QString& existingPath);

private:
    struct PendingResolution {
        Profile profile;
        QString filename;
    };

    QPointer<MainController> m_controller;
    std::optional<PendingResolution> m_pending;
};
