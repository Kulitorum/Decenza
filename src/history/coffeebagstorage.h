#pragma once

#include <QObject>
#include <QHash>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <atomic>
#include <functional>
#include <memory>

class QSqlDatabase;
class QJsonArray;
class QJsonObject;
class SerialDbWorker;

// A coffee bag: the single bean concept that replaced bean presets
// (openspec change bean-bag-inventory). A bag IS the active bean state —
// shots snapshot its fields at save time, edits write through to it, and
// "in inventory" doubles as idle-page visibility (no showOnIdle flag).
// All string dates are ISO yyyy-MM-dd; empty string = unset (stored NULL).
struct CoffeeBag {
    qint64 id = 0;

    // Identity
    QString roasterName;
    QString coffeeName;
    QString roastDate;       // empty = unknown roast date (allowed)
    QString roastLevel;
    QString beanBaseId;      // canonical UUID, empty = unlinked
    QString beanBaseData;    // compact-JSON canonical snapshot, empty = none

    // Lifecycle. frozenDate/defrostDate describe the CURRENT portion only —
    // the defrost history lives in per-shot snapshots.
    QString frozenDate;
    QString defrostDate;
    QString notes;
    double startWeightG = 0; // 0 = unset; local-only, never synced to Visualizer
    bool inInventory = true;

    // Grinder identity is no longer persisted on the bag (migration 23 dropped
    // the grinder_brand/model/burrs columns); it resolves through equipmentId to
    // the package's grinder item. These fields are a display cache: not stored as
    // columns, but populated at load by loadBagStatic via a JOIN on
    // equipment_items (so consumers like MCP bag_list see the grinder identity).
    QString grinderBrand;
    QString grinderModel;
    QString grinderBurrs;
    // Bean-scoped dial memory (write-through from edits, stamped on shot save).
    QString grinderSetting;
    double doseWeightG = 0;  // 0 = unset
    double yieldOverrideG = 0; // 0 = unset

    // Equipment (add-equipment-packages). equipmentId points at the bag's
    // grinder package; rpm is the grinder rpm dial-in (sibling of grinderSetting).
    qint64 equipmentId = 0;  // FK -> equipment_packages.id; 0 = unset
    qint64 rpm = 0;          // 0 = unset

    // Visualizer Coffee Management sync state
    QString visualizerBagId;
    QString visualizerRoasterId;

    qint64 lastUsedEpoch = 0; // bumped on selection and shot save (MRU ordering)

    bool isValid() const { return id > 0; }
    QVariantMap toVariantMap() const;
    static CoffeeBag fromVariantMap(const QVariantMap& map);
};

// An inventory row: a bag plus its shot count. The count is NOT a CoffeeBag
// field — it is a per-query aggregate (a subquery on shots.bag_id) that only
// the inventory listing computes, and it drives the card's delete-vs-finished
// action (0 shots = mistaken creation, trashable; >0 = history, "Bag finished").
// Keeping it out of CoffeeBag makes "this count is inventory-view-only" a type
// fact: the other loaders return bare CoffeeBags and can't expose a stale 0.
struct InventoryBag {
    CoffeeBag bag;
    qint64 shotCount = 0;
};

// SQLite-backed bag storage in the shot history database (coffee_bags table,
// created by ShotHistoryStorage migration 19). All public request* methods are
// async: DB work runs on a QThread::create() background thread and results are
// delivered back via signals, following the ShotHistoryStorage pattern. The
// *Static helpers are synchronous, take a caller-provided connection, and are
// shared with the migration, SettingsSerializer import, and unit tests.
class CoffeeBagStorage : public QObject {
    Q_OBJECT

public:
    explicit CoffeeBagStorage(QObject* parent = nullptr);
    ~CoffeeBagStorage();

    // dbPath must be the shot history database (table lives there).
    void initialize(const QString& dbPath);
    QString databasePath() const { return m_dbPath; }

    // Async queries — results via signals (QVariantList of toVariantMap()).
    Q_INVOKABLE void requestInventory();                   // inInventory = true, MRU order
    Q_INVOKABLE void requestBag(qint64 bagId);             // bagReady()

