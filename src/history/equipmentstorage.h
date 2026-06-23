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
class QSqlQuery;
class SerialDbWorker;

// An equipment item: one typed component of a package. The kinds today are
// "grinder", the optional "basket" (add-basket-equipment), and the optional
// "puckprep" (add-puckprep-equipment); future kinds (tamper, …) are new rows with
// a different `kind` and their own `attrs` payload — no schema migration (openspec
// change add-equipment-packages). Shared fields every kind has (kind/brand/model)
// are real columns; kind-specific fields live in the `attrs` JSON blob. For a
// grinder, attrs = { "burrs": "...", "rpmCapable": true }. The `burrs` and
// `rpmCapable` members below are convenience views of that blob, (de)serialized on
// load/save — they are not their own columns. A basket has NO kind-specific attrs:
// its identity is brand+model and every spec is derived from BasketAliases at read
// time. A puckprep item stores its canonical flag string (PuckPrep::canonical) in
// the `model` column and an empty attrs blob; its flags + derived `distribution`
// are reconstructed from that string at read time (see core/puckprep.h).
struct EquipmentItem {
    qint64 id = 0;
    qint64 packageId = 0;
    QString kind = QStringLiteral("grinder");
    QString brand;
    QString model;

    // Grinder-kind attrs (mirrored into/out of the `attrs` JSON column). Only
    // written/read when kind == "grinder"; a basket item leaves these unset.
    QString burrs;
    bool rpmCapable = false;

    bool isValid() const { return id > 0; }
    // Serialize the kind-specific members into the attrs JSON string.
    QString attrsJson() const;
    // Populate the kind-specific members from an attrs JSON string.
    void setAttrsFromJson(const QString& json);
};

// An equipment package: the switchable container the active bag points at via
// coffee_bags.equipment_id (and shots.equipment_id snapshots). It owns one or
// more EquipmentItems (exactly one grinder today). The package also carries the
// grinder-scoped "last dial" memory — switching to a package applies these to
// the active bag's grind setting / rpm (the bean-scoped memory lives on the bag).
struct EquipmentPackage {
    qint64 id = 0;

    QString name;            // optional; UI falls back to "{brand} {model}"
    bool inInventory = true; // soft-delete: removed packages keep their row

    // Grinder-scoped dial memory (applied on equipment switch).
    QString lastGrindSetting;
    qint64 lastRpm = 0;      // 0 = unset

    qint64 lastUsedEpoch = 0; // MRU ordering (bumped on selection / shot save)

    // Copy-on-write lineage: when an identity edit forks a new package, the old
    // one is soft-deleted (inInventory=0) and points here at the fork's id. 0 =
    // not superseded. Powers "older version" display + dial-history-across-edits.
    qint64 supersededBy = 0;

    bool isValid() const { return id > 0; }
    QVariantMap toVariantMap() const;
    static EquipmentPackage fromVariantMap(const QVariantMap& map);
};

// A package plus its resolved grinder item, optional basket item, and shot
// count, for the inventory list. shotCount is a per-query aggregate
// (shots.equipment_id), not a package field — it drives the card's
// delete-vs-remove action exactly like InventoryBag. `basket` is invalid
// (id == 0) when the package has no basket.
struct EquipmentPackageView {
    EquipmentPackage package;
    EquipmentItem grinder;
    EquipmentItem basket;
    EquipmentItem puckPrep;  // invalid (id == 0) when the package has no puck prep
    qint64 shotCount = 0;
    // Flatten package + grinder identity + basket identity/derived specs +
    // puck-prep flags/derived distribution (+ shotCount) into one map for QML/MCP.
    QVariantMap toVariantMap() const;
};

// SQLite-backed equipment storage in the shot history database
// (equipment_packages + equipment_items tables, created by ShotHistoryStorage
// migration 22). Async request* methods run DB work on a QThread::create()
// background thread and deliver results via signals — the CoffeeBagStorage
// pattern. The *Static helpers are synchronous, take a caller-provided
// connection, and are shared with the migration, device import, and tests.
class EquipmentStorage : public QObject {
    Q_OBJECT

public:
    explicit EquipmentStorage(QObject* parent = nullptr);
    ~EquipmentStorage();

    void initialize(const QString& dbPath);
    QString databasePath() const { return m_dbPath; }

    // Async queries — results via signals.
    Q_INVOKABLE void requestInventory();                       // inventoryReady()
    Q_INVOKABLE void requestPackage(qint64 packageId);         // packageReady()

