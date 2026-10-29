#pragma once

#include "coffeebagstorage.h"
#include "recipestorage.h"
#include "core/dbutils.h"

#include <QSqlQuery>
#include <cmath>

inline bool restoreShotReviewWrites(QSqlDatabase& db, const QVariantMap& plan,
                                    const QVariantMap& originalBag, const QVariantMap& originalRecipe,
                                    const QVariantMap& writtenBag, const QVariantMap& writtenRecipe)
{
    auto transaction = DbWriteTxn::begin(db, "cancel shot review apply");
    if (!transaction.ok()) return false;
    const qint64 bagId = plan.value(QStringLiteral("bagId")).toLongLong();
    const qint64 recipeId = plan.value(QStringLiteral("recipeId")).toLongLong();
    const QVariantMap bagFields = plan.value(QStringLiteral("bagPatch")).toMap();
    const QVariantMap recipeFields = plan.value(QStringLiteral("recipePatch")).toMap();
    const QVariantMap currentBag = bagFields.isEmpty() ? QVariantMap() : CoffeeBagStorage::loadBagStatic(db, bagId).toVariantMap();
    const QVariantMap currentRecipe = recipeFields.isEmpty() ? QVariantMap() : RecipeStorage::loadRecipeStatic(db, recipeId).toVariantMap();
    QVariantMap bagUndo, recipeUndo;
    // Restore only our writes. A later edit to another field, or to one of
    // these fields, belongs to that editor and must survive cancellation.
    for (auto it = bagFields.cbegin(); it != bagFields.cend(); ++it)
        if (currentBag.value(it.key()) == writtenBag.value(it.key())) bagUndo.insert(it.key(), originalBag.value(it.key()));
    for (auto it = recipeFields.cbegin(); it != recipeFields.cend(); ++it)
        if (currentRecipe.value(it.key()) == writtenRecipe.value(it.key())) recipeUndo.insert(it.key(), originalRecipe.value(it.key()));
    if (!bagUndo.isEmpty() && !CoffeeBagStorage::updateBagFieldsStatic(db, bagId, bagUndo)) return false;
    if (!recipeUndo.isEmpty() && !RecipeStorage::updateRecipeFieldsStatic(db, recipeId, recipeUndo)) return false;
    return transaction.commit();
}

