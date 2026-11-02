#include <QtTest>

#include <limits>
#include <QVariantList>

#include "core/machinecontrolpresets.h"
#include "core/settings.h"

class tst_MachineControlPresets : public QObject
{
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }
    void recipeWaterRepairsDisplacedSettings();
    void recipeWaterSwitchesToFixedAndResolvesLegacyName();
    void missingRatioVesselUsesResolvedGrams();
    void waterVesselSelectionAppliesFullPreset();
    void invalidWaterVesselDoesNotMutateOrApply();
    void flushPresetSelectionAppliesFullPreset();
    void invalidFlushPresetDoesNotMutateOrApply();
    void presetSnapshotsRejectStaleEditsAndReordering();
    void recipeIdsRequireExactPositiveIntegers();
};

void tst_MachineControlPresets::recipeWaterRepairsDisplacedSettings()
{
    Settings settings;
    auto* brew = settings.brew();
    brew->addWaterVesselPreset("Recipe ratio repair", 200, "volume", 31, 85);
    const int index = static_cast<int>(brew->waterVesselPresets().size()) - 1;
    const QJsonObject water{{"hasWater", true}, {"vesselName", "Recipe ratio repair"},
        {"amountMode", "ratio"}, {"waterToEspressoRatio", 2.5}, {"temperatureC", 90}, {"flowRate", 40}};
    const auto preset = brew->getWaterVesselPreset(index);
    int applies = 0;
    const auto apply = [&] { ++applies; };
    QVERIFY(MachineControlPresets::applyRecipeWater(brew, settings.hardware(), water, 100, apply));
    QCOMPARE(brew->waterVolume(), 100);
    QCOMPARE(brew->waterVolumeMode(), QStringLiteral("weight"));
    QCOMPARE(applies, 1);
    QVERIFY(MachineControlPresets::applyRecipeWater(brew, settings.hardware(), water, 100, apply));
    QCOMPARE(applies, 1);
    // A standalone commit uses this same fixed-preset path. The recipe must
    // repair its effective settings even though its ratio/yield did not change.
    QVERIFY(MachineControlPresets::selectWaterVessel(brew, settings.hardware(), index, {}));
    QCOMPARE(brew->waterVolume(), 200);
    QVERIFY(MachineControlPresets::applyRecipeWater(brew, settings.hardware(), water, 100, apply));
    QCOMPARE(brew->waterVolume(), 100);
    QCOMPARE(brew->waterTemperature(), 90.0);
    QCOMPARE(settings.hardware()->hotWaterFlowRate(), 40);
    QCOMPARE(brew->getWaterVesselPreset(index), preset);
    QCOMPARE(applies, 2);
}

void tst_MachineControlPresets::recipeWaterSwitchesToFixedAndResolvesLegacyName()
{
    Settings settings;
    auto* brew = settings.brew();
    brew->addWaterVesselPreset("Recipe fixed transition", 200, "volume", 31, 85);
    const int index = static_cast<int>(brew->waterVesselPresets().size()) - 1;
    QJsonObject water{{"hasWater", true}, {"vesselName", "Recipe fixed transition"}, {"amountMode", "ratio"}};
    QVERIFY(MachineControlPresets::applyRecipeWater(brew, settings.hardware(), water, 100, {}));
    water.insert("amountMode", "fixed");
    water.insert("volume", 150);
    water.insert("mode", "weight");
    QVERIFY(MachineControlPresets::applyRecipeWater(brew, settings.hardware(), water, 0, {}));
    QCOMPARE(brew->waterVolume(), 150);
    QCOMPARE(brew->getWaterVesselPreset(index).value("volume").toInt(), 200);
    const QJsonObject legacy{{"hasWater", true}, {"vesselName", "Recipe fixed transition"}};
    QVERIFY(MachineControlPresets::applyRecipeWater(brew, settings.hardware(), legacy, 0, {}));
    QCOMPARE(brew->waterVolume(), 200);
    QCOMPARE(brew->waterVolumeMode(), QStringLiteral("volume"));
    QCOMPARE(brew->waterTemperature(), 85.0);
}

void tst_MachineControlPresets::missingRatioVesselUsesResolvedGrams()
{
    Settings settings;
    auto* brew = settings.brew();
    const QJsonObject water{{"hasWater", true}, {"vesselName", "Recipe missing ratio"}, {"amountMode", "ratio"}};
    QVERIFY(MachineControlPresets::applyRecipeWater(brew, settings.hardware(), water, 90, {}));
    const int index = brew->selectedWaterVessel();
    QCOMPARE(brew->getWaterVesselPreset(index).value("volume").toInt(), 90);
    QVERIFY(MachineControlPresets::applyRecipeWater(brew, settings.hardware(), water, 105, {}));
    QCOMPARE(brew->waterVolume(), 105);
    QCOMPARE(brew->getWaterVesselPreset(index).value("volume").toInt(), 90);
}

void tst_MachineControlPresets::waterVesselSelectionAppliesFullPreset()
{
    Settings settings;
    auto* brew = settings.brew();
    auto* hardware = settings.hardware();
    brew->addWaterVesselPreset(QStringLiteral("Machine control test"), 250,
                               QStringLiteral("volume"), 31, 93.5);
    const int index = static_cast<int>(brew->waterVesselPresets().size()) - 1;
    int applies = 0;

    QVERIFY(MachineControlPresets::selectWaterVessel(
        brew, hardware, index, [&applies] { ++applies; }));

    QCOMPARE(brew->selectedWaterVessel(), index);
    QCOMPARE(brew->waterVolume(), 250);
    QCOMPARE(brew->waterVolumeMode(), QStringLiteral("volume"));
    QCOMPARE(hardware->hotWaterFlowRate(), 31);
    QCOMPARE(brew->waterTemperature(), 93.5);
    QCOMPARE(applies, 1);
}