    // Async writes — all emit bagsChanged() on success.
    Q_INVOKABLE void requestCreateBag(const QVariantMap& bag);          // bagCreated()
    // propagateBeanBase: after the update, copy the bag's (possibly empty)
    // canonical link onto ALL shots referencing it — the edit-dialog
    // "upgrade this bag to Bean Base" flow, where linking the bag fixes its
    // whole history. Runs in the same background job so it cannot race the
    // field update. Default off: routine edits and write-throughs must not
    // rewrite shot snapshots.
    Q_INVOKABLE void requestUpdateBag(qint64 bagId, const QVariantMap& fields,
                                      bool propagateBeanBase = false); // bagUpdated()
    Q_INVOKABLE void requestMarkEmpty(qint64 bagId);                    // bagUpdated()
    Q_INVOKABLE void requestTouchLastUsed(qint64 bagId);                // bump MRU timestamp (no bagUpdated)
    // Deletes only when no shot references the bag (shots.bag_id count = 0);
    // emits bagDeleted(bagId, success) — success false when shots exist.
    Q_INVOKABLE void requestDeleteBag(qint64 bagId);

    // --- Synchronous static helpers (caller provides the connection) ---

    // Create the coffee_bags table if missing. Used by migration 19 and tests.
    static bool ensureTableStatic(QSqlDatabase& db);

    static qint64 insertBagStatic(QSqlDatabase& db, const CoffeeBag& bag);
    static CoffeeBag loadBagStatic(QSqlDatabase& db, qint64 bagId);
    static QVector<InventoryBag> loadInventoryStatic(QSqlDatabase& db);
    // Update only the columns named in `fields` (camelCase CoffeeBag keys).
    static bool updateBagFieldsStatic(QSqlDatabase& db, qint64 bagId, const QVariantMap& fields);

    // True when `fields` (camelCase CoffeeBag keys) contains at least one field
    // that Visualizer stores on its coffee bag — the identity/lifecycle/canonical
    // fields, NOT the local-only grinder/dose/yield/lifecycle-id columns. Drives
    // the bagVisualizerFieldsChanged signal so a Visualizer PATCH fires only for
    // edits the remote bean record actually reflects (a grinder write-through or
    // dose/yield stamp must not hit the network). Single source of truth for the
    // local-key → Visualizer-field mapping.
    static bool touchesVisualizerFields(const QVariantMap& fields);

    // The bag's yield override is the shot's target weight ONLY when it differs
    // from the profile's default target; a plain profile-default pour stores 0
    // (no override), so it doesn't pin the bag to the profile's own number (and
    // doesn't turn the idle brew-settings widget yellow). Shared by the shot-save
    // stamp (MainController) and the brew-settings commit (ProfileManager) so the
    // "is this an override?" rule lives in exactly one tested place.
    static double yieldOverrideForTarget(double shotTargetWeightG, double profileTargetWeightG);

    // Convert one legacy bean preset JSON object (SettingsDye "bean/presets"
    // entry: name/brand/type/roastDate/roastLevel/grinder*/barista/showOnIdle/
    // beanBaseId/beanBaseData) into a CoffeeBag. Preset `name` lands in notes
    // when it differs from "{brand} {type}"; barista and showOnIdle are
    // intentionally dropped (per-shot concept / superseded by inInventory).
    static CoffeeBag bagFromLegacyPreset(const QJsonObject& preset);

    // Merge-import legacy presets: inserts presets that do not match an
    // existing bag (case-insensitive roasterName+coffeeName+roastDate), skips
    // matches. Shared by migration 19 and SettingsSerializer import. Returns
    // the number of bags inserted; appends the bag id created for index
    // `selectedIndex` (or the matched existing bag's id) to *outSelectedBagId
    // when both are non-null/valid.
    static int importLegacyPresetsStatic(QSqlDatabase& db, const QJsonArray& presets,
                                         int selectedIndex = -1,
                                         qint64* outSelectedBagId = nullptr);

    // Copy the bag's canonical link (beanbase_id + beanbase_json, possibly
    // empty = unlink) onto every shot whose bag_id references it. Returns
    // the number of shots updated, -1 on failure.
    static int propagateBeanBaseStatic(QSqlDatabase& db, qint64 bagId);

