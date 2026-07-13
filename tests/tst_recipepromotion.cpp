#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>

#include "history/recipepromotion.h"
#include "history/shothistory_types.h"

// RecipePromotion::fieldsFromShotRecord (recipes-idle-layout-upgrade): the
// shared promotion field-builder used by both the recipe_create_from_shot
// MCP tool and the upgrade dialog's starter-recipe hook.

static ShotRecord sampleShotRecord() {
    ShotRecord record;
    record.summary.id = 42;
    record.summary.profileName = "D-Flow / default";
    record.summary.beanBrand = "Roaster";
    record.summary.beanType = "Guji";
    record.summary.doseWeight = 18.0;
    record.profileJson = "{\"title\":\"D-Flow / default\",\"espresso_temperature\":\"90\"}";
    record.equipmentId = 7;
    record.targetWeight = 36.0;
    record.temperatureOverride = 92.5;
    record.grinderSetting = "22";
    record.steamJson = "{\"hasMilk\":false,\"milkWeightG\":0}";
    record.hotWaterJson = "{\"hasWater\":false}";
    return record;
}

class tst_RecipePromotion : public QObject {
    Q_OBJECT

private slots:

    // The shot's bag becomes the recipe's hard bag link (recipes link a
    // specific bag); a pre-bag shot (bagId <= 0) stores no link — the bean
    // identity fields still carry as the relink matching key.
    void carriesShotBagId() {
        ShotRecord record = sampleShotRecord();
        record.bagId = 5;
        QCOMPARE(RecipePromotion::fieldsFromShotRecord(record, "R", std::nullopt, QString())
                     .value("bagId").toLongLong(), (qint64)5);
        record.bagId = -1;  // pre-bag shot
        QCOMPARE(RecipePromotion::fieldsFromShotRecord(record, "R", std::nullopt, QString())
                     .value("bagId").toLongLong(), (qint64)0);
    }
    // No hasMilk override: the shot's own steam snapshot carries through
    // verbatim (the MCP tool's default behavior).
    void fieldsCarryShotDataVerbatimWithoutOverride() {
        const ShotRecord record = sampleShotRecord();
        const QVariantMap fields = RecipePromotion::fieldsFromShotRecord(
            record, "My espresso", std::nullopt, QString());

        QCOMPARE(fields.value("name").toString(), QStringLiteral("My espresso"));
        QCOMPARE(fields.value("profileTitle").toString(), QStringLiteral("D-Flow / default"));
        QCOMPARE(fields.value("profileJson").toString(), record.profileJson);
        QCOMPARE(fields.value("roasterName").toString(), QStringLiteral("Roaster"));
        QCOMPARE(fields.value("coffeeName").toString(), QStringLiteral("Guji"));
        QCOMPARE(fields.value("equipmentId").toLongLong(), qint64(7));
        QCOMPARE(fields.value("doseG").toDouble(), 18.0);
        QCOMPARE(fields.value("yieldG").toDouble(), 36.0);
        // The shot's 92.5 absolute converts to an offset against the profile
        // snapshot's 90° (recipe-relative-temp-offset).
        QCOMPARE(fields.value("tempOffsetC").toDouble(), 2.5);
        QCOMPARE(fields.value("steamJson").toString(), record.steamJson);
        QCOMPARE(fields.value("hotWaterJson").toString(), record.hotWaterJson);
        QCOMPARE(fields.value("createdFromShotId").toLongLong(), qint64(42));
    }

    // Temperature conversion edges (recipe-relative-temp-offset): no override
    // → offset 0; an override whose profile snapshot carries no temperature
    // → offset 0 (a delta against an unknown baseline is meaningless); an
    // override matching the snapshot temp → offset 0 (rounds away).
    void temperatureOffsetConversionEdges() {
        ShotRecord noOverride = sampleShotRecord();
        noOverride.temperatureOverride = 0;
        QCOMPARE(RecipePromotion::fieldsFromShotRecord(noOverride, "n", std::nullopt, QString())
                     .value("tempOffsetC").toDouble(), 0.0);

        ShotRecord noAnchor = sampleShotRecord();
        noAnchor.profileJson = "{\"title\":\"D-Flow / default\"}";  // no espresso_temperature
        QCOMPARE(RecipePromotion::fieldsFromShotRecord(noAnchor, "n", std::nullopt, QString())
                     .value("tempOffsetC").toDouble(), 0.0);

        ShotRecord atProfile = sampleShotRecord();
        atProfile.temperatureOverride = 90.0;  // equals the snapshot's 90°
        QCOMPARE(RecipePromotion::fieldsFromShotRecord(atProfile, "n", std::nullopt, QString())
                     .value("tempOffsetC").toDouble(), 0.0);
    }

