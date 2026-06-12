#include "coffeebagstorage.h"
#include "core/dbutils.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QSqlDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDate>
#include <QDateTime>
#include <QSettings>
#include <QThread>
#include <QDebug>

namespace {

// Column list shared by every SELECT — order must match bagFromQueryRow.
const char* kBagColumns =
    "id, roaster_name, coffee_name, roast_date, roast_level, beanbase_id, beanbase_json, "
    "frozen_date, defrost_date, notes, start_weight_g, in_inventory, "
    "grinder_brand, grinder_model, grinder_burrs, grinder_setting, dose_weight_g, yield_target_g, "
    "visualizer_bag_id, visualizer_roaster_id, last_used";

QVariant nullIfEmpty(const QString& s) {
    return s.isEmpty() ? QVariant() : QVariant(s);
}

QVariant nullIfZero(double v) {
    return v > 0 ? QVariant(v) : QVariant();
}

} // namespace

QVariantMap CoffeeBag::toVariantMap() const
{
    QVariantMap map;
    map["id"] = id;
    map["roasterName"] = roasterName;
    map["coffeeName"] = coffeeName;
    map["roastDate"] = roastDate;
    map["roastLevel"] = roastLevel;
    map["beanBaseId"] = beanBaseId;
    map["beanBaseData"] = beanBaseData;
    map["frozenDate"] = frozenDate;
    map["defrostDate"] = defrostDate;
    map["notes"] = notes;
    map["startWeightG"] = startWeightG;
    map["inInventory"] = inInventory;
    map["grinderBrand"] = grinderBrand;
    map["grinderModel"] = grinderModel;
    map["grinderBurrs"] = grinderBurrs;
    map["grinderSetting"] = grinderSetting;
    map["doseWeightG"] = doseWeightG;
    map["yieldTargetG"] = yieldTargetG;
    map["visualizerBagId"] = visualizerBagId;
    map["visualizerRoasterId"] = visualizerRoasterId;
    map["lastUsedEpoch"] = lastUsedEpoch;
    map["shotCount"] = shotCount;
    return map;
}

CoffeeBag CoffeeBag::fromVariantMap(const QVariantMap& map)
{
    CoffeeBag bag;
    bag.id = map.value("id", 0).toLongLong();
    bag.roasterName = map.value("roasterName").toString();
    bag.coffeeName = map.value("coffeeName").toString();
    bag.roastDate = map.value("roastDate").toString();
    bag.roastLevel = map.value("roastLevel").toString();
    bag.beanBaseId = map.value("beanBaseId").toString();
    bag.beanBaseData = map.value("beanBaseData").toString();
    bag.frozenDate = map.value("frozenDate").toString();
    bag.defrostDate = map.value("defrostDate").toString();
    bag.notes = map.value("notes").toString();
    bag.startWeightG = map.value("startWeightG", 0.0).toDouble();
    bag.inInventory = map.value("inInventory", true).toBool();
    bag.grinderBrand = map.value("grinderBrand").toString();
    bag.grinderModel = map.value("grinderModel").toString();
    bag.grinderBurrs = map.value("grinderBurrs").toString();
    bag.grinderSetting = map.value("grinderSetting").toString();
    bag.doseWeightG = map.value("doseWeightG", 0.0).toDouble();
    bag.yieldTargetG = map.value("yieldTargetG", 0.0).toDouble();
    bag.visualizerBagId = map.value("visualizerBagId").toString();
    bag.visualizerRoasterId = map.value("visualizerRoasterId").toString();
    bag.lastUsedEpoch = map.value("lastUsedEpoch", 0).toLongLong();
    bag.shotCount = map.value("shotCount", 0).toLongLong();
    return bag;
}

CoffeeBagStorage::CoffeeBagStorage(QObject* parent)
    : QObject(parent)
{
}

CoffeeBagStorage::~CoffeeBagStorage()
{
    *m_destroyed = true;
}

void CoffeeBagStorage::initialize(const QString& dbPath)
{
    m_dbPath = dbPath;
}

