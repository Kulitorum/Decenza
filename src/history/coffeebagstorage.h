#pragma once

#include <QObject>
#include <QDate>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <atomic>
#include <functional>
#include <memory>

#include "network/visualizersync.h"

#include <QtQmlIntegration/qqmlintegration.h>
class QSqlDatabase;
class QJsonArray;
class QJsonObject;
class SerialDbWorker;

// Forward-declared rather than including beanbase_blob.h: this header reaches
// maincontroller.h and from there most of the tree, and the two types are only
// used here by reference/pointer.
namespace BeanBaseBlob {
struct BagIdentity;
struct CanonicalLink;
}

// A coffee bag: the single bean concept that replaced bean presets
// (openspec change bean-bag-inventory). A bag IS the active bean state —
// shots snapshot its fields at save time, edits write through to it, and
// "in inventory" doubles as idle-page visibility (no showOnIdle flag).
// All string dates are ISO yyyy-MM-dd; empty string = unset (stored NULL).
// Structured tea brewing data carried in a tea bag's beanBaseData blob
// (add-recipe-wizard-tea). Schemaless JSON keys written by the tea extraction
// prompt and the tea bag form; absent keys mean "vendor did not state it" —
// consumers use defaults, never inferred values. brewTempC is always Celsius
// (the extraction normalizes °F and "boiling"); leafGramsPer100Ml is the leaf
// ratio normalized from per-cup wordings. steepTime stays a display string
// (no machine mapping).
struct TeaBrewingData {
    QString teaType;             // black/green/oolong/white/herbal/pu-erh; empty = unstated
    double brewTempC = 0;        // 0 = unstated
    double leafGramsPer100Ml = 0;// 0 = unstated
    QString steepTime;           // e.g. "3-5 minutes"; empty = unstated
};

struct CoffeeBag {
    qint64 id = 0;

    // Identity
    QString roasterName;
    QString coffeeName;
    QString roastDate;       // empty = unknown roast date (allowed)
    QString roastLevel;
    QString beanBaseId;      // canonical UUID, empty = unlinked
    QString beanBaseData;    // compact-JSON canonical snapshot, empty = none

    // Bag kind (add-recipe-wizard-tea): "coffee" | "tea". Stamped by the
    // creation entry point (Add Coffee / Add Tea) and never edited after —
    // a mis-created zero-shot bag is deleted and recreated. Tea bags skip
    // roast level, grind, Bean Base linking, and the Visualizer canonical
    // search lane; tea-specific descriptive/brewing fields (teaType,
    // brewTempC, leafGramsPer100Ml, …) live in the beanBaseData blob.
    QString kind = QStringLiteral("coffee");

    // Lifecycle, on two INDEPENDENT axes — nothing here gates or clears
    // anything else here.
    //   Freezer: frozenDate says the BAG is stored frozen; defrostDate says
    //     when the CURRENT PORTION left the freezer. Beans are frozen in
    //     portions and pulled out one at a time, so a frozen bag keeps
    //     portions in the freezer indefinitely — frozenDate staying set after
    //     a thaw is correct, and thawing is a recurring event.
    //   Container: storageHint is the PLAN for how beans are kept when NOT in
    //     the freezer (counter / airtight / vacuum-sealed / fridge). It is
    //     forward-looking on a frozen bag and valid in every freeze state.
    //     There is no "frozen" value — frozenDate alone decides frozen-ness,
    //     so the two answer different questions and cannot disagree.
    //   Use: openedDate is when the current portion left airtight storage —
    //     the sibling of defrostDate, not its non-frozen substitute.
    // All describe the CURRENT portion only; full history lives in per-shot
    // snapshots. All are local-only (never synced to Visualizer).
    QString frozenDate;
    QString defrostDate;
    QString storageHint;
    QString openedDate;
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
    // Bean-scoped dial memory, split on the measurement/intent line
    // (add-yield-ratio-anchor): grinderSetting/rpm/doseWeightG are dial-in —
    // things the user physically did — and keep their unconditional
    // write-through from edits plus the dose stamp on shot save. The yield
    // spec below is design intent and is button-protected: it changes ONLY
    // via the explicit "Update Bag" action in Brew Settings — never from a
    // shot save, Brew Settings OK, dose capture, or bag selection.
    QString grinderSetting;
    double doseWeightG = 0;  // 0 = unset
    // The bean's OWN yield spec ("none" | "absolute" | "ratio", see
    // src/core/yieldspec.h) — a first-class anchor, not a deviation from the
    // active profile's target. Local-only (never synced to Visualizer).
    // Replaces the legacy yield_override_g, which migration 34 converts and
    // leaves dead in place.
    double yieldValue = 0;   // 0 = unset (grams when absolute, multiplier when ratio)
    QString yieldMode = QStringLiteral("none");

