#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QVector>
#include <QPointF>

#include "../history/shotprojection.h"

class ShotDataModel;
class Settings;
class Profile;
class DE1Device;

// DYE (Describe Your Espresso) metadata for shot uploads
struct ShotMetadata {
    QString beanBrand;
    QString beanType;
    QString roastDate;      // ISO format: YYYY-MM-DD
    QString roastLevel;     // Light, Medium, Dark
    QString grinderBrand;
    QString grinderModel;
    QString grinderBurrs;
    QString grinderSetting;
    double beanWeight = 0;  // Dose weight in grams
    double drinkWeight = 0; // Output weight in grams
    double drinkTds = 0;
    double drinkEy = 0;
    int espressoEnjoyment = 0;  // 0-100
    QString espressoNotes;
    QString barista;
    // Compact-JSON linked-bean snapshot ("" = unlinked, the common free-text
    // case) — Visualizer canonical or Bean Base sourced. Persisted per shot
    // so history stays accurate even after the preset is edited or deleted.
    QString beanBaseJson;

    // Active coffee bag snapshot (bean-bag-inventory): the bag the shot was
    // pulled with and its freeze lifecycle at shot time. Sentinel rule
    // (canonical — see ShotProjection): bagId <= 0 == no bag. Default -1. The
    // dates record the beans' thermal history permanently, even after the
    // bag's defrostDate moves on to the next portion.
    qint64 bagId = -1;
    QString frozenDate;   // ISO yyyy-MM-dd, "" = not frozen
    QString defrostDate;  // ISO yyyy-MM-dd, "" = not defrosted
};

class VisualizerUploader : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool uploading READ isUploading NOTIFY uploadingChanged)
    Q_PROPERTY(QString lastUploadStatus READ lastUploadStatus NOTIFY lastUploadStatusChanged)
    Q_PROPERTY(QString lastShotUrl READ lastShotUrl NOTIFY lastShotUrlChanged)

public:
    explicit VisualizerUploader(QNetworkAccessManager* networkManager, Settings* settings, QObject* parent = nullptr);

    bool isUploading() const { return m_uploading; }
    QString lastUploadStatus() const { return m_lastUploadStatus; }
    QString lastShotUrl() const { return m_lastShotUrl; }

    void setDevice(DE1Device* device) { m_device = device; }

    // Upload shot data to visualizer.coffee.
    // `dbShotId` is the local shots.id this upload is for, so a successful
    // upload can persist its returned Visualizer id to the right row from
    // C++ (MainController) without depending on any UI page. Live shots
    // are saved on a separate async path, so the caller passes the id it
    // captured from shotSaved.
    Q_INVOKABLE void uploadShot(ShotDataModel* shotData,
                                 const Profile* profile,
                                 double duration,
                                 double finalWeight = 0,
                                 double doseWeight = 0,
                                 const ShotMetadata& metadata = ShotMetadata(),
                                 const QString& debugLog = QString(),
                                 qint64 shotEpoch = 0,
                                 qint64 dbShotId = 0);

    // Upload a shot from history (takes the typed projection from
    // ShotHistoryStorage::convertShotRecord).
    Q_INVOKABLE void uploadShotFromHistory(const ShotProjection& shotData);

    // Upload with metadata overrides applied on top of a base shot.
    // baseShot is QVariant (not const ShotProjection&) so a QML caller can pass
    // EITHER a raw ShotProjection gadget OR an edited/cloned shot (a plain JS
    // object, e.g. clonePersistedShot's output) — ShotProjection::coerce()
    // accepts both. This is why a plain object is now a supported input rather
    // than something to avoid: coerce() reconstructs id/durationSec/frames that
    // a bare Object.assign on a Q_GADGET would have dropped (causing isValid()
    // to fail silently). C++ callers wrap with QVariant::fromValue(shot).
    Q_INVOKABLE void uploadShotFromHistoryWithOverrides(
        const QVariant& baseShot, const QVariantMap& overrides);

    // Update metadata on an already-uploaded shot (PATCH to visualizer.coffee)
    Q_INVOKABLE void updateShotOnVisualizer(const QString& visualizerId, const ShotProjection& shotData);

    // PATCH with overrides applied on top of a base shot. Fields not present in
    // overrides (notably profileName) are taken from the base record rather than
    // left empty. baseShot is QVariant for the same reason as above (coerce()).
    Q_INVOKABLE void updateShotOnVisualizerWithOverrides(
        const QString& visualizerId,
        const QVariant& baseShot,
        const QVariantMap& overrides);

    // Test connection with current credentials
    Q_INVOKABLE void testConnection();

    // --- Visualizer Coffee Management sync (bean-bag-inventory) ---
    // CM state is probed at upload time with a single-field PATCH on our own
    // just-uploaded shot (spike-verified: 200 = CM on, 400 = param dropped =
    // CM off; bag CRUD is premium-gated, not CM-gated). Cached per session;
    // testConnection() resets it so a toggle on visualizer.coffee converges
    // on the next upload.
    enum class CmState { Unknown, Active, NoCoffeeManagement, PremiumNoCm };
    CmState cmState() const { return m_cmState; }
    // Shot history DB path — needed to read the uploaded shot's bag row and
    // persist visualizerBagId/visualizerRoasterId back. Set by MainController.
    void setLocalDbPath(const QString& dbPath) { m_localDbPath = dbPath; }

    // Push a local bag edit to its already-synced Visualizer bag (PATCH
    // /api/coffee_bags/:id). No-op unless CM is Active and the bag has a
    // visualizerBagId (an unsynced bag is created later, on its next shot
    // upload). When the bag has a roaster name, re-resolves the roaster by that
    // name so a rename re-points roaster_id; with no roaster name it PATCHes the
    // descriptive fields alone. Caller (MainController) gates on
    // visualizerAutoUpdate and only invokes this for Visualizer-stored field
    // edits (CoffeeBagStorage::bagVisualizerFieldsChanged).
    Q_INVOKABLE void updateBagOnVisualizer(qint64 localBagId);

    // One-time reconciliation support: fetch the user's shot list
    // (GET /api/shots, paged) for shots whose start time (clock) is at
    // or after windowStartEpoch. Emits shotListFetched with a
    // QVariantList of {visualizerId, url, clockEpoch} maps, or
    // shotListFailed on any HTTP/parse error (fail safe — caller does
    // not advance its run-once flag on failure).
    void fetchShotListSince(qint64 windowStartEpoch);

    // Build a visualizer-compatible JSON payload from a ShotProjection.
    // Thread-safe; does not touch instance state. Reused by ShotHistoryExporter.
    static QByteArray buildHistoryShotJson(const ShotProjection& shotData);