void CoffeeBagStorage::runAsync(const QString& connPrefix,
                                std::function<void(QSqlDatabase&)> work,
                                std::function<void()> done)
{
    if (m_dbPath.isEmpty()) {
        qWarning() << "CoffeeBagStorage: not initialized, dropping" << connPrefix;
        return;
    }
    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;
    QThread* thread = QThread::create([this, dbPath, connPrefix, work = std::move(work),
                                       done = std::move(done), destroyed]() {
        if (!withTempDb(dbPath, connPrefix, [&](QSqlDatabase& db) { work(db); }))
            qWarning() << "CoffeeBagStorage: failed to open DB for" << connPrefix;
        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [done = std::move(done), destroyed]() {
            if (*destroyed) return;
            done();
        }, Qt::QueuedConnection);
    });
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void CoffeeBagStorage::requestInventory()
{
    auto bags = std::make_shared<QVariantList>();
    runAsync("bags_inv",
        [bags](QSqlDatabase& db) {
            const QVector<CoffeeBag> inventory = loadInventoryStatic(db);
            for (const CoffeeBag& bag : inventory)
                bags->append(bag.toVariantMap());
        },
        [this, bags]() { emit inventoryReady(*bags); });
}

void CoffeeBagStorage::requestBag(qint64 bagId)
{
    auto result = std::make_shared<QVariantMap>();
    runAsync("bags_get",
        [bagId, result](QSqlDatabase& db) {
            const CoffeeBag bag = loadBagStatic(db, bagId);
            if (bag.isValid())
                *result = bag.toVariantMap();
        },
        [this, bagId, result]() { emit bagReady(bagId, *result); });
}

void CoffeeBagStorage::requestCreateBag(const QVariantMap& bagMap)
{
    auto newId = std::make_shared<qint64>(-1);
    auto created = std::make_shared<QVariantMap>();
    runAsync("bags_create",
        [bagMap, newId, created](QSqlDatabase& db) {
            CoffeeBag bag = CoffeeBag::fromVariantMap(bagMap);
            bag.lastUsedEpoch = QDateTime::currentSecsSinceEpoch();
            *newId = insertBagStatic(db, bag);
            if (*newId > 0)
                *created = loadBagStatic(db, *newId).toVariantMap();
        },
        [this, newId, created]() {
            emit bagCreated(*newId, *created);
            if (*newId > 0)
                emit bagsChanged();
        });
}

void CoffeeBagStorage::requestUpdateBag(qint64 bagId, const QVariantMap& fields)
{
    auto success = std::make_shared<bool>(false);
    runAsync("bags_update",
        [bagId, fields, success](QSqlDatabase& db) {
            *success = updateBagFieldsStatic(db, bagId, fields);
        },
        [this, bagId, success]() {
            emit bagUpdated(bagId, *success);
            if (*success)
                emit bagsChanged();
        });
}

void CoffeeBagStorage::requestMarkEmpty(qint64 bagId)
{
    requestUpdateBag(bagId, {{"inInventory", false}});
}

void CoffeeBagStorage::requestSetDefrostToday(qint64 bagId)
{
    requestUpdateBag(bagId, {{"defrostDate", QDate::currentDate().toString(Qt::ISODate)}});
}

void CoffeeBagStorage::requestTouchLastUsed(qint64 bagId)
{
    runAsync("bags_touch",
        [bagId](QSqlDatabase& db) {
            QSqlQuery query(db);
            query.prepare("UPDATE coffee_bags SET last_used = :now, "
                          "updated_at = strftime('%s', 'now') WHERE id = :id");
            query.bindValue(":now", QDateTime::currentSecsSinceEpoch());
            query.bindValue(":id", bagId);
            if (!query.exec())
                qWarning() << "CoffeeBagStorage: touch last_used failed:" << query.lastError().text();
        },
        []() {});
}