    // Equipment (add-equipment-packages). equipmentId points at the bag's
    // grinder package; rpm is the grinder rpm dial-in (sibling of grinderSetting).
    qint64 equipmentId = 0;  // FK -> equipment_packages.id; 0 = unset
    qint64 rpm = 0;          // 0 = unset

    // Visualizer Coffee Management sync state
    QString visualizerBagId;
    QString visualizerRoasterId;
    // Set when a bag edit's Visualizer push must be deferred (park-first
    // before each attempt; stays set on retryable failure or while CM is
    // still Unknown); the upload read-back's retry drains it. Carried by
    // backup import like any column — a restored pending flag just retries
    // harmlessly. Not a Visualizer-synced field itself (visualizer=false in
    // kCols, so writing it never triggers a push).
    bool visualizerSyncPending = false;
    // JSON object of each attribute's value as Visualizer was last known to hold
    // it, keyed by API name, "archived_at" included (VisualizerSync bag rules).
    QString visualizerSeen;

    qint64 lastUsedEpoch = 0; // bumped on selection and shot save (MRU ordering)

    bool isValid() const { return id > 0; }
    QVariantMap toVariantMap() const;
    static CoffeeBag fromVariantMap(const QVariantMap& map);
    // What a shot pulled from this bag records, as ShotHistoryStorage metadata keys:
    // the bean fields, the Bean Base snapshot and the bag link. One definition for
    // the app's Change Beans and the web page's bean picker.
    QVariantMap shotSnapshot() const;

    bool isTea() const { return kind == QLatin1String("tea"); }

    // Parse the tea brewing keys out of a beanBaseData blob string. Tolerant:
    // empty/invalid JSON or absent keys land on the struct defaults.
    static TeaBrewingData teaBrewingFromBlob(const QString& beanBaseData);

    // Out-of-freezer storage choices, unset first, as {value, labelKey, label}:
    // the one list the app and web editors both render. "frozen" is deliberately
    // NOT a value — frozen state is defined solely by `frozenDate`. Inline so the
    // translation-key registry can list the labels without linking bag storage.
    struct StorageHintOption { const char* value; const char* labelKey; const char* label; };
    static constexpr StorageHintOption kStorageHintOptions[] = {
        {"", "changebeans.form.storageHint.unset", "Not specified"},
        {"counter", "changebeans.form.storageHint.counter", "Counter"},
        {"airtight", "changebeans.form.storageHint.airtight", "Airtight container"},
        {"vacuum-sealed", "changebeans.form.storageHint.vacuum", "Vacuum-sealed"},
        {"fridge", "changebeans.form.storageHint.fridge", "Fridge"},
    };
    static QVariantList storageHintOptions()
    {
        QVariantList options;
        for (const auto& o : kStorageHintOptions)
            options << QVariantMap{{QStringLiteral("value"), QString::fromLatin1(o.value)},
                                   {QStringLiteral("labelKey"), QString::fromLatin1(o.labelKey)},
                                   {QStringLiteral("label"), QString::fromLatin1(o.label)}};
        return options;
    }
    // The non-empty values of storageHintOptions().
    static const QStringList& storageHintValues();
    // Accepts "" (unset) or any canonical value; rejects "frozen" and junk.
    // The write boundary uses this so an off-list value from an MCP client
    // can't reach the DB (and thence the AI freshness block) unvalidated.
    static bool isValidStorageHint(const QString& hint);

    // The opened date a shot pulled on `today` should stamp, or "" to leave it.
    // A shot proves the current portion is open: stamp when nothing is
    // recorded, or when the recorded date predates the latest thaw (it belongs
    // to the previous portion). A shot from a frozen bag with no thaw recorded
    // is one serving taken out with the rest put back, so it never stamps. Nor
    // does a bag whose roast date is after `today` (a legacy entry): the stamp
    // would be out of order and refuse the dose written with it.
    // Dates are ISO yyyy-MM-dd.
    static QString openedDateForShot(const QString& roastDate, const QString& frozenDate,
                                     const QString& defrostDate, const QString& openedDate,
                                     const QDate& today);

    // Checks the lifecycle fields a write carries: each storage date must be
    // yyyy-MM-dd, no date may be after `today` (a roast or thaw that hasn't happened yet is a typo
    // the AI would read as fact), and storageHint must be on the list. Returns a
    // message naming the first bad field, or "" when all are fine.
    static QString lifecycleFieldError(const QVariantMap& fields, const QDate& today);