    // Async writes — all emit packagesChanged() on success.
    // The map carries package fields plus grinder identity (brand/model/burrs);
    // rpmCapable is derived from the registry, not taken from the map.
    Q_INVOKABLE void requestCreatePackage(const QVariantMap& package);     // packageCreated()
    Q_INVOKABLE void requestUpdatePackage(qint64 packageId, const QVariantMap& fields); // packageUpdated()
    Q_INVOKABLE void requestMarkRemoved(qint64 packageId);                 // soft-delete (packageUpdated)
    Q_INVOKABLE void requestTouchLastUsed(qint64 packageId);              // bump MRU (no signal)
    // Deletes only when no bag/shot references the package; emits
    // packageDeleted(packageId, success) — success false when references exist.
    Q_INVOKABLE void requestDeletePackage(qint64 packageId);

    // --- Synchronous static helpers (caller provides the connection) ---

    static bool ensureTablesStatic(QSqlDatabase& db);

    static qint64 insertPackageStatic(QSqlDatabase& db, const EquipmentPackage& pkg);
    static qint64 insertItemStatic(QSqlDatabase& db, const EquipmentItem& item);
    // Create a package, its single grinder item, and (optionally) a basket item in
    // one call; returns the new package id (-1 on failure). rpmCapable is derived
    // from the registry. An empty basketBrand+basketModel creates a grinder-only
    // package. pkg is taken by value: a name is persisted at creation (defaults to
    // "{brand} {model}") so it survives identity edits / copy-on-write.
    static qint64 createPackageWithGrinderStatic(QSqlDatabase& db, EquipmentPackage pkg,
                                                 const QString& brand, const QString& model,
                                                 const QString& burrs,
                                                 const QString& basketBrand = QString(),
                                                 const QString& basketModel = QString(),
                                                 const QString& puckPrep = QString());

    static EquipmentPackage loadPackageStatic(QSqlDatabase& db, qint64 packageId);
    static EquipmentItem loadGrinderItemStatic(QSqlDatabase& db, qint64 packageId);
    // The package's basket item, or an invalid item (id == 0) when none.
    static EquipmentItem loadBasketItemStatic(QSqlDatabase& db, qint64 packageId);
    // The package's puckprep item (canonical flag string in `model`), or an invalid
    // item (id == 0) when none.
    static EquipmentItem loadPuckPrepItemStatic(QSqlDatabase& db, qint64 packageId);
    static QVector<EquipmentPackageView> loadInventoryStatic(QSqlDatabase& db);

    static bool updatePackageFieldsStatic(QSqlDatabase& db, qint64 packageId, const QVariantMap& fields);
    // Update the package's grinder item identity; re-derives rpmCapable for the
    // new brand/model. No-op-safe when the package has no grinder item.
    static bool updateGrinderItemStatic(QSqlDatabase& db, qint64 packageId,
                                        const QString& brand, const QString& model,
                                        const QString& burrs);
    // Set the package's basket identity: update the existing basket item, insert
    // one when none exists, or delete it when brand+model are both empty (the
    // "no basket" state). No grinder analogue — a package's basket is optional.
    // Returns true on success OR when no change is needed (basket already in the
    // desired state); false ONLY on a genuine SQL failure, so a caller inside a
    // transaction can roll back on a real error without tripping on a benign no-op.
    static bool setBasketItemStatic(QSqlDatabase& db, qint64 packageId,
                                    const QString& brand, const QString& model);
    // Set the package's puck-prep identity from a canonical flag string
    // (PuckPrep::canonical): update the existing puckprep item, insert one when
    // none exists, or delete it when the string is empty ("no puck prep"). Same
    // return contract as setBasketItemStatic — false ONLY on a genuine SQL failure.
    static bool setPuckPrepItemStatic(QSqlDatabase& db, qint64 packageId,
                                      const QString& puckPrep);

    // Apply a grinder+basket+puckprep identity edit honoring copy-on-write
    // immutability + merge, and return the package id the caller should now treat
    // as active:
    //   - identity unchanged                      -> same id (no-op)
    //   - another in-inventory package matches     -> merge: repoint this
    //       package's bags to it, delete this package if unused else soft-delete
    //       it with superseded_by -> that id
    //   - package unused (no shots)                -> edit in place -> same id
    //   - package used (>=1 shot)                  -> fork a new package (copies
    //       name + last dial), repoint bags, soft-delete old (superseded_by) -> new id
    // Identity is the full (grinder brand/model/burrs + basket brand/model +
    // puckprep canonical flag string) tuple; "no basket" / "no puck prep" are
    // distinct values. Bag repointing is done here; the active-equipment selection
    // is the caller's to update from the returned id.
    static qint64 supersedeOrEditStatic(QSqlDatabase& db, qint64 packageId,
                                        const QString& brand, const QString& model,
                                        const QString& burrs,
                                        const QString& basketBrand, const QString& basketModel,
                                        const QString& puckPrep);
    // Grinder-only convenience: edit the grinder identity while PRESERVING the
    // package's current basket + puck prep. Thin wrapper over supersedeOrEditStatic
    // used by callers that don't touch them (MCP grinder edit, tests).
    static qint64 supersedeOrEditGrinderStatic(QSqlDatabase& db, qint64 packageId,
                                               const QString& brand, const QString& model,
                                               const QString& burrs);