void tst_MachineControlPresets::invalidWaterVesselDoesNotMutateOrApply()
{
    Settings settings;
    auto* brew = settings.brew();
    auto* hardware = settings.hardware();
    const int previousSelection = brew->selectedWaterVessel();
    const int previousVolume = brew->waterVolume();
    const QString previousMode = brew->waterVolumeMode();
    const int previousFlow = hardware->hotWaterFlowRate();
    const double previousTemperature = brew->waterTemperature();
    int applies = 0;

    QVERIFY(!MachineControlPresets::selectWaterVessel(
        brew, hardware, -1, [&applies] { ++applies; }));

    QCOMPARE(brew->selectedWaterVessel(), previousSelection);
    QCOMPARE(brew->waterVolume(), previousVolume);
    QCOMPARE(brew->waterVolumeMode(), previousMode);
    QCOMPARE(hardware->hotWaterFlowRate(), previousFlow);
    QCOMPARE(brew->waterTemperature(), previousTemperature);
    QCOMPARE(applies, 0);
}

void tst_MachineControlPresets::flushPresetSelectionAppliesFullPreset()
{
    Settings settings;
    auto* brew = settings.brew();
    brew->addFlushPreset(QStringLiteral("Machine control test"), 7.5, 12.0);
    const int index = static_cast<int>(brew->flushPresets().size()) - 1;
    int applies = 0;

    QVERIFY(MachineControlPresets::selectFlushPreset(
        brew, index, [&applies] { ++applies; }));

    QCOMPARE(brew->selectedFlushPreset(), index);
    QCOMPARE(brew->flushFlow(), 7.5);
    QCOMPARE(brew->flushSeconds(), 12.0);
    QCOMPARE(applies, 1);
}

void tst_MachineControlPresets::invalidFlushPresetDoesNotMutateOrApply()
{
    Settings settings;
    auto* brew = settings.brew();
    const int previousSelection = brew->selectedFlushPreset();
    const double previousFlow = brew->flushFlow();
    const double previousSeconds = brew->flushSeconds();
    int applies = 0;

    QVERIFY(!MachineControlPresets::selectFlushPreset(
        brew, -1, [&applies] { ++applies; }));

    QCOMPARE(brew->selectedFlushPreset(), previousSelection);
    QCOMPARE(brew->flushFlow(), previousFlow);
    QCOMPARE(brew->flushSeconds(), previousSeconds);
    QCOMPARE(applies, 0);
}

void tst_MachineControlPresets::presetSnapshotsRejectStaleEditsAndReordering()
{
    const QVariantMap expected{{QStringLiteral("name"), QStringLiteral("Small pitcher")},
                               {QStringLiteral("milkG"), 120}};
    QCOMPARE(MachineControlPresets::matchesSnapshot(expected, expected), true);
    QVERIFY(!MachineControlPresets::matchesSnapshot({}, expected));
    QVERIFY(!MachineControlPresets::matchesSnapshot(
        expected, {{QStringLiteral("name"), QStringLiteral("Small pitcher")},
                   {QStringLiteral("milkG"), 130}}));

    const QVariantList expectedOrder{
        expected,
        QVariantMap{{QStringLiteral("name"), QStringLiteral("Large pitcher")},
                    {QStringLiteral("milkG"), 300}}
    };
    const QVariantList currentOrder{expectedOrder.at(1), expectedOrder.at(0)};
    QVERIFY(MachineControlPresets::matchesSelection(
        0, 0, expectedOrder.at(0).toMap(), expectedOrder.at(0).toMap()));
    QVERIFY(!MachineControlPresets::matchesSelection(
        0, 1, expectedOrder.at(0).toMap(), currentOrder.at(0).toMap()));
    QVERIFY(!MachineControlPresets::matchesSelection(
        0, 0, expectedOrder.at(0).toMap(), currentOrder.at(0).toMap()));
}

void tst_MachineControlPresets::recipeIdsRequireExactPositiveIntegers()
{
    qint64 parsed = -1;
    QVERIFY(MachineControlPresets::readPositiveId(QVariant::fromValue<qint64>(42), &parsed));
    QCOMPARE(parsed, qint64(42));
    QVERIFY(MachineControlPresets::readPositiveId(QVariant::fromValue<quint64>(42), &parsed));
    QCOMPARE(parsed, qint64(42));
    QVERIFY(MachineControlPresets::readPositiveId(QVariant(42.0), &parsed));
    QCOMPARE(parsed, qint64(42));

    for (const QVariant& invalid : {QVariant(true), QVariant(QStringLiteral("42")),
                                    QVariant(42.5), QVariant(0), QVariant(-1),
                                    QVariant(std::numeric_limits<quint64>::max()),
                                    QVariant(std::numeric_limits<double>::infinity()),
                                    QVariant(std::numeric_limits<double>::quiet_NaN()),
                                    QVariant(9007199254740992.0)}) {
        QVERIFY2(!MachineControlPresets::readPositiveId(invalid, &parsed),
                 qPrintable(invalid.toString()));
    }
}

QTEST_GUILESS_MAIN(tst_MachineControlPresets)
#include "tst_machinecontrolpresets.moc"