    // Days from an ISO date to `today`; -1 when unparsable or in the future.
    static int daysSince(const QString& iso, const QDate& today);
    // A bag's lifecycle line as data, in display order: {kind, date, ageDays}
    // with kind roasted / frozen / thawed / opened. Each surface only words it.
    static QVariantList lifecycleParts(const QVariantMap& bag, const QDate& today);

    // Fields that apply to only one kind. The tea-only ones live in the blob.
    static const QStringList& teaOnlyKeys();
    static const QStringList& coffeeOnlyKeys();

    // A new bag of the same coffee: `bag` minus its id and what belonged to that
    // bag (its dates, notes, start weight and inventory state). The storage plan
    // carries over, and a bag that was frozen starts frozen on `today`.
    static QVariantMap restockTemplate(QVariantMap bag, const QDate& today);

    // The lifecycle dates in `changes` out of order against `stored` overlaid with
    // them (frozen before roast, thaw before freeze, opened before roast), as a
    // message; "" when in order. Only pairs that `changes` touches are checked,
    // so an unrelated edit never trips over an old record.
    static QString lifecycleOrderError(const QVariantMap& stored, const QVariantMap& changes);
    // A non-empty coffee-only field on a tea bag (`changes` overlaid on `stored`
    // decides the kind); clearing one is fine. "" when none.
    static QString kindFieldError(const QVariantMap& stored, const QVariantMap& changes);
    // Every check a bag write must pass: the kind's fields, each changed lifecycle
    // field, then the order against `stored`.
    static QString writeError(const QVariantMap& stored, const QVariantMap& changes, const QDate& today);

    // What an edit changed: the keys of `current` that differ from `opened`. The
    // detail blob goes key by key as beanBaseDataPatch (a removed key as null)
    // unless `replaceBlob`, so an edit never overwrites a Visualizer pull it
    // didn't see. A blob that isn't a JSON object goes whole.
    static QVariantMap editChanges(const QVariantMap& opened, const QVariantMap& current,
                                   bool replaceBlob);
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

    // The actions a card offers, as ids both surfaces render: restock, restore,
    // findInBeanBase, bagFinished, delete, edit, info, freeze, thaw.
    QStringList cardActions(bool finished) const;
    // The bag map plus what the inventory view derives: shotCount, actions and
    // lifecycleParts.
    QVariantMap toVariantMap(bool finished, const QDate& today) const;
};

// SQLite-backed bag storage in the shot history database (coffee_bags table,
// created by ShotHistoryStorage migration 19). All public request* methods are
// async: DB work runs on a QThread::create() background thread and results are
// delivered back via signals, following the ShotHistoryStorage pattern. The
// *Static helpers are synchronous, take a caller-provided connection, and are
// shared with the migration, SettingsSerializer import, and unit tests.
class CoffeeBagStorage : public QObject {
    Q_OBJECT

    // Compile-time QML registration, so qmllint, qmlcachegen and the language server can
    // follow MainController's property through to this class. A runtime qmlRegister* call is
    // invisible to all three. Full rationale in src/controllers/maincontroller.h.
    QML_ELEMENT
    QML_UNCREATABLE("CoffeeBagStorage is created in C++ and reached via MainController")

public:
    explicit CoffeeBagStorage(QObject* parent = nullptr);
    ~CoffeeBagStorage();

    // dbPath must be the shot history database (table lives there).
    void initialize(const QString& dbPath);
    QString databasePath() const { return m_dbPath; }

    // True when no background CRUD work is queued, running, or waiting to
    // deliver its result — see ShotHistoryStorage::isDbWorkIdle() for the full
    // rationale (same shape, same worker type). A test that destroys this
    // object without waiting on this first was the exact path
    // ~SerialDbWorker's discard warning was added to surface: work vanishes
    // with nothing shown for it.
    bool isDbWorkIdle() const;

