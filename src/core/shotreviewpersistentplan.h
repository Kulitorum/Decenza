#pragma once

#include "shotreviewbaseline.h"
#include "core/drinktypes.h"
#include <QVariantList>
#include <QVariantMap>

// Persistent changes are derived only from approved shot corrections. The
// caller reads the rows before preview and checks them again at commit.
inline QVariantMap shotReviewPersistentPlan(const ShotReviewSetup& setup,
                                            const QVariantMap& patch,
                                            const QVariantMap& recipe,
                                            const QVariantMap& bag,
                                            const QVariantMap& equipment = {})
{
    const qint64 bagId = patch.contains(QStringLiteral("bagId"))
        ? patch.value(QStringLiteral("bagId")).toLongLong() : setup.brew.activeBagId;
    const qint64 recipeId = setup.brew.activeRecipeId;
    const qint64 equipmentId = (patch.contains(QStringLiteral("grinderSetting")) || patch.contains(QStringLiteral("rpm")))
        ? patch.value(QStringLiteral("equipmentId"), setup.brew.activeEquipmentId).toLongLong() : -1;
    if ((bagId > 0 && bag.value(QStringLiteral("id")).toLongLong() != bagId)
        || (equipmentId > 0 && equipment.value(QStringLiteral("id")).toLongLong() != equipmentId)
        || (recipeId > 0 && recipe.value(QStringLiteral("id")).toLongLong() != recipeId))
        return {{QStringLiteral("valid"), false}};
    const qint64 linkedBagId = recipe.value(QStringLiteral("bagId")).toLongLong();
    if (recipeId > 0 && linkedBagId > 0 && linkedBagId != setup.brew.activeBagId)
        return {{QStringLiteral("valid"), false}};

    QVariantMap bagPatch, recipePatch, equipmentPatch;
    QVariantList effects;
    const auto add = [&](const QString& record, const QVariantMap& row, QVariantMap& writes,
                         const QString& shotKey, const QString& rowKey) {
        if (!patch.contains(shotKey)) return;
        const QVariant before = row.value(rowKey);
        const QVariant after = patch.value(shotKey);
        if (rowKey == QLatin1String("bagId") || rowKey == QLatin1String("equipmentId")) {
            const qint64 oldId = before.toLongLong(), newId = after.toLongLong();
            if ((oldId > 0 ? oldId : -1) == (newId > 0 ? newId : -1)) return;
        } else if (rowKey == QLatin1String("doseG") || rowKey == QLatin1String("doseWeightG")
                   || rowKey == QLatin1String("rpm") || rowKey == QLatin1String("rpmPinned")
                   || rowKey == QLatin1String("lastRpm")) {
            if (std::abs(before.toDouble() - after.toDouble()) < 0.005) return;
        } else if (before == after) return;
        writes.insert(rowKey, after);
        effects.append(QVariantMap{{QStringLiteral("record"), record},
                                   {QStringLiteral("field"), rowKey},
                                   {QStringLiteral("before"), before},
                                   {QStringLiteral("after"), after}});
    };
    if (bagId > 0) {
        add(QStringLiteral("bag"), bag, bagPatch, QStringLiteral("doseWeight"), QStringLiteral("doseWeightG"));
        add(QStringLiteral("bag"), bag, bagPatch, QStringLiteral("grinderSetting"), QStringLiteral("grinderSetting"));
        add(QStringLiteral("bag"), bag, bagPatch, QStringLiteral("rpm"), QStringLiteral("rpm"));
        add(QStringLiteral("bag"), bag, bagPatch, QStringLiteral("equipmentId"), QStringLiteral("equipmentId"));
    }
    if (recipeId > 0) {
        add(QStringLiteral("recipe"), recipe, recipePatch, QStringLiteral("bagId"), QStringLiteral("bagId"));
        if (!recipe.value(QStringLiteral("profileTitle")).toString().trimmed().isEmpty())
            add(QStringLiteral("recipe"), recipe, recipePatch, QStringLiteral("doseWeight"), QStringLiteral("doseG"));
        if (DrinkTypes::hasGrind(recipe.value(QStringLiteral("drinkType")).toString())) {
            add(QStringLiteral("recipe"), recipe, recipePatch, QStringLiteral("grinderSetting"), QStringLiteral("grindPinned"));
            add(QStringLiteral("recipe"), recipe, recipePatch, QStringLiteral("rpm"), QStringLiteral("rpmPinned"));
        }
        add(QStringLiteral("recipe"), recipe, recipePatch, QStringLiteral("equipmentId"), QStringLiteral("equipmentId"));
    }
    if (equipmentId > 0) {
        add(QStringLiteral("equipment"), equipment, equipmentPatch, QStringLiteral("grinderSetting"), QStringLiteral("lastGrindSetting"));
        add(QStringLiteral("equipment"), equipment, equipmentPatch, QStringLiteral("rpm"), QStringLiteral("lastRpm"));
    }
    return {{QStringLiteral("valid"), true}, {QStringLiteral("bagId"), bagId},
            {QStringLiteral("equipmentId"), equipmentId}, {QStringLiteral("equipmentPatch"), equipmentPatch},
            {QStringLiteral("equipmentBefore"), equipment},
            {QStringLiteral("recipeId"), recipeId}, {QStringLiteral("bagPatch"), bagPatch},
            {QStringLiteral("recipePatch"), recipePatch}, {QStringLiteral("effects"), effects}};
}