    // Find an existing in-inventory package whose full identity matches
    // (case-insensitive grinder brand+model+burrs AND basket brand+model AND
    // puckprep canonical string, where empty params match a package lacking that
    // component), or 0 if none. Used by the migration and create/edit flows to
    // dedup on identity (NOT on grind/rpm).
    static qint64 findPackageByGrinderIdentityStatic(QSqlDatabase& db, const QString& brand,
                                                     const QString& model, const QString& burrs,
                                                     qint64 excludeId = 0,
                                                     const QString& basketBrand = QString(),
                                                     const QString& basketModel = QString(),
                                                     const QString& puckPrep = QString());

    // rpmCapable for a grinder identity: the registry's variableRpm when the
    // brand/model matches an alias, else true (a custom grinder shows the rpm
    // field). Shared by create/update/migration so the rule lives in one place.
    static bool deriveRpmCapable(const QString& brand, const QString& model);

    // Best-effort, marker-gated split of a combined grind+rpm string. A trailing
    // `(\d+)\s*rpm` token (case-insensitive) moves the number into outRpm and the
    // remainder (trimmed) into outGrind; anything without an explicit `rpm`
    // marker leaves outGrind == combined and outRpm == 0 (e.g. "1+4", "24 clicks").
    static void splitGrindAndRpm(const QString& combined, QString& outGrind, qint64& outRpm);

    // Migration 22 data step (shared with tests): create equipment packages from
    // the distinct grinder identities on coffee_bags + shots (deduped on
    // brand/model/burrs only — NOT grind/rpm), link each row via equipment_id,
    // and split each row's grinder_setting into grinder_setting + rpm. The
    // current* args are the user's live grinder settings (read from QSettings by
    // the caller) — they seed the default package so the active grinder always
    // has one even when no row carries that identity. Assumes the equipment
    // tables exist and coffee_bags/shots already have equipment_id + rpm columns.
    // Does NOT drop the legacy grinder identity columns (the schema owner does).
    static bool migrateFromGrinderColumnsStatic(QSqlDatabase& db,
                                                const QString& currentBrand,
                                                const QString& currentModel,
                                                const QString& currentBurrs,
                                                const QString& currentSetting);

    // Device-transfer import (add-equipment-packages task 2.8). Imports
    // equipment_packages + equipment_items from srcDb into destDb with new ids,
    // filling outIdMap[srcPackageId] = destPackageId so the caller can remap
    // coffee_bags.equipment_id and shots.equipment_id. superseded_by is remapped
    // through the same map after all packages are inserted. In merge mode an
    // in-inventory source package whose grinder identity already exists in dest
    // maps to that existing package WITHOUT copying a duplicate package/items;
    // superseded (historical) packages always import as new rows. A source with
    // no equipment tables yields an empty map and returns true. Runs inside the
    // caller's destDb transaction; assumes the equipment tables exist in dest.
    static bool importEquipmentStatic(QSqlDatabase& srcDb, QSqlDatabase& destDb, bool merge,
                                      QHash<qint64, qint64>& outIdMap);

signals:
    void inventoryReady(const QVariantList& packages);
    void packageReady(qint64 packageId, const QVariantMap& package); // empty if not found
    void packageCreated(qint64 packageId, const QVariantMap& package); // id -1 on failure
    void packageUpdated(qint64 packageId, bool success);
    void packageDeleted(qint64 packageId, bool success);
    void packagesChanged();

private:
    // Run `work(db)` on a background thread, then `done(dbOpened)` on the main
    // thread. Read callers must skip their "Ready" emission when dbOpened is
    // false (open failure → empty result that must not be read as not-found).
    void runAsync(const QString& connPrefix,
                  std::function<void(QSqlDatabase&)> work,
                  std::function<void(bool dbOpened)> done);

    QString m_dbPath;
    std::shared_ptr<std::atomic<bool>> m_destroyed = std::make_shared<std::atomic<bool>>(false);
    // Serializes all background DB work onto one FIFO worker thread so successive
    // writes to the same row apply in submission order (see SerialDbWorker).
    std::unique_ptr<SerialDbWorker> m_dbWorker;
};