signals:
    void uploadingChanged();
    void lastUploadStatusChanged();
    void lastShotUrlChanged();
    void uploadSuccess(const QString& shotId, const QString& url);
    // Like uploadSuccess but carries the originating local shots.id so
    // MainController can persist the link without a UI page. dbShotId is
    // 0 when the upload had no associated local row (should not happen
    // on the normal paths).
    void uploadSucceededForShot(qint64 dbShotId, const QString& visualizerId, const QString& url);
    void updateSuccess(const QString& visualizerId);
    void uploadFailed(const QString& error);
    // Emitted when an upload attempt is rejected by policy rather than
    // an actual failure (maintenance profile, too-short shot). Listeners that
    // track real failure conditions should NOT react to this — in particular
    // MainController's uploadFailed-based migration-16 abort logic deliberately
    // ignores skips so a routine policy rejection cannot kill the drain.
    void uploadSkipped(const QString& reason);
    void connectionTestResult(bool success, const QString& message);
    // Reconciliation list fetch results.
    void shotListFetched(const QVariantList& shots);
    void shotListFailed(const QString& error);

private slots:
    void onUploadFinished(QNetworkReply* reply);
    void onUpdateFinished(QNetworkReply* reply, const QString& visualizerId);
    void onTestFinished(QNetworkReply* reply);

