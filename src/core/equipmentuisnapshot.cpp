#include "equipmentuisnapshot.h"

#include "settings_dye.h"
#include "../history/equipmentstorage.h"
#include "../history/shothistorystorage.h"

#include <QVariantList>

namespace EquipmentUiSnapshot {

QVariantMap createSeed(const SettingsDye* dye)
{
    if (!dye)
        return {};
    return {
        {QStringLiteral("grinderBrand"), dye->dyeGrinderBrand()},
        {QStringLiteral("grinderModel"), dye->dyeGrinderModel()},
        {QStringLiteral("grinderBurrs"), dye->dyeGrinderBurrs()},
        {QStringLiteral("basketBrand"), dye->dyeBasketBrand()},
        {QStringLiteral("basketModel"), dye->dyeBasketModel()},
        {QStringLiteral("puckPrepCanonical"), dye->dyePuckPrepCanonical()}
    };
}

QVariantMap options(const SettingsDye* dye, ShotHistoryStorage* history)
{
    if (!dye)
        return {};

    QVariantMap grinderModels;
    QVariantMap grinderBurrs;
    for (const QString& brand : dye->knownGrinderBrands()) {
        const QStringList models = dye->knownGrinderModels(brand);
        grinderModels.insert(brand, models);
        QVariantMap byModel;
        for (const QString& model : models) {
            byModel.insert(model, QVariantMap{
                {QStringLiteral("swappable"), dye->isBurrSwappable(brand, model)},
                {QStringLiteral("suggestions"), dye->suggestedBurrs(brand, model)}
            });
        }
        grinderBurrs.insert(brand, byModel);
    }

    QVariantMap basketModels;
    QVariantMap basketSummaries;
    for (const QString& brand : dye->knownBasketBrands()) {
        const QStringList models = dye->knownBasketModels(brand);
        basketModels.insert(brand, models);
        QVariantMap byModel;
        for (const QString& model : models)
            byModel.insert(model, dye->basketModelSummary(brand, model));
        basketSummaries.insert(brand, byModel);
    }

    QVariantList puckPrepFlags;
    for (const QVariant& value : EquipmentStorage::puckPrepFlags()) {
        const QVariantMap flag = value.toMap();
        puckPrepFlags.append(QVariantMap{
            {QStringLiteral("key"), flag.value(QStringLiteral("key"))},
            {QStringLiteral("labelKey"), flag.value(QStringLiteral("labelKey"))},
            {QStringLiteral("label"), flag.value(QStringLiteral("label"))}
        });
    }

    const QStringList historyBrands = history ? history->getDistinctGrinderBrands() : QStringList{};
    return {
        {QStringLiteral("grinderBrands"), dye->knownGrinderBrands()},
        {QStringLiteral("grinderHistoryBrands"), historyBrands},
        {QStringLiteral("grinderModels"), grinderModels},
        {QStringLiteral("grinderBurrs"), grinderBurrs},
        {QStringLiteral("basketBrands"), dye->knownBasketBrands()},
        {QStringLiteral("basketModels"), basketModels},
        {QStringLiteral("basketSummaries"), basketSummaries},
        {QStringLiteral("puckPrepFlags"), puckPrepFlags}
    };
}

} // namespace EquipmentUiSnapshot