void CoffeeBagStorage::requestDeleteBag(qint64 bagId)
{
    auto success = std::make_shared<bool>(false);
    runAsync("bags_delete",
        [bagId, success](QSqlDatabase& db) {
            // Bags with linked shots are history — only Mark as Empty is
            // allowed for them. Delete is for mistaken creations.
            QSqlQuery countQuery(db);
            countQuery.prepare("SELECT COUNT(*) FROM shots WHERE bag_id = :id");
            countQuery.bindValue(":id", bagId);
            if (!countQuery.exec() || !countQuery.next()) {
                qWarning() << "CoffeeBagStorage: delete pre-check failed:" << countQuery.lastError().text();
                return;
            }
            if (countQuery.value(0).toInt() > 0) {
                qWarning() << "CoffeeBagStorage: refusing to delete bag" << bagId << "with linked shots";
                return;
            }
            QSqlQuery deleteQuery(db);
            deleteQuery.prepare("DELETE FROM coffee_bags WHERE id = :id");
            deleteQuery.bindValue(":id", bagId);
            *success = deleteQuery.exec();
            if (!*success)
                qWarning() << "CoffeeBagStorage: delete failed:" << deleteQuery.lastError().text();
        },
        [this, bagId, success]() {
            emit bagDeleted(bagId, *success);
            if (*success)
                emit bagsChanged();
        });
}

bool CoffeeBagStorage::ensureTableStatic(QSqlDatabase& db)
{
    QSqlQuery query(db);
    const bool ok = query.exec(R"(
        CREATE TABLE IF NOT EXISTS coffee_bags (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            roaster_name TEXT,
            coffee_name TEXT,
            roast_date TEXT,
            roast_level TEXT,
            beanbase_id TEXT,
            beanbase_json TEXT,
            frozen_date TEXT,
            defrost_date TEXT,
            notes TEXT,
            start_weight_g REAL,
            in_inventory INTEGER NOT NULL DEFAULT 1,
            grinder_brand TEXT,
            grinder_model TEXT,
            grinder_burrs TEXT,
            grinder_setting TEXT,
            dose_weight_g REAL,
            yield_target_g REAL,
            visualizer_bag_id TEXT,
            visualizer_roaster_id TEXT,
            last_used INTEGER,
            created_at INTEGER DEFAULT (strftime('%s', 'now')),
            updated_at INTEGER DEFAULT (strftime('%s', 'now'))
        )
    )");
    if (!ok) {
        qWarning() << "CoffeeBagStorage: failed to create coffee_bags table:" << query.lastError().text();
        return false;
    }
    query.exec("CREATE INDEX IF NOT EXISTS idx_coffee_bags_inventory ON coffee_bags(in_inventory, last_used DESC)");
    return true;
}

CoffeeBag CoffeeBagStorage::bagFromQueryRow(const QSqlQuery& query)
{
    CoffeeBag bag;
    bag.id = query.value(0).toLongLong();
    bag.roasterName = query.value(1).toString();
    bag.coffeeName = query.value(2).toString();
    bag.roastDate = query.value(3).toString();
    bag.roastLevel = query.value(4).toString();
    bag.beanBaseId = query.value(5).toString();
    bag.beanBaseData = query.value(6).toString();
    bag.frozenDate = query.value(7).toString();
    bag.defrostDate = query.value(8).toString();
    bag.notes = query.value(9).toString();
    bag.startWeightG = query.value(10).toDouble();
    bag.inInventory = query.value(11).toInt() != 0;
    bag.grinderBrand = query.value(12).toString();
    bag.grinderModel = query.value(13).toString();
    bag.grinderBurrs = query.value(14).toString();
    bag.grinderSetting = query.value(15).toString();
    bag.doseWeightG = query.value(16).toDouble();
    bag.yieldTargetG = query.value(17).toDouble();
    bag.visualizerBagId = query.value(18).toString();
    bag.visualizerRoasterId = query.value(19).toString();
    bag.lastUsedEpoch = query.value(20).toLongLong();
    return bag;
}

