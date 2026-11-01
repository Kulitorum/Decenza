#pragma once

#include <QObject>
#include <QList>
#include <QPair>
#include <QPointer>
#include <QVariantMap>
#include <functional>
#include <utility>
#include <memory>

class AIManager;
class ProfileStorage;
class ScreensaverVideoManager;
class Settings;
class ShotHistoryStorage;
class TranslationManager;
class QTemporaryDir;

class FullBackupRestore final : public QObject
{
    Q_OBJECT
public:
    // Internal records are forward-declared here so the bounded parser can
    // build them in a worker without copying archive payloads into signals.
    struct PreparedArchive;
    struct ActiveRestore;
    struct StagedFile {
        QString source;
        QString temporary;
        QString destination;
        QString displayName;
    };
    struct Options {
        bool merge = true;
        bool shots = true;
        bool settings = true;
        bool profiles = true;
        bool media = true;
        bool preserveFlowCalibration = false;
    };

    FullBackupRestore(Settings* settings, ShotHistoryStorage* storage,
                      ProfileStorage* profiles, ScreensaverVideoManager* media,
                      AIManager* aiManager, TranslationManager* translations,
                      QObject* parent = nullptr);
    ~FullBackupRestore() override;

    bool busy() const;
    void setRestoreAllowed(std::function<bool()> predicate) { m_restoreAllowed = std::move(predicate); }
    bool restoreFile(const QString& requestId, const QString& path, const Options& options,
                     std::shared_ptr<QTemporaryDir> inputLease = {});

signals:
    void busyChanged();
    void profilesRestored();
    void mediaRestored();
    void finished(const QString& requestId, bool success, const QVariantMap& result,
                  const QString& reason, int legacyHttpStatus);

private:
    void applyNonShotData(const std::shared_ptr<PreparedArchive>& archive,
                          const Options& options,
                          std::function<void(bool)> completed);
    void applySettingsData(const std::shared_ptr<PreparedArchive>& archive,
                           const Options& options);
    void nonShotDataStaged(const QString& requestId, bool staged,
                           const std::shared_ptr<PreparedArchive>& archive,
                           const Options& options,
                           const std::shared_ptr<QList<StagedFile>>& profiles,
                           const std::shared_ptr<QList<StagedFile>>& media,
                           std::function<void(bool)> completed);
    void archivePrepared(const QString& requestId,
                         const std::shared_ptr<PreparedArchive>& archive,
                         const QString& error, int httpStatus);
    void importShots(const QString& requestId);
    void finishRestore(const QString& requestId, bool success,
                       QVariantMap result, QString reason = {}, int httpStatus = 200);

    QPointer<Settings> m_settings;
    QPointer<ShotHistoryStorage> m_storage;
    QPointer<ProfileStorage> m_profiles;
    QPointer<ScreensaverVideoManager> m_media;
    QPointer<AIManager> m_aiManager;
    QPointer<TranslationManager> m_translations;
    std::unique_ptr<ActiveRestore> m_active;
    std::function<bool()> m_restoreAllowed;
};