    // The shot's own recorded grind/rpm become the recipe's grind regardless
    // of a bean link (fix-recipe-grind-integrity: grind always lives on the
    // recipe — the old empty-means-inherit encoding is retired).
    void grindAlwaysCarriesFromShot() {
        ShotRecord withBean = sampleShotRecord();
        withBean.rpm = 90;
        const QVariantMap withBeanFields = RecipePromotion::fieldsFromShotRecord(
            withBean, "n", std::nullopt, QString());
        QCOMPARE(withBeanFields.value("grindPinned").toString(), QStringLiteral("22"));
        QCOMPARE(withBeanFields.value("rpmPinned").toLongLong(), qint64(90));

        ShotRecord noBean = sampleShotRecord();
        noBean.summary.beanBrand.clear();
        noBean.summary.beanType.clear();
        const QVariantMap noBeanFields = RecipePromotion::fieldsFromShotRecord(
            noBean, "n", std::nullopt, QString());
        QCOMPARE(noBeanFields.value("grindPinned").toString(), QStringLiteral("22"));
    }

    // A bean link resolved purely through the Bean Base canonical id (no
    // free-text brand/type on the shot) still carries the canonical id — and
    // the shot's grind rides along like any other promotion.
    void beanBaseIdOnlyLinkStillCarries() {
        ShotRecord record = sampleShotRecord();
        record.summary.beanBrand.clear();
        record.summary.beanType.clear();
        record.beanBaseJson = "{\"visualizerCanonicalId\":\"bb-uuid-1\"}";
        const QVariantMap fields = RecipePromotion::fieldsFromShotRecord(
            record, "n", std::nullopt, QString());
        QCOMPARE(fields.value("grindPinned").toString(), QStringLiteral("22"));
        QCOMPARE(fields.value("beanBaseId").toString(), QStringLiteral("bb-uuid-1"));
    }

    // hasMilkOverride stamps hasMilk onto an existing steam snapshot without
    // disturbing its other fields (e.g. milkWeightG).
    void hasMilkOverrideStampsExistingSteamJson() {
        ShotRecord record = sampleShotRecord();
        record.steamJson = "{\"hasMilk\":false,\"milkWeightG\":150}";
        const QVariantMap fields = RecipePromotion::fieldsFromShotRecord(
            record, "n", std::optional<bool>(true), QString());

        const QJsonObject steam = QJsonDocument::fromJson(
            fields.value("steamJson").toString().toUtf8()).object();
        QCOMPARE(steam.value("hasMilk").toBool(), true);
        QCOMPARE(steam.value("milkWeightG").toDouble(), 150.0);
    }

    // A shot that predates steam snapshots (empty steamJson) falls back to
    // the caller-supplied current-settings snapshot — matching the MCP tool's
    // existing older-shot behavior.
    void emptySteamJsonFallsBackToCallerSnapshot() {
        ShotRecord record = sampleShotRecord();
        record.steamJson.clear();
        const QString fallback = "{\"hasMilk\":false,\"pitcherName\":\"Small\"}";
        const QVariantMap fields = RecipePromotion::fieldsFromShotRecord(
            record, "n", std::nullopt, fallback);
        QCOMPARE(fields.value("steamJson").toString(), fallback);
    }

    // hasMilkOverride with no steam snapshot AND no fallback synthesizes a
    // minimal steam block carrying just the override — never leaves the
    // recipe's steam block silently empty when the user made an explicit
    // Espresso/Milk choice.
    void hasMilkOverrideSynthesizesSteamJsonWhenNoneAvailable() {
        ShotRecord record = sampleShotRecord();
        record.steamJson.clear();
        const QVariantMap fields = RecipePromotion::fieldsFromShotRecord(
            record, "n", std::optional<bool>(true), QString());
        const QJsonObject steam = QJsonDocument::fromJson(
            fields.value("steamJson").toString().toUtf8()).object();
        QCOMPARE(steam.value("hasMilk").toBool(), true);
    }