qint64 CoffeeBagStorage::insertBagStatic(QSqlDatabase& db, const CoffeeBag& bag)
{
    QSqlQuery query(db);
    query.prepare(R"(
        INSERT INTO coffee_bags (
            roaster_name, coffee_name, roast_date, roast_level, beanbase_id, beanbase_json,
            frozen_date, defrost_date, notes, start_weight_g, in_inventory,
            grinder_brand, grinder_model, grinder_burrs, grinder_setting,
            dose_weight_g, yield_target_g,
            visualizer_bag_id, visualizer_roaster_id, last_used
        ) VALUES (
            :roaster_name, :coffee_name, :roast_date, :roast_level, :beanbase_id, :beanbase_json,
            :frozen_date, :defrost_date, :notes, :start_weight_g, :in_inventory,
            :grinder_brand, :grinder_model, :grinder_burrs, :grinder_setting,
            :dose_weight_g, :yield_target_g,
            :visualizer_bag_id, :visualizer_roaster_id, :last_used
        )
    )");
    query.bindValue(":roaster_name", nullIfEmpty(bag.roasterName));
    query.bindValue(":coffee_name", nullIfEmpty(bag.coffeeName));
    query.bindValue(":roast_date", nullIfEmpty(bag.roastDate));
    query.bindValue(":roast_level", nullIfEmpty(bag.roastLevel));
    query.bindValue(":beanbase_id", nullIfEmpty(bag.beanBaseId));
    query.bindValue(":beanbase_json", nullIfEmpty(bag.beanBaseData));
    query.bindValue(":frozen_date", nullIfEmpty(bag.frozenDate));
    query.bindValue(":defrost_date", nullIfEmpty(bag.defrostDate));
    query.bindValue(":notes", nullIfEmpty(bag.notes));
    query.bindValue(":start_weight_g", nullIfZero(bag.startWeightG));
    query.bindValue(":in_inventory", bag.inInventory ? 1 : 0);
    query.bindValue(":grinder_brand", nullIfEmpty(bag.grinderBrand));
    query.bindValue(":grinder_model", nullIfEmpty(bag.grinderModel));
    query.bindValue(":grinder_burrs", nullIfEmpty(bag.grinderBurrs));
    query.bindValue(":grinder_setting", nullIfEmpty(bag.grinderSetting));
    query.bindValue(":dose_weight_g", nullIfZero(bag.doseWeightG));
    query.bindValue(":yield_target_g", nullIfZero(bag.yieldTargetG));
    query.bindValue(":visualizer_bag_id", nullIfEmpty(bag.visualizerBagId));
    query.bindValue(":visualizer_roaster_id", nullIfEmpty(bag.visualizerRoasterId));
    query.bindValue(":last_used", bag.lastUsedEpoch > 0 ? QVariant(bag.lastUsedEpoch) : QVariant());

    if (!query.exec()) {
        qWarning() << "CoffeeBagStorage: insert failed:" << query.lastError().text();
        return -1;
    }
    return query.lastInsertId().toLongLong();
}

CoffeeBag CoffeeBagStorage::loadBagStatic(QSqlDatabase& db, qint64 bagId)
{
    QSqlQuery query(db);
    query.prepare(QString("SELECT %1 FROM coffee_bags WHERE id = :id").arg(kBagColumns));
    query.bindValue(":id", bagId);
    if (!query.exec() || !query.next())
        return CoffeeBag();
    return bagFromQueryRow(query);
}

QVector<CoffeeBag> CoffeeBagStorage::loadInventoryStatic(QSqlDatabase& db)
{
    QVector<CoffeeBag> bags;
    QSqlQuery query(db);
    // shot_count subquery feeds the card's single delete-vs-finished action:
    // a bag nothing references is a mistaken creation (trash); one with
    // shots is history ("Bag finished").
    if (!query.exec(QString("SELECT %1, "
                            "(SELECT COUNT(*) FROM shots WHERE bag_id = coffee_bags.id) AS shot_count "
                            "FROM coffee_bags WHERE in_inventory = 1 "
                            "ORDER BY last_used DESC, id DESC").arg(kBagColumns))) {
        qWarning() << "CoffeeBagStorage: inventory query failed:" << query.lastError().text();
        return bags;
    }
    while (query.next()) {
        CoffeeBag bag = bagFromQueryRow(query);
        bag.shotCount = query.value(21).toLongLong();
        bags.append(bag);
    }
    return bags;
}