private:
    QByteArray buildShotJson(ShotDataModel* shotData,
                             const Profile* profile,
                             double finalWeight,
                             double doseWeight,
                             const ShotMetadata& metadata,
                             const QString& debugLog,
                             qint64 shotEpoch = 0);

    QJsonObject buildVisualizerProfileJson(const Profile* profile);
    QByteArray buildMultipartData(const QByteArray& jsonData, const QString& boundary);
    QString authHeader() const;

    static QJsonObject buildAppInfoJson();
    static QJsonObject buildProfileSettings(const Profile* profile);
    bool validateUpload(const QString& beverageType, double duration);
    void sendUpload(const QByteArray& jsonData);

    // Paged reconciliation fetch: accumulates results across pages,
    // recurses until older than m_reconcileWindowStartEpoch or paging
    // exhausted, then emits shotListFetched once.
    void fetchShotListPage(int page, qint64 windowStartEpoch, QVariantList accumulated);

    // --- Coffee Management sync chain (bean-bag-inventory) ---
    // Entry point, called after a successful upload POST. Loads the shot's
    // bag from the local DB on a background thread, then drives the chain.
    void syncCoffeeBagAfterUpload(qint64 dbShotId, const QString& visualizerShotId);
    // Single-field probe PATCH on our own shot: 200 → Active, 400 → PremiumNoCm.
    // onCmActive runs (queued) when the probe confirms CM.
    void probeCmState(const QString& visualizerShotId, const QString& probeBagUuid,
                      std::function<void()> onCmActive);
    void resolveRoaster(const QString& visualizerShotId, const QVariantMap& bag);
    // Find-or-create a Visualizer roaster by name; calls onResolved(roasterId)
    // on success (not called on empty name or HTTP/parse failure). Shared by
    // the create chain (resolveRoaster) and the bag update path. Carries the
    // canonical roaster UUID onto a freshly-created roaster for the verified
    // badge. A 403 on create caches NoCoffeeManagement (CRUD is premium-gated).
    void resolveRoasterId(const QString& roasterName, const QString& canonicalRoasterId,
                          std::function<void(const QString& roasterId)> onResolved);
    void findRemoteBag(const QString& visualizerShotId, const QVariantMap& bag,
                       const QString& roasterId, int page = 1);
    void createRemoteBag(const QString& visualizerShotId, const QVariantMap& bag,
                         const QString& roasterId);
    // Add every Visualizer-stored descriptive field to `body` from a bag map
    // (name + roast/lifecycle/canonical + the beanBaseData blob attributes).
    // Omits roaster_id — the caller sets that. Shared by create and update so
    // both send the identical field set (the canonical id is a link, never a
    // substitute for the attributes — the server does not auto-fill them).
    void addBagDescriptiveFields(QJsonObject& body, const QVariantMap& bag) const;
    // PATCH /api/coffee_bags/:visualizerBagId with the descriptive fields, plus
    // roaster_id when `roasterId` differs from the bag's stored one (rename).
    // Persists the new visualizerRoasterId on a roaster change. 403 → not premium
    // (NoCoffeeManagement); 404 → remote bag deleted, id left stale and re-created
    // on the next shot upload. A bag PATCH is premium-gated, not CM-gated, so it
    // never resolves PremiumNoCm (only the shot-link probe can).
    void patchRemoteBag(const QVariantMap& bag, const QString& roasterId);
    void linkShotToBag(const QString& visualizerShotId, const QString& bagUuid,
                       const QString& canonicalId);
    void persistBagSyncIds(qint64 localBagId, const QString& visualizerBagId,
                           const QString& visualizerRoasterId);
    QNetworkRequest makeApiJsonRequest(const QString& path) const;

    // Single mutation point for m_cmState — every CM-probe transition flows
    // through here so there is one place to log old->new. The CM probing is
    // notoriously fiddly to debug; reads still use m_cmState / cmState()
    // directly. No-op when the state is unchanged.
    void setCmState(CmState state);

    Settings* m_settings;
    QNetworkAccessManager* m_networkManager;
    DE1Device* m_device = nullptr;
    bool m_uploading = false;
    QString m_lastUploadStatus;
    QString m_lastShotUrl;
    // The local shots.id the in-flight upload is for; emitted with
    // uploadSucceededForShot. A single member suffices because callers
    // (MainController shot-end, manual re-upload, history re-upload) are
    // mutually exclusive in practice and never issue overlapping
    // uploads. NOTE: m_uploading is a UI state flag, NOT a concurrency
    // guard — nothing rejects a second uploadShot() while one is in
    // flight. Do not add a concurrent upload caller without revisiting
    // this correlation (it would mis-attribute the returned id).
    // Reset after each terminal outcome.
    qint64 m_uploadingDbShotId = 0;

    // Coffee Management sync state (see CmState above).
    CmState m_cmState = CmState::Unknown;
    QString m_localDbPath;

    static constexpr const char* VISUALIZER_API_URL = "https://visualizer.coffee/api/shots/upload";
    static constexpr const char* VISUALIZER_SHOTS_API_URL = "https://visualizer.coffee/api/shots/";
    static constexpr const char* VISUALIZER_SHOT_URL = "https://visualizer.coffee/shots/";
};