// Stage the database half of an approved proposal inside a transaction owned
// by the caller. Every failure requires that transaction to be rolled back;
// the caller must coordinate its commit with the live-state publication.
inline QString stageShotReviewWrites(QSqlDatabase& db, const DbWriteTxn& transaction, qint64 shotId,
                                     const QVariantMap& plan,
                                     const QVariantMap& approvedPatch,
                                     const QVariantMap& expectedBag,
                                     const QVariantMap& expectedRecipe)
{
    if (!transaction.ok() || shotId <= 0 || !plan.value(QStringLiteral("valid")).toBool())
        return QStringLiteral("invalid");
    QSqlQuery latest(db);
    if (!latest.exec(QStringLiteral("SELECT MAX(id) FROM shots")) || !latest.next())
        return QStringLiteral("unavailable");
    const qint64 latestId = latest.value(0).toLongLong();
    latest.finish();
    if (latestId != shotId) return QStringLiteral("staleShot");

    // The confirmation is for a saved correction, so verify the approved
    // fields against the row under the same write lock as the record updates.
    static const QMap<QString, QString> columns{
        {QStringLiteral("bagId"), QStringLiteral("bag_id")},
        {QStringLiteral("beanBrand"), QStringLiteral("bean_brand")},
        {QStringLiteral("beanType"), QStringLiteral("bean_type")},
        {QStringLiteral("roastDate"), QStringLiteral("roast_date")},
        {QStringLiteral("roastLevel"), QStringLiteral("roast_level")},
        {QStringLiteral("beanBaseId"), QStringLiteral("beanbase_id")},
        {QStringLiteral("beanBaseJson"), QStringLiteral("beanbase_json")},
        {QStringLiteral("doseWeight"), QStringLiteral("dose_weight")},
        {QStringLiteral("grinderSetting"), QStringLiteral("grinder_setting")},
        {QStringLiteral("rpm"), QStringLiteral("rpm")},
        {QStringLiteral("equipmentId"), QStringLiteral("equipment_id")},
        {QStringLiteral("barista"), QStringLiteral("barista")}};
    if (approvedPatch.isEmpty()) return QStringLiteral("invalid");
    QStringList selectedColumns;
    for (auto it = approvedPatch.cbegin(); it != approvedPatch.cend(); ++it) {
        if (!columns.contains(it.key())) return QStringLiteral("invalid");
        selectedColumns.append(columns.value(it.key()));
    }
    QSqlQuery shot(db);
    if (!shot.prepare(QStringLiteral("SELECT %1 FROM shots WHERE id = ?").arg(selectedColumns.join(QLatin1Char(',')))))
        return QStringLiteral("unavailable");
    shot.addBindValue(shotId);
    if (!shot.exec() || !shot.next()) return QStringLiteral("staleShot");
    int index = 0;
    for (auto it = approvedPatch.cbegin(); it != approvedPatch.cend(); ++it, ++index) {
        const QVariant stored = shot.value(index);
        const bool idField = it.key() == QLatin1String("bagId") || it.key() == QLatin1String("equipmentId");
        const bool numeric = idField || it.key() == QLatin1String("rpm") || it.key() == QLatin1String("doseWeight");
        bool approvedOk = false;
        const double actual = stored.toDouble(), approved = it.value().toDouble(&approvedOk);
        if ((numeric && (!approvedOk || !std::isfinite(actual) || !std::isfinite(approved)))
            || (numeric && !(idField && actual <= 0 && approved <= 0)
             && std::abs(actual - approved) >= 0.005)
            || (!numeric && stored.toString() != it.value().toString()))
            return QStringLiteral("staleShot");
    }
    shot.finish();

    const qint64 bagId = plan.value(QStringLiteral("bagId")).toLongLong();
    const qint64 recipeId = plan.value(QStringLiteral("recipeId")).toLongLong();
    if (approvedPatch.contains(QStringLiteral("bagId"))) {
        const qint64 approvedBagId = approvedPatch.value(QStringLiteral("bagId")).toLongLong();
        if ((bagId > 0 ? bagId : -1) != (approvedBagId > 0 ? approvedBagId : -1))
            return QStringLiteral("invalid");
    }
    const QVariantMap bagPatch = plan.value(QStringLiteral("bagPatch")).toMap();
    const QVariantMap recipePatch = plan.value(QStringLiteral("recipePatch")).toMap();
    const QMap<QString, QString> bagSource{{QStringLiteral("doseWeightG"), QStringLiteral("doseWeight")},
        {QStringLiteral("grinderSetting"), QStringLiteral("grinderSetting")},
        {QStringLiteral("rpm"), QStringLiteral("rpm")}, {QStringLiteral("equipmentId"), QStringLiteral("equipmentId")}};
    const QMap<QString, QString> recipeSource{{QStringLiteral("bagId"), QStringLiteral("bagId")},
        {QStringLiteral("doseG"), QStringLiteral("doseWeight")},
        {QStringLiteral("grindPinned"), QStringLiteral("grinderSetting")},
        {QStringLiteral("rpmPinned"), QStringLiteral("rpm")}, {QStringLiteral("equipmentId"), QStringLiteral("equipmentId")}};
    const auto followsApproval = [&approvedPatch](const QVariantMap& writes, const QMap<QString, QString>& sources) {
        for (auto it = writes.cbegin(); it != writes.cend(); ++it) {
            const QString source = sources.value(it.key());
            if (source.isEmpty() || !approvedPatch.contains(source) || approvedPatch.value(source) != it.value())
                return false;
        }
        return true;
    };
    if (!followsApproval(bagPatch, bagSource) || !followsApproval(recipePatch, recipeSource))
        return QStringLiteral("invalid");
    if ((!bagPatch.isEmpty() && bagId <= 0) || (!recipePatch.isEmpty() && recipeId <= 0))
        return QStringLiteral("invalid");
    if (bagId > 0) {
        const CoffeeBag bag = CoffeeBagStorage::loadBagStatic(db, bagId);
        if (!bag.isValid() || bag.toVariantMap() != expectedBag) return QStringLiteral("staleBag");
    }
    if (recipeId > 0) {
        const Recipe recipe = RecipeStorage::loadRecipeStatic(db, recipeId);
        const QVariantMap expected = Recipe::editableSnapshot(expectedRecipe);
        if (!recipe.isValid() || expected.isEmpty() || recipe.editableSnapshot() != expected)
            return QStringLiteral("staleRecipe");
    }
    if (!bagPatch.isEmpty() && !CoffeeBagStorage::updateBagFieldsStatic(db, bagId, bagPatch))
        return QStringLiteral("bagWriteFailed");
    if (!recipePatch.isEmpty() && !RecipeStorage::updateRecipeFieldsStatic(db, recipeId, recipePatch))
        return QStringLiteral("recipeWriteFailed");
    return {};
}

// Runs on the storage worker. Both callbacks consult the live owner; publication
// must recheck and apply in the same owner-thread callback.
template<typename StillCurrent, typename PublishIfCurrent>
QString commitShotReviewWrites(QSqlDatabase& db, qint64 shotId, const QVariantMap& plan,
                              const QVariantMap& patch, const QVariantMap& originalBag,
                              const QVariantMap& originalRecipe, StillCurrent stillCurrent,
                              PublishIfCurrent publishIfCurrent)
{
    const QString cancelled = QStringLiteral("The next shot changed. Its settings were kept.");
    const QString failed = QStringLiteral("The changes could not be saved. The next shot was kept.");
    QVariantMap bag, recipe;
    {
        auto transaction = DbWriteTxn::begin(db, "shot review apply");
        if (!transaction.ok()) return failed;
        if (!stageShotReviewWrites(db, transaction, shotId, plan, patch, originalBag, originalRecipe).isEmpty())
            return failed;
        if (!stillCurrent()) return cancelled;
        const qint64 bagId = plan.value(QStringLiteral("bagId")).toLongLong();
        const qint64 recipeId = plan.value(QStringLiteral("recipeId")).toLongLong();
        if (bagId > 0) bag = CoffeeBagStorage::loadBagStatic(db, bagId).toVariantMap();
        if (recipeId > 0) recipe = RecipeStorage::loadRecipeStatic(db, recipeId).toVariantMap();
        if (!transaction.commit()) return failed;
    }
    if (publishIfCurrent(bag, recipe)) return {};
    return restoreShotReviewWrites(db, plan, originalBag, originalRecipe, bag, recipe) ? cancelled
        : QStringLiteral("The next shot changed while saving. Please review the recipe and bag changes.");
}
