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
class TranslationManager;

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
    qint64 equipmentId = 0; // Equipment package (add-equipment-packages); 0 = none
    qint64 rpm = 0;         // Grinder rpm dial-in; 0 = unset
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
    // pulled with and its freeze lifecycle at shot time. "No bag" sentinel is
    // bagId <= 0 (default -1) — see bagIdIsSet() in bagid.h. The dates record
    // the beans' thermal history permanently, even after the bag's defrostDate
    // moves on to the next portion.
    qint64 bagId = -1;
    QString frozenDate;   // ISO yyyy-MM-dd, "" = not frozen
    QString defrostDate;  // ISO yyyy-MM-dd, "" = not defrosted
    // Non-frozen storage lifecycle (bean-freshness-followup): the bag's
    // storage category and opened date at shot time. Local history only —
    // not part of the Visualizer upload payload.
    QString storageHint;  // counter/airtight/vacuum-sealed/fridge, "" = unset
    QString openedDate;   // ISO yyyy-MM-dd, "" = not opened

    // Recipe provenance (add-recipes): the recipe active at shot start
    // (<= 0 = none) and compact-JSON snapshots of the steam and hot-water specs
    // in effect, so promote-from-shot round-trips the whole drink. Local history
    // only — not part of the Visualizer upload payload.
    qint64 recipeId = -1;
    QString steamJson;
    QString hotWaterJson;
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

    // Inject the TranslationManager so user-visible upload/status/error strings
    // localize (mirrors VisualizerImporter). Wired from
    // MainController::setTranslationManager. Until injected, tr_() returns the
    // English fallback.
    void setTranslationManager(TranslationManager* tm) { m_translationManager = tm; }

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

    // The descriptive-field PATCH body to enrich a server coffee bag: only the
    // fields we hold locally that the server left blank (fill-blanks, never
    // clobbering a user-set value). Pure (no network) and public so the
    // blob→API field mapping and the fill-blanks contract are unit-tested.
    static QJsonObject buildBagEnrichBody(const QJsonObject& remoteBag, const QVariantMap& bag);

    // Every Visualizer-stored descriptive field from a bag map (name +
    // roast/lifecycle/canonical + the beanBaseData blob attributes), added to
    // `body` at CURRENT values (empty locals omitted — never sent as null).
    // Omits roaster_id — the caller sets that. Used by the bag-edit path
    // (patchRemoteBag), which overwrites the full set on an explicit user
    // edit. Pure + public so the blob→API mapping is unit-tested.
    static void addBagDescriptiveFields(QJsonObject& body, const QVariantMap& bag);

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
    // Correlated failure for the PATCH path (updateShotOnVisualizer and
    // its WithOverrides wrapper): carries the target visualizerId (empty
    // only on the no-id guard path) plus whether the failure is permanent
    // (HTTP 404 — the shot does not exist on Visualizer and never will),
    // so queue-drain listeners (MainController's migration16 sync, #1431)
    // can evict poison entries instead of retrying them on every boot.
    // Emitted alongside uploadFailed, which remains uncorrelated by
    // design for the UI status listeners.
    void updateFailed(const QString& visualizerId, bool permanent, const QString& error);
    // Emitted when an upload attempt is rejected by policy rather than
    // an actual failure (maintenance profile, too-short shot). Listeners
    // that track real failure conditions should NOT react to this. The
    // migration-16 drain is safe by construction: it listens only to the
    // PATCH-correlated updateFailed, which upload-policy skips never emit.
    void uploadSkipped(const QString& reason);
    void connectionTestResult(bool success, const QString& message);
    // A bag edit-push was rejected by server validation (HTTP 422 — e.g. the
    // renamed bag collides with an existing roaster+name+roast_date, or the
    // defrost date precedes the frozen date). Definitive: not retried; local
    // values stay as edited. Carries the bag's display name (a 422 can fire
    // from the retry drain long after the edit) and the server's message for
    // a one-shot non-blocking toast (add-bag-detail-editing).
    void bagPushRejected(qint64 localBagId, const QString& bagName, const QString& message);
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

    // Translate a user-visible string via the injected TranslationManager,
    // falling back to the English source when none is set.
    QString tr_(const char* key, const char* fallback) const;

    static QJsonObject buildAppInfoJson();
    static QJsonObject buildProfileSettings(const Profile* profile);
    bool validateUpload(const QString& beverageType, double duration);
    void sendUpload(const QByteArray& jsonData);

    // Paged reconciliation fetch: accumulates results across pages,
    // recurses until older than m_reconcileWindowStartEpoch or paging
    // exhausted, then emits shotListFetched once.
    void fetchShotListPage(int page, qint64 windowStartEpoch, QVariantList accumulated);

    // --- Coffee Management sync (bean-bag-inventory) ---
    // Entry point, called after a successful upload POST. Loads the shot's bag
    // from the local DB on a background thread, then reconciles it. No-op for
    // CM-off accounts (cached per session).
    void syncCoffeeBagAfterUpload(qint64 dbShotId, const QString& visualizerShotId);
    // Read the shot back to learn the coffee_bag the SERVER auto-linked (it
    // find-or-creates the bag from bean_brand/bean_type/roast_date on every
    // upload). An empty coffee_bag_id means CM is off → cache the negative.
    // Otherwise capture the authoritative ids (self-healing a stale local id)
    // and enrich. Replaces the old guess/probe/find-or-create chain.
    void reconcileShotBag(const QString& visualizerShotId, const QVariantMap& bag);
    // GET the server bag and PATCH only the descriptive fields it left blank
    // (origin/region/producer/etc. + lifecycle + canonical link). Never touches
    // server-managed name/roast_date/roast_level, and never clobbers a value the
    // user set on visualizer.coffee. 404 → bag deleted mid-flight, clear local id.
    void enrichRemoteBag(const QString& serverBagId, const QVariantMap& bag);
    // Best-effort verified-roaster badge: link the server-created roaster to its
    // canonical when blank (the badge is cosmetic, so failures are ignored).
    void enrichRemoteRoaster(const QString& roasterId, const QString& canonicalRoasterId);
    // PATCH the shot's canonical_coffee_bag_id (not CM-gated). Canonical-only
    // mode: attaches a known coffee to a shot with no personal bag.
    void linkShotCanonical(const QString& visualizerShotId, const QString& canonicalId);
    // Find-or-create a Visualizer roaster by name; calls onResolved(roasterId)
    // on success (not called on empty name or HTTP/parse failure). Carries the
    // canonical roaster UUID onto a freshly-created roaster for the verified
    // badge. A 403 on create caches NoCoffeeManagement (CRUD is premium-gated).
    // Used by the bag-edit path (updateBagOnVisualizer).
    void resolveRoasterId(const QString& roasterName, const QString& canonicalRoasterId,
                          std::function<void(const QString& roasterId)> onResolved);
    // PATCH /api/coffee_bags/:visualizerBagId with the descriptive fields, plus
    // roaster_id when `roasterId` differs from the bag's stored one (rename).
    // Persists the new visualizerRoasterId on a roaster change. 403 → not premium
    // (NoCoffeeManagement); 404 → remote bag deleted, id left stale and re-created
    // on the next shot upload. The bag-edit counterpart to enrichRemoteBag.
    void patchRemoteBag(const QVariantMap& bag, const QString& roasterId);
    void persistBagSyncIds(qint64 localBagId, const QString& visualizerBagId,
                           const QString& visualizerRoasterId);
    // Set/clear coffee_bags.visualizer_sync_pending (background write, no
    // signals). Park-first contract: updateBagOnVisualizer SETS it before any
    // network I/O (and when parking during CM-Unknown); it is CLEARED only by
    // an outcome — patchRemoteBag's reply (200/403/404/422) or the not-synced-
    // yet skip. Failures that never produce a reply (roaster-list GET dying
    // offline, roaster create dropped, bag load failure) therefore leave it
    // set for the next upload cycle's retry. One deliberate leak: a roaster-
    // create 403 flips CM off with the flag still set — inert (retry requires
    // Active) and self-draining if premium returns.
    void persistBagSyncPending(qint64 localBagId, bool pending);
    // Re-push every sync-pending bag. Called from the upload read-back once CM
    // is confirmed Active (add-bag-detail-editing) — the event-driven retry for
    // offline/5xx-failed edit pushes.
    void retrySyncPendingBags();
    QNetworkRequest makeApiJsonRequest(const QString& path) const;

    // Single mutation point for m_cmState — every CM-probe transition flows
    // through here so there is one place to log old->new. The CM probing is
    // notoriously fiddly to debug; reads still use m_cmState / cmState()
    // directly. No-op when the state is unchanged.
    void setCmState(CmState state);

    Settings* m_settings;
    QNetworkAccessManager* m_networkManager;
    DE1Device* m_device = nullptr;
    TranslationManager* m_translationManager = nullptr;
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

    // Bounded auto-retry for the upload POST on a transient failure (transport
    // error/timeout or 5xx — never auth/validation/429). Same single-in-flight
    // assumption as m_uploadingDbShotId. m_uploadRetries is reset at each public
    // entry (uploadShot/uploadShotFromHistory); m_lastUploadJson is refreshed in
    // sendUpload() before every POST (including retries) so the re-POST needs no
    // shot state.
    QByteArray m_lastUploadJson;
    int m_uploadRetries = 0;
    static constexpr int kMaxUploadRetries = 2;

    // Coffee Management sync state (see CmState above).
    CmState m_cmState = CmState::Unknown;
    QString m_localDbPath;

    static constexpr const char* VISUALIZER_API_URL = "https://visualizer.coffee/api/shots/upload";
    static constexpr const char* VISUALIZER_SHOTS_API_URL = "https://visualizer.coffee/api/shots/";
    static constexpr const char* VISUALIZER_SHOT_URL = "https://visualizer.coffee/shots/";
};