bool CoffeeBagStorage::updateBagFieldsStatic(QSqlDatabase& db, qint64 bagId, const QVariantMap& fields)
{
    // camelCase CoffeeBag key -> column. Only listed keys are updatable.
    static const QHash<QString, QString> kColumnFor = {
        {"roasterName", "roaster_name"},
        {"coffeeName", "coffee_name"},
        {"roastDate", "roast_date"},
        {"roastLevel", "roast_level"},
        {"beanBaseId", "beanbase_id"},
        {"beanBaseData", "beanbase_json"},
        {"frozenDate", "frozen_date"},
        {"defrostDate", "defrost_date"},
        {"notes", "notes"},
        {"startWeightG", "start_weight_g"},
        {"inInventory", "in_inventory"},
        {"grinderBrand", "grinder_brand"},
        {"grinderModel", "grinder_model"},
        {"grinderBurrs", "grinder_burrs"},
        {"grinderSetting", "grinder_setting"},
        {"doseWeightG", "dose_weight_g"},
        {"yieldTargetG", "yield_target_g"},
        {"visualizerBagId", "visualizer_bag_id"},
        {"visualizerRoasterId", "visualizer_roaster_id"},
        {"lastUsedEpoch", "last_used"},
    };

    QStringList assignments;
    QVariantList values;
    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it) {
        const QString column = kColumnFor.value(it.key());
        if (column.isEmpty()) {
            qWarning() << "CoffeeBagStorage: ignoring unknown field" << it.key();
            continue;
        }
        assignments << QString("%1 = ?").arg(column);
        QVariant value = it.value();
        if (it.key() == "inInventory")
            value = it.value().toBool() ? 1 : 0;
        else if (value.metaType().id() == QMetaType::QString && value.toString().isEmpty())
            value = QVariant();  // empty string clears to NULL
        values << value;
    }
    if (assignments.isEmpty())
        return false;

    QSqlQuery query(db);
    query.prepare(QString("UPDATE coffee_bags SET %1, updated_at = strftime('%s', 'now') WHERE id = ?")
                      .arg(assignments.join(", ")));
    int pos = 0;
    for (const QVariant& value : values)
        query.bindValue(pos++, value);
    query.bindValue(pos, bagId);

    if (!query.exec()) {
        qWarning() << "CoffeeBagStorage: update failed for bag" << bagId << ":" << query.lastError().text();
        return false;
    }
    return query.numRowsAffected() > 0;
}

CoffeeBag CoffeeBagStorage::bagFromLegacyPreset(const QJsonObject& preset)
{
    CoffeeBag bag;
    bag.roasterName = preset.value("brand").toString();
    bag.coffeeName = preset.value("type").toString();
    bag.roastDate = preset.value("roastDate").toString();
    bag.roastLevel = preset.value("roastLevel").toString();
    bag.beanBaseId = preset.value("beanBaseId").toString();
    bag.beanBaseData = preset.value("beanBaseData").toString();
    bag.grinderBrand = preset.value("grinderBrand").toString();
    bag.grinderModel = preset.value("grinderModel").toString();
    bag.grinderBurrs = preset.value("grinderBurrs").toString();
    bag.grinderSetting = preset.value("grinderSetting").toString();
    bag.inInventory = true;

    // The user-chosen preset label survives in notes when it carried more
    // information than "{brand} {type}". barista (per-shot, snapshot on every
    // shot already) and showOnIdle (superseded by inInventory) are dropped.
    const QString name = preset.value("name").toString().trimmed();
    const QString brandType = QString("%1 %2").arg(bag.roasterName, bag.coffeeName).trimmed();
    if (!name.isEmpty() && name.compare(brandType, Qt::CaseInsensitive) != 0
        && name.compare(bag.coffeeName, Qt::CaseInsensitive) != 0)
        bag.notes = name;

    return bag;
}

