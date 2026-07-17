#include "recipepromotion.h"

#include "shothistory_types.h"
#include "../network/beanbase_blob.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDebug>

namespace RecipePromotion {

QVariantMap fieldsFromShotRecord(const ShotRecord& record, const QString& name,
                                  std::optional<bool> hasMilkOverride,
                                  const QString& fallbackSteamJson) {
    const QString beanBaseId = BeanBaseBlob::canonicalId(record.beanBaseJson);

    QString steamJson = !record.steamJson.isEmpty() ? record.steamJson : fallbackSteamJson;
    if (hasMilkOverride.has_value() && !steamJson.isEmpty()) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(steamJson.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            qWarning() << "RecipePromotion::fieldsFromShotRecord: malformed steamJson for shot"
                       << record.summary.id << "-" << parseError.errorString()
                       << "- other steam fields (e.g. milkWeightG) will be dropped";
        }
        QJsonObject steam = doc.object();
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
    // The shot's own bag becomes the recipe's hard bag link (recipes link a
    // specific bag, not a bean). A pre-bag shot (bagId <= 0) yields 0 = no
    // link; the bean identity fields below still carry, so wake-on-restock
    // can home the recipe onto a future bag of that bean.
    fields.insert("bagId", record.bagId > 0 ? record.bagId : 0);
    fields.insert("beanBaseId", beanBaseId);
    fields.insert("roasterName", record.summary.beanBrand);
    fields.insert("coffeeName", record.summary.beanType);
    fields.insert("equipmentId", record.equipmentId);
    fields.insert("doseG", record.summary.doseWeight);
    // Promotion COPIES the shot's yield anchor — mode and value — never
    // reconstructing a ratio from target ÷ dose (add-yield-ratio-anchor).
    // dose_weight is post-shot editable; a corrected dose would mint a ratio
    // nobody chose. A 1:2 shot therefore promotes to a 1:2 recipe, a legacy
    // shot (backfilled absolute) to an absolute one — exactly as it recorded.
    if (record.yieldMode == QLatin1String("ratio")
        || record.yieldMode == QLatin1String("absolute")) {
        fields.insert("yieldValue", record.yieldAnchorValue);
        fields.insert("yieldMode", record.yieldMode);
    } else if (record.targetWeight > 0) {
        // No recorded anchor (external import): the recorded target promotes
        // as an absolute, matching pre-anchor behaviour.
        fields.insert("yieldValue", record.targetWeight);
        fields.insert("yieldMode", QStringLiteral("absolute"));
    } else {
        fields.insert("yieldMode", QStringLiteral("none"));
    }
    // The shot's temperature override is a frozen ABSOLUTE; the recipe stores
    // a SIGNED OFFSET against its profile (recipe-relative-temp-offset).
    // Convert against the shot's own profile snapshot — the profile as it was
    // when the shot was pulled, which is what the override was relative to.
    // No snapshot (or no override) → no pin: a delta against an unknown
    // baseline is meaningless.
    double tempOffsetC = 0;
    if (record.temperatureOverride > 0 && !record.profileJson.isEmpty()) {
        const QJsonObject profile =
            QJsonDocument::fromJson(record.profileJson.toUtf8()).object();
        double profileTemp = profile.value(QStringLiteral("espresso_temperature"))
                                 .toString().toDouble();
        if (profileTemp <= 0)
            profileTemp = profile.value(QStringLiteral("espresso_temperature")).toDouble();
        if (profileTemp > 0) {
            tempOffsetC = record.temperatureOverride - profileTemp;
            if (qAbs(tempOffsetC) < 0.05)
                tempOffsetC = 0;
        } else {
            qWarning() << "RecipePromotion: shot" << record.summary.id
                       << "has a temperature override but its profile snapshot has no"
                          " espresso_temperature - dropping the temperature pin";
        }
    }
    fields.insert("tempOffsetC", tempOffsetC);
    // The shot's own recorded dial is the recipe's grind — the exact grind
    // that produced the shot being promoted (grind always lives on the
    // recipe; the old empty-means-inherit encoding is retired).
    fields.insert("grindPinned", record.grinderSetting);
    fields.insert("rpmPinned", record.rpm);
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

bool isEligibleForStarterRecipe(bool recipeCountOk, qint64 recipeCount,
                                 qint64 latestShotId, qint64 loadedRecordId) {
    return recipeCountOk && recipeCount == 0 && latestShotId > 0 && loadedRecordId > 0;
}

} // namespace RecipePromotion
