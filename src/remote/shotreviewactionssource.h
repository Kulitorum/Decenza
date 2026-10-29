#pragma once

#include "rep_shotreviewactions_source.h"
#include "serviceoperation.h"
#include "shotreviewsessions.h"
#include "history/shotfileexport.h"
#include "core/shotreviewbaseline.h"

#include <QPointer>

class MainController;
class Settings;
class BLEManager;
class RefractometerProxy;
class RemoteAccess;
class OpaqueFileBundleStore;

class NativeShotReviewActionSource final : public ShotReviewActionStateSimpleSource
{
    Q_OBJECT
public:
    explicit NativeShotReviewActionSource(MainController* controller, Settings* settings,
                                          BLEManager* bleManager, RefractometerProxy* refractometer,
                                          QObject* parent = nullptr);

    // Whether the remote connector is live decides if Discuss pastes the shot.
    void setRemoteAccess(RemoteAccess* remoteAccess);
    void execute(QString requestId, QString operation, QVariantMap arguments) override;
    void openClient(const QString& clientId, bool native = false);
    void closeClient(const QString& clientId);

private:
    void publishUploadState();
    void finish(const QString& requestId, const QString& operation, bool success,
                const QVariantMap& result = {}, const QString& reason = {});
    bool validRequestId(const QString& requestId) const;
    void publishState();
    void publishRefractometerState();
    void synchronizeReviewSessions();
    void completeMeasurement();
    void clearExport(const QString& reviewId);
    void prepareExport(const QString& requestId, const QString& clientId,
                       const QString& reviewId, qint64 shotId, ShotFileExport::Format format);
    void finishCarryPreview(const QString& requestId, qint64 shotId,
                            const QVariantMap& shot, const QString& reason);
    void requestCarryRecipe(const QString& requestId, const QVariantMap& proposed,
                            const ShotReviewSetup& expected, const QVariantMap& bag);
    void completeCarryPreview(const QString& requestId, const QVariantMap& proposed,
                              const ShotReviewSetup& expected, const QVariantMap& bag,
                              const QVariantMap& recipe);
    void failCarryPreview(const QString& requestId, const QString& reason);
    void invalidateCarryProposals(bool recordChanged = false);
    void invalidateCarryForShot(qint64 shotId);
    void invalidateCarryForRecord(const QString& kind, qint64 id);
    bool carryApplyCurrent(const QString& clientId, const QString& reviewId,
                           const QString& proposalId, qint64 shotId,
                           const ShotReviewSetup& expected, quint64 operation) const;
    bool m_carryApplyBusy = false;

    struct ExportFile {
        QString clientId;
        QString requestId;
        QPointer<OpaqueFileBundleStore> files;
    };
    QHash<QString, ExportFile> m_exportFiles;

    struct ReviewBaseline {
        QString clientId;
        qint64 shotId = 0;
        ShotReviewSetup setup;
        quint64 operationGeneration = 0;
    };
    QHash<QString, ReviewBaseline> m_reviewBaselines;
    struct ReviewProposal {
        QString requestId;
        QVariantMap patch;
        ShotReviewSetup expected;
        QVariantMap persistentPlan;
        QVariantMap bag;
        QVariantMap recipe;
    };
    QHash<QString, ReviewProposal> m_reviewProposals;
    struct PendingCarryPreview {
        QString clientId;
        QString reviewId;
        qint64 shotId = 0;
        QVariantMap opening;
        QVariantMap saved;
        bool shotChanged = false;
        bool recordChanged = false;
    };
    QHash<QString, PendingCarryPreview> m_pendingCarryPreviews;
    QHash<QString, QString> m_latestCarryPreview;
    QHash<QString, qint64> m_pendingReviewShotWrites;

    QPointer<MainController> m_controller;
    QPointer<Settings> m_settings;
    QPointer<BLEManager> m_bleManager;
    QPointer<RefractometerProxy> m_refractometer;
    QPointer<RemoteAccess> m_remoteAccess;
    ServiceRequestIds m_requests;
    ShotReviewSessions m_sessions;
    quint64 m_measurementGeneration = 0;
};