qint64 CoffeeBagStorage::findBagForShotStatic(QSqlDatabase& db, qint64 shotId,
                                              const QString& roasterName, const QString& coffeeName)
{
    if (shotId > 0) {
        QSqlQuery linkQuery(db);
        linkQuery.prepare("SELECT b.id FROM shots s JOIN coffee_bags b ON b.id = s.bag_id "
                          "WHERE s.id = :sid");
        linkQuery.bindValue(":sid", shotId);
        if (linkQuery.exec() && linkQuery.next())
            return linkQuery.value(0).toLongLong();
    }

    if (roasterName.isEmpty() && coffeeName.isEmpty())
        return -1;

    QSqlQuery identityQuery(db);
    identityQuery.prepare(
        "SELECT id FROM coffee_bags WHERE "
        "LOWER(COALESCE(roaster_name,'')) = LOWER(:roaster) AND "
        "LOWER(COALESCE(coffee_name,'')) = LOWER(:coffee) "
        "ORDER BY in_inventory DESC, last_used DESC, id DESC LIMIT 1");
    identityQuery.bindValue(":roaster", roasterName);
    identityQuery.bindValue(":coffee", coffeeName);
    if (identityQuery.exec() && identityQuery.next())
        return identityQuery.value(0).toLongLong();
    return -1;
}

qint64 CoffeeBagStorage::convertLegacyPresetSettings(const QString& dbPath)
{
    // Same QSettings scope the app's Settings object owns — a bare
    // QSettings() resolves to a different, empty store.
    QSettings appSettings(QStringLiteral("DecentEspresso"), QStringLiteral("DE1Qt"));
    const QByteArray presetsJson = appSettings.value(QStringLiteral("bean/presets")).toByteArray();
    if (presetsJson.isEmpty())
        return -1;

    const QJsonArray presets = QJsonDocument::fromJson(presetsJson).array();
    if (presets.isEmpty()) {
        // SettingsDye historically seeded an empty array on construction —
        // nothing to import, just retire the legacy keys.
        appSettings.remove(QStringLiteral("bean/presets"));
        appSettings.remove(QStringLiteral("bean/selectedPreset"));
        return -1;
    }

    const int selectedIndex = appSettings.value(QStringLiteral("bean/selectedPreset"), -1).toInt();
    qint64 selectedBagId = -1;
    bool committed = false;
    int imported = -1;

    withTempDb(dbPath, "bags_legacy", [&](QSqlDatabase& db) {
        // Pre-migration-19 database (e.g. main DB init not yet run): leave
        // the keys for a later retry.
        QSqlQuery probe(db);
        if (!probe.exec("SELECT COUNT(*) FROM coffee_bags"))
            return;

        if (!db.transaction()) {
            qWarning() << "CoffeeBagStorage: legacy preset transaction begin failed:"
                       << db.lastError().text();
            return;
        }
        imported = importLegacyPresetsStatic(db, presets, selectedIndex, &selectedBagId);
        if (imported < 0) {
            qWarning() << "CoffeeBagStorage: legacy preset import failed - rolling back";
            db.rollback();
            return;
        }
        if (!db.commit()) {
            qWarning() << "CoffeeBagStorage: legacy preset import commit failed:"
                       << db.lastError().text();
            db.rollback();
            return;
        }
        committed = true;
    });

    if (!committed)
        return -1;

    appSettings.remove(QStringLiteral("bean/presets"));
    appSettings.remove(QStringLiteral("bean/selectedPreset"));
    qDebug() << "CoffeeBagStorage: imported" << imported << "of" << presets.size()
             << "legacy bean presets as bags; selected bag id" << selectedBagId;
    return selectedBagId;
}