    // Hot water carries verbatim from the shot snapshot only — no
    // current-settings fallback, regardless of fallbackSteamJson (which only
    // covers the steam block).
    void hotWaterCarriesVerbatimNeverFallsBack() {
        ShotRecord record = sampleShotRecord();
        record.hotWaterJson = "{\"hasWater\":true,\"vesselName\":\"Kettle\"}";
        const QVariantMap fields = RecipePromotion::fieldsFromShotRecord(
            record, "n", std::nullopt, "{\"hasWater\":false}");
        QCOMPARE(fields.value("hotWaterJson").toString(), record.hotWaterJson);
    }

    // milkPreselectedFromSteamJson: the upgrade dialog's Espresso/Milk
    // heuristic (recipes-idle-layout-upgrade decision 5).

    void milkPreselectedWhenHasMilkTrue() {
        QVERIFY(RecipePromotion::milkPreselectedFromSteamJson(
            "{\"hasMilk\":true,\"milkWeightG\":0}"));
    }

    void milkPreselectedWhenMilkWeightPositive() {
        // hasMilk absent/false but a nonzero milkWeightG still pre-selects
        // milk — the persistent last-steam-ever value.
        QVERIFY(RecipePromotion::milkPreselectedFromSteamJson(
            "{\"hasMilk\":false,\"milkWeightG\":180}"));
    }

    void espressoPreselectedWhenNoMilkSignal() {
        QVERIFY(!RecipePromotion::milkPreselectedFromSteamJson(
            "{\"hasMilk\":false,\"milkWeightG\":0}"));
    }

    void espressoPreselectedWhenSteamJsonEmpty() {
        // A shot with no steam snapshot (predates the feature, or an
        // espresso-only shot) must not crash and defaults to espresso.
        QVERIFY(!RecipePromotion::milkPreselectedFromSteamJson(QString()));
    }

    // isEligibleForStarterRecipe: the recipes-idle-layout-upgrade offer's
    // starter-recipe gate. recipeCountOk=false must always mean "not
    // eligible" — an unreliable count must never be read as "zero recipes",
    // which would create a spurious duplicate for a user who already has
    // some (the bug this predicate was extracted to make impossible to
    // reintroduce silently).

    void eligibleWhenNoRecipesAndShotLoaded() {
        QVERIFY(RecipePromotion::isEligibleForStarterRecipe(
            /*recipeCountOk=*/true, /*recipeCount=*/0, /*latestShotId=*/42, /*loadedRecordId=*/42));
    }

    void ineligibleWhenRecipeCountUnreliable() {
        // DB open/query failure — even though the (stale/default) count
        // reads as zero and a shot was found, an unreliable count must fail
        // closed, not be treated as "confirmed zero recipes".
        QVERIFY(!RecipePromotion::isEligibleForStarterRecipe(
            /*recipeCountOk=*/false, /*recipeCount=*/0, /*latestShotId=*/42, /*loadedRecordId=*/42));
    }

    void ineligibleWhenUserHasRecipes() {
        QVERIFY(!RecipePromotion::isEligibleForStarterRecipe(
            /*recipeCountOk=*/true, /*recipeCount=*/3, /*latestShotId=*/42, /*loadedRecordId=*/42));
    }

    void ineligibleWhenNoSavedShots() {
        QVERIFY(!RecipePromotion::isEligibleForStarterRecipe(
            /*recipeCountOk=*/true, /*recipeCount=*/0, /*latestShotId=*/-1, /*loadedRecordId=*/0));
    }

    void ineligibleWhenShotIdFoundButRecordFailedToLoad() {
        // A shot id was found by the latest-shot query but loadShotRecordStatic
        // came back empty (e.g. a race with a concurrent delete) — the found
        // id alone isn't enough, the record must have actually loaded.
        QVERIFY(!RecipePromotion::isEligibleForStarterRecipe(
            /*recipeCountOk=*/true, /*recipeCount=*/0, /*latestShotId=*/42, /*loadedRecordId=*/0));
    }
};

QTEST_GUILESS_MAIN(tst_RecipePromotion)
#include "tst_recipepromotion.moc"