    // Link orphan shots (bag_id NULL) to bags by identity — two passes:
    // exact (case-insensitive roaster+coffee+roast_date), then identity-only
    // for the leftovers, preferring the most recently used bag. Idempotent
    // (only touches NULL bag_id rows). Used by migration 20 (repairs
    // upgraded devices whose migrated preset-bags predate their shots) and
    // by importDatabaseStatic for pre-bag backup sources. Returns the
    // number of shots linked, -1 on failure.
    static int linkOrphanShotsStatic(QSqlDatabase& db);

    // Resolve which bag a historical shot belongs to: the shot's own bag_id
    // link when it exists (snapshot from save time), else the best identity
    // match (case-insensitive roaster+coffee; in-inventory and most recently
    // used first — covers pre-bag shots). Returns -1 when nothing matches.
    static qint64 findBagForShotStatic(QSqlDatabase& db, qint64 shotId,
                                       const QString& roasterName, const QString& coffeeName);

    // Reads the legacy bean/presets + bean/selectedPreset QSettings keys,
    // merge-imports them into coffee_bags in the given database, and clears
    // the keys only after a successful commit. Returns the bag id the
    // selected preset mapped to, or -1 (none selected, nothing to import, or
    // failure — failure leaves QSettings intact for retry). Shared by
    // ShotHistoryStorage's launch-time import and SettingsSerializer's
    // mid-session legacy import; safe alongside an open main connection
    // (WAL + busy_timeout via withTempDb).
    static qint64 convertLegacyPresetSettings(const QString& dbPath);

    // Copy coffee_bags rows from srcDb into destDb (device transfer / backup
    // restore). Row ids change on insert — outIdMap records old->new so the
    // caller can remap shots.bag_id. Merge mode maps a source bag matching an
    // existing dest bag (case-insensitive roaster+coffee+roastDate) to the
    // existing row instead of inserting a duplicate; replace mode clears
    // dest bags first. Source DBs from before migration 19 have no
    // coffee_bags table — returns true with an empty map. Runs inside the
    // caller's destDb transaction.
    // packageIdMap remaps each source bag's equipment_id to the imported
    // package's new dest id (built by EquipmentStorage::importEquipmentStatic,
    // which runs first). A source equipment_id absent from the map (e.g. an
    // older source with no equipment tables) becomes NULL.
    static bool importBagsStatic(QSqlDatabase& srcDb, QSqlDatabase& destDb, bool merge,
                                 QHash<qint64, qint64>& outIdMap,
                                 const QHash<qint64, qint64>& packageIdMap);

signals:
    void inventoryReady(const QVariantList& bags);
    void bagReady(qint64 bagId, const QVariantMap& bag);   // bag empty if not found
    void bagCreated(qint64 bagId, const QVariantMap& bag); // bagId -1 on failure
    void bagUpdated(qint64 bagId, bool success);
    void bagDeleted(qint64 bagId, bool success);
    // Emitted (after a successful update) only when the edit touched a field
    // Visualizer stores on the bean — see touchesVisualizerFields(). The
    // MainController gates on visualizerAutoUpdate + CM-active before PATCHing.
    void bagVisualizerFieldsChanged(qint64 bagId);
    // Coarse "something changed" signal so views can re-request the inventory.
    void bagsChanged();

private:
    // Run `work(db)` on a background thread, then `done(dbOpened)` on the main
    // thread. Read callers must skip their "Ready" emission when dbOpened is
    // false (open failure → empty result that must not be read as not-found).
    void runAsync(const QString& connPrefix,
                  std::function<void(QSqlDatabase&)> work,
                  std::function<void(bool dbOpened)> done);

    static CoffeeBag bagFromQueryRow(const class QSqlQuery& query);

    QString m_dbPath;
    std::shared_ptr<std::atomic<bool>> m_destroyed = std::make_shared<std::atomic<bool>>(false);
    // Serializes all background DB work onto one FIFO worker thread so successive
    // writes to the same row apply in submission order (see SerialDbWorker).
    std::unique_ptr<SerialDbWorker> m_dbWorker;
};
