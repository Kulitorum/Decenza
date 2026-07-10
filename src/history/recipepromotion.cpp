#include "recipepromotion.h"

#include "shothistory_types.h"
#include "../network/beanbase_blob.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace RecipePromotion {

QVariantMap fieldsFromShotRecord(const ShotRecord& record, const QString& name,
                                  std::optional<bool> hasMilkOverride,
                                  const QString& fallbackSteamJson) {
    const QString beanBaseId = BeanBaseBlob::canonicalId(record.beanBaseJson);
    const bool hasBean = !beanBaseId.isEmpty()
        || !record.summary.beanBrand.isEmpty() || !record.summary.beanType.isEmpty();

    QString steamJson = !record.steamJson.isEmpty() ? record.steamJson : fallbackSteamJson;
    if (hasMilkOverride.has_value() && !steamJson.isEmpty()) {
        QJsonObject steam = QJsonDocument::fromJson(steamJson.toUtf8()).object();
        steam["hasMilk"] = *hasMilkOverride;
        steamJson = QString::fromUtf8(QJsonDocument(steam).toJson(QJsonDocument::Compact));
    } else if (hasMilkOverride.has_value() && steamJson.isEmpty()) {
        steamJson = QString::fromUtf8(QJsonDocument(
            QJsonObject{{"hasMilk", *hasMilkOverride}}).toJson(QJsonDocument::Compact));
    }

    QVariantMap fields;
    fields.insert("name", name);
    fields.insert("profileTitle", record.summary.profileName);
    fields.insert("profileJson", record.profileJson);
    fields.insert("beanBaseId", beanBaseId);
    fields.insert("roasterName", record.summary.beanBrand);
    fields.insert("coffeeName", record.summary.beanType);
    fields.insert("equipmentId", record.equipmentId);
    fields.insert("doseG", record.summary.doseWeight);
    fields.insert("yieldG", record.targetWeight);
    fields.insert("tempOverrideC", record.temperatureOverride);
    fields.insert("grindPinned", hasBean ? QString() : record.grinderSetting);
    fields.insert("steamJson", steamJson);
    // Hot water carries verbatim from the shot snapshot only — NO
    // current-settings fallback (that would force a shot pulled while an
    // Americano recipe is active into an Americano, mirroring the composer's
    // deliberate choice).
    fields.insert("hotWaterJson", record.hotWaterJson);
    fields.insert("createdFromShotId", record.summary.id);
    return fields;
}

bool milkPreselectedFromSteamJson(const QString& steamJson) {
    if (steamJson.isEmpty())
        return false;
    const QJsonObject steam = QJsonDocument::fromJson(steamJson.toUtf8()).object();
    return steam.value("hasMilk").toBool() || steam.value("milkWeightG").toDouble() > 0;
}

} // namespace RecipePromotion