bool CoffeeBagStorage::importBagsStatic(QSqlDatabase& srcDb, QSqlDatabase& destDb, bool merge,
                                        QHash<qint64, qint64>& outIdMap)
{
    // Pre-migration-19 source: no coffee_bags table, nothing to import.
    {
        QSqlQuery srcCheck(srcDb);
        if (!srcCheck.exec("SELECT COUNT(*) FROM coffee_bags"))
            return true;
    }

    if (!merge) {
        QSqlQuery clearQuery(destDb);
        if (!clearQuery.exec("DELETE FROM coffee_bags")) {
            qWarning() << "CoffeeBagStorage: failed to clear bags for replace import:"
                       << clearQuery.lastError().text();
            return false;
        }
    }

    QSqlQuery srcBags(srcDb);
    if (!srcBags.exec(QString("SELECT %1 FROM coffee_bags").arg(kBagColumns))) {
        qWarning() << "CoffeeBagStorage: failed to query source bags:" << srcBags.lastError().text();
        return false;
    }

    int imported = 0, matched = 0;
    while (srcBags.next()) {
        const CoffeeBag bag = bagFromQueryRow(srcBags);

        qint64 destId = -1;
        if (merge) {
            QSqlQuery dupQuery(destDb);
            dupQuery.prepare(
                "SELECT id FROM coffee_bags WHERE "
                "LOWER(COALESCE(roaster_name,'')) = LOWER(:roaster) AND "
                "LOWER(COALESCE(coffee_name,'')) = LOWER(:coffee) AND "
                "COALESCE(roast_date,'') = :roast_date LIMIT 1");
            dupQuery.bindValue(":roaster", bag.roasterName);
            dupQuery.bindValue(":coffee", bag.coffeeName);
            dupQuery.bindValue(":roast_date", bag.roastDate);
            if (dupQuery.exec() && dupQuery.next()) {
                destId = dupQuery.value(0).toLongLong();
                matched++;
            }
        }

        if (destId < 0) {
            destId = insertBagStatic(destDb, bag);
            if (destId < 0)
                return false;
            imported++;
        }
        outIdMap.insert(bag.id, destId);
    }

    qDebug() << "CoffeeBagStorage: bag import -" << imported << "imported," << matched << "matched existing";
    return true;
}

int CoffeeBagStorage::importLegacyPresetsStatic(QSqlDatabase& db, const QJsonArray& presets,
                                                int selectedIndex, qint64* outSelectedBagId)
{
    int inserted = 0;
    for (qsizetype i = 0; i < presets.size(); i++) {
        const CoffeeBag bag = bagFromLegacyPreset(presets[i].toObject());
        if (bag.roasterName.isEmpty() && bag.coffeeName.isEmpty())
            continue;

        // Dedup on identity (case-insensitive roaster+name+roastDate).
        // Conservative on purpose: presets carry no lifecycle data, so a
        // skipped duplicate loses nothing.
        qint64 bagId = -1;
        {
            QSqlQuery dupQuery(db);
            dupQuery.prepare(
                "SELECT id FROM coffee_bags WHERE "
                "LOWER(COALESCE(roaster_name,'')) = LOWER(:roaster) AND "
                "LOWER(COALESCE(coffee_name,'')) = LOWER(:coffee) AND "
                "COALESCE(roast_date,'') = :roast_date LIMIT 1");
            dupQuery.bindValue(":roaster", bag.roasterName);
            dupQuery.bindValue(":coffee", bag.coffeeName);
            dupQuery.bindValue(":roast_date", bag.roastDate);
            if (dupQuery.exec() && dupQuery.next()) {
                bagId = dupQuery.value(0).toLongLong();
                qDebug() << "CoffeeBagStorage: preset import skipping duplicate"
                         << bag.roasterName << bag.coffeeName;
            }
        }

        if (bagId < 0) {
            bagId = insertBagStatic(db, bag);
            if (bagId < 0)
                return -1;  // caller rolls back
            inserted++;
        }

        if (outSelectedBagId && i == selectedIndex)
            *outSelectedBagId = bagId;
    }
    return inserted;
}
