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

    // Last-used grinder/dose (write-through from edits, dose/yield stamped
    // on shot save). Slated to move to a future Equipment abstraction.
    QString grinderBrand;
    QString grinderModel;
    QString grinderBurrs;
    QString grinderSetting;
    double doseWeightG = 0;  // 0 = unset
    double yieldTargetG = 0; // 0 = unset

    // Visualizer Coffee Management sync state
    QString visualizerBagId;
    QString visualizerRoasterId;

    qint64 lastUsedEpoch = 0; // bumped on selection and shot save (MRU ordering)

    // Transient: number of shots referencing this bag. Populated by
    // loadInventoryStatic's subquery only (not a coffee_bags column) —
    // drives the card's delete-vs-finished action. 0 from other loaders.
    qint64 shotCount = 0;

    bool isValid() const { return id > 0; }
    QVariantMap toVariantMap() const;
    static CoffeeBag fromVariantMap(const QVariantMap& map);
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
    Q_INVOKABLE void requestUpdateBag(qint64 bagId, const QVariantMap& fields); // bagUpdated()
    Q_INVOKABLE void requestMarkEmpty(qint64 bagId);                    // bagUpdated()
    Q_INVOKABLE void requestSetDefrostToday(qint64 bagId);              // "Next Portion": defrostDate = today
    Q_INVOKABLE void requestTouchLastUsed(qint64 bagId);                // bump MRU timestamp (no bagUpdated)
    // Deletes only when no shot references the bag (shots.bag_id count = 0);
    // emits bagDeleted(bagId, success) — success false when shots exist.
    Q_INVOKABLE void requestDeleteBag(qint64 bagId);

    // --- Synchronous static helpers (caller provides the connection) ---

    // Create the coffee_bags table if missing. Used by migration 19 and tests.
    static bool ensureTableStatic(QSqlDatabase& db);

    static qint64 insertBagStatic(QSqlDatabase& db, const CoffeeBag& bag);
    static CoffeeBag loadBagStatic(QSqlDatabase& db, qint64 bagId);
    static QVector<CoffeeBag> loadInventoryStatic(QSqlDatabase& db);
    // Update only the columns named in `fields` (camelCase CoffeeBag keys).
    static bool updateBagFieldsStatic(QSqlDatabase& db, qint64 bagId, const QVariantMap& fields);

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
    static bool importBagsStatic(QSqlDatabase& srcDb, QSqlDatabase& destDb, bool merge,
                                 QHash<qint64, qint64>& outIdMap);

signals:
    void inventoryReady(const QVariantList& bags);
    void bagReady(qint64 bagId, const QVariantMap& bag);   // bag empty if not found
    void bagCreated(qint64 bagId, const QVariantMap& bag); // bagId -1 on failure
    void bagUpdated(qint64 bagId, bool success);
    void bagDeleted(qint64 bagId, bool success);
    // Coarse "something changed" signal so views can re-request the inventory.
    void bagsChanged();

private:
    // Run `work(db)` on a background thread, then `done` on the main thread.
    void runAsync(const QString& connPrefix,
                  std::function<void(QSqlDatabase&)> work,
                  std::function<void()> done);

    static CoffeeBag bagFromQueryRow(const class QSqlQuery& query);

    QString m_dbPath;
    std::shared_ptr<std::atomic<bool>> m_destroyed = std::make_shared<std::atomic<bool>>(false);
};