    // Async queries — results via signals (QVariantList of toVariantMap()).
    Q_INVOKABLE void requestInventory();                   // inInventory = true, MRU order
    Q_INVOKABLE void requestFinishedBags();                // inInventory = false, MRU order
    Q_INVOKABLE void requestFinishedBagCount();            // finishedBagCountReady()
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
    // Applies a Visualizer pull: `decide` runs on the bag worker with the row as
    // it stands and returns what to write (VisualizerSync::bag*PullChanges); the
    // fields and the seen values commit together. A change emits bagsChanged,
    // bagPulledFromVisualizer and the inventory lifecycle signals — never
    // bagUpdated, whose listeners take it as the result of their own write, nor
    // bagVisualizerFieldsChanged, since pushing the values back would only echo.
    void requestApplyVisualizerPull(qint64 bagId, std::function<VisualizerSync::BagPull(const QVariantMap&)> decide);
    // Records remote values in coffee_bags.visualizer_seen, keyed by API name.
    static bool mergeVisualizerSeenStatic(QSqlDatabase& db, qint64 bagId, const QVariantMap& seen);
    // Stamp "the AI product-page search already ran for this bag" into the
    // stored blob (add-beanbase-archive-link-fallback). Its own key, not
    // linkDead: a bag whose URL died is precisely the one the search must
    // still be allowed to run for. Read-modify-write of the STORED blob, so a
    // caller holding unsaved form edits cannot persist them through this.
    Q_INVOKABLE void requestMarkAiPageSearched(qint64 bagId);
    Q_INVOKABLE void requestTouchLastUsed(qint64 bagId);                // bump MRU timestamp (no bagUpdated)
    // Deletes only when no shot references the bag (shots.bag_id count = 0);
    // emits bagDeleted(bagId, success) — success false when shots exist.
    Q_INVOKABLE void requestDeleteBag(qint64 bagId);

    // The newest other in-inventory bag of the same coffee (canonical id, else
    // roaster + coffee), or -1. Where a finished bag's recipes roll to.
    static qint64 successorBagStatic(QSqlDatabase& db, qint64 finishedBagId);

    // QML bridges to the CoffeeBag rules the web page uses too.
    Q_INVOKABLE QVariantList storageHintOptions() const { return CoffeeBag::storageHintOptions(); }
    // Ages are measured to `referenceIso` (a shot's date), today when empty.
    Q_INVOKABLE QVariantList lifecycleParts(const QVariantMap& bag, const QString& referenceIso = QString()) const
    {
        const QDate reference = QDate::fromString(referenceIso, QStringLiteral("yyyy-MM-dd"));
        return CoffeeBag::lifecycleParts(bag, reference.isValid() ? reference : QDate::currentDate());
    }
    Q_INVOKABLE QVariantMap restockTemplate(const QVariantMap& bag) const
    { return CoffeeBag::restockTemplate(bag, QDate::currentDate()); }
    Q_INVOKABLE QVariantMap editChanges(const QVariantMap& opened, const QVariantMap& current,
                                        bool replaceBlob) const
    { return CoffeeBag::editChanges(opened, current, replaceBlob); }

    // --- Synchronous static helpers (caller provides the connection) ---

    // Create the coffee_bags table if missing. Used by migration 19 and tests.
    static bool ensureTableStatic(QSqlDatabase& db);

    static qint64 insertBagStatic(QSqlDatabase& db, const CoffeeBag& bag);
    // An invalid bag means not found, unless `readError` is set: a failed query
    // is not a missing row. A failure is logged here.
    static CoffeeBag loadBagStatic(QSqlDatabase& db, qint64 bagId, QString* readError = nullptr);
    // The open bags, or the finished ones. A failed query returns no bags and
    // sets `readError`, so a caller can tell it from an empty shelf.
    static QVector<InventoryBag> loadInventoryStatic(QSqlDatabase& db, bool finished = false,
                                                     QString* readError = nullptr);
    // Update only the columns named in `fields` (camelCase CoffeeBag keys).
    // `refusal`, when given, receives why CoffeeBag::writeError refused the
    // write, "" for any other failure.
    static bool updateBagFieldsStatic(QSqlDatabase& db, qint64 bagId, const QVariantMap& fields,
                                      QString* refusal = nullptr);

    // Enforce the canonical-link invariant on a bag about to be written: a bag
    // may carry a canonical id only while its own roaster/coffee still name the
    // record that id points at. A canonical_coffee_bags row IS a roaster's
    // product, and visualizer.coffee treats the id as authoritative for identity
    // (Shot#refresh_coffee_bag_fields — its `elsif canonical_coffee_bag` branch,
    // reached when the shot has no server-side coffee_bag), so a bag that
    // borrowed another roaster's record republished its shots under that roaster. Editing the
    // identity is the supported way to fix a near-match pick, so the link — not
    // the user's name — is what gives way: the id and the blob's link keys are
    // dropped, every descriptive field kept. Returns true when it dropped one.
    //
    // Applied at the STORAGE write so the bag editor, MCP bag_update and the web
    // /beans editor all inherit it; each of the three can otherwise produce the
    // mismatch in a single save.
    static bool dropConflictedCanonicalLink(const BeanBaseBlob::BagIdentity& identity,
                                            BeanBaseBlob::CanonicalLink* link);

