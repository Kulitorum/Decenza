#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <atomic>
#include <memory>

#include "history/bagid.h"

class QSqlDatabase;
class BeanBaseClient;
class CoffeeBagStorage;

// Backing model for the Change Beans dialog (bean-bag-inventory): one ranked
// list merging three lanes — inventory bags, Visualizer canonical
// autocomplete, and distinct coffees from the local shot history.
//
// Tiers (quality ranking, lower = better):
//   0  In inventory — selecting applies immediately, no details form
//   1  History + canonical (merged: grinder/dose from history, attrs from
//      Bean Base)
//   2  Canonical only
//   3  History with a canonical link
//   4  History free text only
//   (5 Manual entry — a static QML row, not a model entry)
//
// Merge rules: a history/canonical result matching an inventory bag (on
// beanBaseId or case-insensitive roaster+name) is absorbed into the bag's
// Tier 0 row — the dialog never offers to re-create a coffee already in
// inventory. Within history, the same coffee linked and unlinked (shots
// before/after canonical linking) collapses to one entry.
//
// Lanes arrive asynchronously (BeanBaseClient's debounced canonical search,
// a background-thread history query); the merged list is recomputed on each
// arrival. History queries coalesce event-based: a query that lands while
// one is in flight is parked and re-run on completion (no timer guard).
class UnifiedBeanSearchModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    // Search-result tier — the model's core ranking invariant (lower = better).
    // Emitted as the integer TierRole value; the QML consumers in
    // ChangeBeansDialog.qml compare against these exact numbers, so the
    // underlying values must not change.
    enum class Tier {
        Inventory       = 0,  // In inventory — selecting applies immediately, no details form
        HistoryCanonical = 1, // History + canonical, merged (grinder/dose from history, attrs from Bean Base)
        CanonicalOnly   = 2,  // Bean Base autocomplete only
        HistoryLinked   = 3,  // History with a canonical link, unabsorbed
        HistoryFreeText = 4,  // History free text only
    };
    Q_ENUM(Tier)

    // Cross-role invariant: a real (set) bag id is exclusive to Inventory-tier
    // rows (every other tier carries bagId == -1). Expressed in enum terms so
    // mergeLanes and the tests can assert it directly. Shares the bag-id
    // threshold with bagIdIsSet() so the two can't drift.
    static constexpr bool bagIdInvariantHolds(Tier tier, qint64 bagId) {
        return !bagIdIsSet(bagId) || tier == Tier::Inventory;
    }

    enum Roles {
        TierRole = Qt::UserRole + 1,
        SourcesRole,       // "inventory" | "beanbase" | "history" | "beanbase+history"
        RoasterNameRole,
        CoffeeNameRole,
        BagIdRole,         // > 0 only for Tier::Inventory (mirrors the bag's "id"); -1 for every other tier
        BeanBaseIdRole,
        RoastDateRole,
        FrozenDateRole,
        DefrostDateRole,
        LastUsedEpochRole,
    };
    Q_ENUM(Roles)

    explicit UnifiedBeanSearchModel(QObject* parent = nullptr);
    ~UnifiedBeanSearchModel() override;

    // Non-owning; wired by MainController after construction.
    void setSources(CoffeeBagStorage* bagStorage, BeanBaseClient* beanBase, const QString& dbPath);

    QString query() const { return m_query; }
    void setQuery(const QString& query);
    bool searching() const { return m_searching; }

    // Refresh inventory + recent-history suggestions (dialog open).
    Q_INVOKABLE void refresh();

    // Full result row for the bag-details form prefill (all CoffeeBag-shaped
    // keys plus tier/sources).
    Q_INVOKABLE QVariantMap get(int row) const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Distinct coffees from the shot history, newest first, capped at
    // `limit`. Grouped on COALESCE(beanbase_id, brand|type) with a C++
    // post-merge of linked+unlinked duplicates. Static for unit tests.
    static QVariantList queryHistoryStatic(QSqlDatabase& db, const QString& filter, int limit = 50);

    // Pure merge of the three lanes into the ranked result list. Static for
    // unit tests; each entry is a QVariantMap with the keys exposed by get().
    static QVariantList mergeLanes(const QVariantList& inventoryBags,
                                   const QVariantList& canonicalResults,
                                   const QVariantList& historyEntries,
                                   const QString& query);

signals:
    void queryChanged();
    void searchingChanged();
    void countChanged();

private:
    void requestHistory();
    void rebuild();
    void setSearching(bool searching);

    CoffeeBagStorage* m_bagStorage = nullptr;  // Non-owning
    BeanBaseClient* m_beanBase = nullptr;      // Non-owning
    QString m_dbPath;

    QString m_query;
    QVariantList m_inventory;        // toVariantMap() rows, all inInventory bags
    QVariantList m_canonical;        // BeanBaseClient searchResults entries
    QVariantList m_history;          // queryHistoryStatic rows
    QVariantList m_results;          // merged, what the view shows

    bool m_searching = false;
    bool m_historyInFlight = false;
    bool m_historyPending = false;   // query changed while a history query ran

    std::shared_ptr<std::atomic<bool>> m_destroyed = std::make_shared<std::atomic<bool>>(false);

#ifdef DECENZA_TESTING
    friend class tst_CoffeeBags;  // populates m_inventory + rebuild() to exercise data() roles
#endif
};