    // Queue this bag's UPLOADED shots for the Visualizer bean repair
    // (shots.bean_repair_pending). Called wherever a borrowed canonical link is
    // dropped, because those are exactly the shots visualizer.coffee may have
    // renamed. Returns the number of shots queued, -1 on failure.
    static int markShotsForBeanRepairStatic(QSqlDatabase& db, qint64 bagId);

    // Clean every stored bag that already carries a conflicted link (migration
    // 38's data pass), queueing their shots for repair. Returns the number of
    // bags unlinked, -1 on failure.
    //
    // `queueShots` false skips only the queueing, and exists for one caller: the
    // migration whose ALTER for shots.bean_repair_pending failed. There the
    // UPDATE would fail on the missing column and fail the whole unlink with it,
    // which is the opposite of the migration's stated policy that the unlink
    // lands regardless.
    static int cleanConflictedCanonicalLinksStatic(QSqlDatabase& db, bool queueShots = true);

    // True when `fields` (camelCase CoffeeBag keys) contains at least one field
    // that Visualizer stores on its coffee bag — the identity/lifecycle/canonical
    // fields, NOT the local-only grinder/dose/yield/lifecycle-id columns. Drives
    // the bagVisualizerFieldsChanged signal so a Visualizer PATCH fires only for
    // edits the remote bean record actually reflects (a grinder write-through or
    // dose/yield stamp must not hit the network). Single source of truth for the
    // local-key → Visualizer-field mapping.
    static bool touchesVisualizerFields(const QVariantMap& fields);

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
    // The read did not happen: the database would not open, or storage was
    // never initialized. Distinct from an empty inventoryReady, because a view
    // that waits for "loaded" before deciding what to render would otherwise
    // wait forever and show neither bags nor an empty state.
    void inventoryFailed();
    void finishedBagsReady(const QVariantList& bags);
    void finishedBagsFailed();
    void finishedBagCountReady(int count);
    void bagReady(qint64 bagId, const QVariantMap& bag);   // bag empty if not found
    // requestBag could not read the database. Not "not found": a listener that
    // acts on a missing bag (SettingsDye clears the selection) must not act on this.
    void bagReadFailed(qint64 bagId);
    void bagCreated(qint64 bagId, const QVariantMap& bag); // bagId -1 on failure
    // `refusal`: why CoffeeBag::writeError refused the write ("" otherwise).
    void bagUpdated(qint64 bagId, bool success, const QString& refusal);
    // A write failed in a way the user must know about. bagUpdated carries the
    // same status, but it is a terminal signal for programmatic callers (the
    // MCP tool arms a one-shot to send its response) — the UI never consumed
    // it, so a failed save closed the dialog silently and the user read the
    // unchanged card as their own mistake. Mirrors
    // ShotHistoryStorage::errorOccurred; main.qml surfaces both as a toast.
    void errorOccurred(const QString& message);
    // The bag left inventory (requestMarkEmpty, or any update carrying
    // inInventory=false — card, MCP, web all funnel through requestUpdateBag).
    // The recipe roll-on-finish relink hooks onto this event
    // (recipe-bag-lifecycle) — event-driven, never polled.
    void bagFinished(qint64 bagId);
    // The bag returned to inventory (an update carrying inInventory=true).
    // Wake-on-restock hooks onto this alongside bagCreated, so a bag
    // un-finished via MCP/web wakes stale sibling recipes exactly like a
    // newly added bag (the relink is idempotent and dup-guarded).
    void bagRestocked(qint64 bagId);
    void bagDeleted(qint64 bagId, bool success);
    // Emitted (after a successful update) only when the edit touched a field
    // Visualizer stores on the bean — see touchesVisualizerFields(). The
    // MainController gates on visualizerActive + upload autoUpdate + CM-active before PATCHing.
    void bagVisualizerFieldsChanged(qint64 bagId);
    // A Visualizer pull changed this bag's fields.
    void bagPulledFromVisualizer(qint64 bagId);
    // Coarse "something changed" signal so views can re-request the inventory.
    void bagsChanged();

private:
    void requestShelf(bool finished);  // requestInventory / requestFinishedBags
    void updateBag(qint64 bagId, const QVariantMap& fields, bool propagateBeanBase);
    void finishBagUpdate(qint64 bagId, const QVariantMap& fields, bool success, const QString& refusal);
    void emitInventoryLifecycle(qint64 bagId, const QVariantMap& fields);
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
