#include <QtTest>

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QVariantMap>

#include <cmath>
#include <limits>
#include <utility>

#include "core/settings.h"
#include "core/steampitcherselection.h"
#include "network/steampitchersnapshot.h"

namespace {
QVariantMap preset(QString name, double pitcherWeight)
{
    return {{QStringLiteral("name"), std::move(name)},
            {QStringLiteral("duration"), 30},
            {QStringLiteral("flow"), 150},
            {QStringLiteral("temperature"), 135.0},
            {QStringLiteral("pitcherWeightG"), pitcherWeight},
            {QStringLiteral("calibMilkG"), 250.0}};
}

QVariantMap heaterOff()
{
    return {{QStringLiteral("disabled"), true},
            {QStringLiteral("builtin"), true}};
}

QJsonObject validObject()
{
    const QJsonArray presets{
             QJsonObject{{QStringLiteral("name"), QStringLiteral("Small")},
                         {QStringLiteral("duration"), 30},
                         {QStringLiteral("flow"), 150},
                         {QStringLiteral("temperature"), 135.0},
                         {QStringLiteral("pitcherWeightG"), 165.5},
                         {QStringLiteral("calibMilkG"), 250.0}},
             QJsonObject{{QStringLiteral("name"), QStringLiteral("Large")}},
             QJsonObject{{QStringLiteral("disabled"), true},
                         {QStringLiteral("builtin"), true}}
         };
    const QString revision = QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(presets).toJson(QJsonDocument::Compact),
        QCryptographicHash::Sha256).toHex());
    return QJsonObject{
        {QStringLiteral("presets"), presets},
        {QStringLiteral("revision"), revision},
        {QStringLiteral("selectedIndex"), 1}
    };
}
} // namespace

class tst_SteamPitcherSnapshot : public QObject
{
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }

    void selectionHelperUsesSentinelAndSyntheticLastRow()
    {
        const QVariantList presets{
            preset(QStringLiteral("Small"), 160.0),
            preset(QStringLiteral("Large"), 220.0),
            heaterOff()
        };

        QCOMPARE(SteamPitcherSelection::presetAt(presets, 0).value(QStringLiteral("name")).toString(),
                 QStringLiteral("Small"));
        QCOMPARE(SteamPitcherSelection::presetAt(presets, -1), heaterOff());
        QCOMPARE(SteamPitcherSelection::presetAt(presets, 2), heaterOff());
        QVERIFY(SteamPitcherSelection::presetAt(presets, -2).isEmpty());
        QVERIFY(SteamPitcherSelection::presetAt(presets, 3).isEmpty());

        QCOMPARE(SteamPitcherSelection::displayIndex(presets, -1), 2);
        QCOMPARE(SteamPitcherSelection::displayIndex(presets, 2), 2);
        QCOMPARE(SteamPitcherSelection::displayIndex(presets, 1), 1);
        QCOMPARE(SteamPitcherSelection::displayIndex(presets, 9), 9);
        QCOMPARE(SteamPitcherSelection::displayIndex({}, -1), -1);
    }

    void nativeSettingsUsesTheSharedLookupAndDisplayRules()
    {
        Settings settings;
        SettingsBrew* brew = settings.brew();
        const QVariantList presets = brew->steamPitcherPresets();
        QVERIFY(!presets.isEmpty());
        const int offDisplayIndex = static_cast<int>(presets.size() - 1);

        QCOMPARE(brew->getSteamPitcherPreset(SettingsBrew::HeaterOffPitcherIndex),
                 SteamPitcherSelection::presetAt(presets, SteamPitcherSelection::HeaterOffIndex));
        QCOMPARE(brew->getSteamPitcherPreset(offDisplayIndex),
                 SteamPitcherSelection::presetAt(presets, offDisplayIndex));
        QCOMPARE(brew->getSteamPitcherPreset(-2), SteamPitcherSelection::presetAt(presets, -2));
        QCOMPARE(brew->getSteamPitcherPreset(offDisplayIndex + 1),
                 SteamPitcherSelection::presetAt(presets, offDisplayIndex + 1));

        brew->setSelectedSteamCup(SettingsBrew::HeaterOffPitcherIndex);
        QCOMPARE(brew->selectedSteamPitcherDisplayIndex(),
                 SteamPitcherSelection::displayIndex(presets, brew->selectedSteamPitcher()));
        QCOMPARE(brew->selectedSteamPitcherDisplayIndex(), offDisplayIndex);

        brew->setSelectedSteamCup(0);
        QCOMPARE(brew->selectedSteamPitcherDisplayIndex(),
                 SteamPitcherSelection::displayIndex(presets, brew->selectedSteamPitcher()));
    }

    void projectionWhitelistsFieldsAndKeepsMissingValuesAbsent()
    {
        QVariantMap userPreset = preset(QStringLiteral("Small"), 165.5);
        userPreset.insert(QStringLiteral("internalRecipeData"), QStringLiteral("do not export"));
        const SteamPitcherSnapshot projected = SteamPitcherSnapshot::project(
            {userPreset, QVariantMap{{QStringLiteral("name"), QStringLiteral("Partial")}}, heaterOff()}, 1);
        const QJsonObject wire = projected.toJson();

        QCOMPARE(wire.keys(), QStringList({QStringLiteral("presets"), QStringLiteral("revision"),
                                           QStringLiteral("selectedIndex")}));
        const QJsonArray wirePresets = wire.value(QStringLiteral("presets")).toArray();
        QCOMPARE(wirePresets.size(), 3);
        QVERIFY(!wirePresets.at(0).toObject().contains(QStringLiteral("internalRecipeData")));
        QVERIFY(!wirePresets.at(1).toObject().contains(QStringLiteral("duration")));
        QCOMPARE(wirePresets.at(2).toObject().value(QStringLiteral("disabled")).toBool(), true);
        QCOMPARE(wirePresets.at(2).toObject().value(QStringLiteral("builtin")).toBool(), true);

        SteamPitcherSnapshot parsed;
        QVERIFY2(SteamPitcherSnapshot::parse(wire, &parsed), "projected native presets parse");
        QCOMPARE(parsed.presets.size(), 3);
        QVERIFY(!parsed.presets.at(1).toMap().contains(QStringLiteral("duration")));
        QCOMPARE(parsed.selectedIndex, 1);
        QCOMPARE(parsed.displayIndex(), 1);
        QCOMPARE(parsed.presetAt(-1), heaterOff());
        QCOMPARE(wire.value(QStringLiteral("revision")).toString(), parsed.revision());
    }

    void malformedOptionalNativeFieldsRemainRepresentedForStrictClientRejection()
    {
        QVariantMap malformed{{QStringLiteral("name"), QStringLiteral("Old")},
                              {QStringLiteral("duration"), QStringLiteral("not a number")},
                              {QStringLiteral("temperature"), std::numeric_limits<double>::infinity()}};
        const SteamPitcherSnapshot projected = SteamPitcherSnapshot::project(
            {malformed, heaterOff()}, 0);
        QCOMPARE(projected.presets.size(), 2);
        QVERIFY(projected.presets.at(0).toMap().contains(QStringLiteral("name")));
        QCOMPARE(projected.presets.at(0).toMap().value(QStringLiteral("duration")).toString(),
                 QStringLiteral("not a number"));
        QVERIFY(std::isinf(projected.presets.at(0).toMap()
                               .value(QStringLiteral("temperature")).toDouble()));

        const QJsonObject wirePreset = projected.toJson()
            .value(QStringLiteral("presets")).toArray().at(0).toObject();
        QCOMPARE(wirePreset.value(QStringLiteral("duration")).toString(),
                 QStringLiteral("not a number"));
        SteamPitcherSnapshot parsed;
        QString error;
        QVERIFY(!SteamPitcherSnapshot::parse(projected.toJson(), &parsed, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(projected.presets.size(), 2);
    }

    void validSnapshotRoundTripsAndPreservesSelectionAliases()
    {
        SteamPitcherSnapshot parsed;
        QVERIFY2(SteamPitcherSnapshot::parse(validObject(), &parsed), "valid pitcher snapshot parses");
        QCOMPARE(parsed.presets.size(), 3);
        QCOMPARE(parsed.selectedIndex, 1);
        QCOMPARE(parsed.displayIndex(), 1);
        QCOMPARE(parsed.presetAt(0).value(QStringLiteral("pitcherWeightG")).toDouble(), 165.5);
        QVERIFY(!parsed.presetAt(1).contains(QStringLiteral("duration")));

        SteamPitcherSnapshot heaterOffSelected;
        QJsonObject offObject = validObject();
        offObject.insert(QStringLiteral("selectedIndex"), -1);
        QVERIFY(SteamPitcherSnapshot::parse(offObject, &heaterOffSelected));
        QCOMPARE(heaterOffSelected.displayIndex(), 2);
        QCOMPARE(heaterOffSelected.presetAt(heaterOffSelected.displayIndex()), heaterOff());

        offObject.insert(QStringLiteral("selectedIndex"), 2);
        QVERIFY(SteamPitcherSnapshot::parse(offObject, &heaterOffSelected));
        QCOMPARE(heaterOffSelected.displayIndex(), 2);
    }

    void malformedSnapshotsAreRejectedAtomically()
    {
        SteamPitcherSnapshot output;
        output.presets = {preset(QStringLiteral("Keep"), 100.0), heaterOff()};
        output.selectedIndex = -1;
        const SteamPitcherSnapshot previous = output;

        auto rejects = [&](const QJsonObject& object) {
            QString error;
            QVERIFY(!SteamPitcherSnapshot::parse(object, &output, &error));
            QVERIFY(!error.isEmpty());
            QCOMPARE(output.presets, previous.presets);
            QCOMPARE(output.selectedIndex, previous.selectedIndex);
        };

        QJsonObject object = validObject();
        object.remove(QStringLiteral("presets"));
        rejects(object);
        object = validObject();
        object.insert(QStringLiteral("extra"), true);
        rejects(object);
        object = validObject();
        object.insert(QStringLiteral("revision"), QString(64, QLatin1Char('0')));
        rejects(object);
        object = validObject();
        object.remove(QStringLiteral("revision"));
        rejects(object);
        object = validObject();
        object.insert(QStringLiteral("selectedIndex"), 1.5);
        rejects(object);
        object = validObject();
        object.insert(QStringLiteral("selectedIndex"), true);
        rejects(object);
        object = validObject();
        object.insert(QStringLiteral("selectedIndex"), QJsonValue(QJsonValue::Null));
        rejects(object);
        object = validObject();
        object.insert(QStringLiteral("selectedIndex"), 5);
        SteamPitcherSnapshot outOfRangeSelection;
        QVERIFY(SteamPitcherSnapshot::parse(object, &outOfRangeSelection));
        QCOMPARE(outOfRangeSelection.selectedIndex, 5);
        QCOMPARE(outOfRangeSelection.displayIndex(), 5);
        QVERIFY(outOfRangeSelection.presetAt(5).isEmpty());
        object.insert(QStringLiteral("selectedIndex"), 2147483648.0);
        rejects(object);
        object = validObject();
        object.insert(QStringLiteral("selectedIndex"), -2147483649.0);
        rejects(object);
        object = validObject();
        QJsonArray rows = object.value(QStringLiteral("presets")).toArray();
        rows.replace(2, QJsonObject{{QStringLiteral("disabled"), true}});
        object.insert(QStringLiteral("presets"), rows);
        rejects(object);
        object = validObject();
        rows = object.value(QStringLiteral("presets")).toArray();
        rows.replace(0, QJsonObject{{QStringLiteral("name"), QStringLiteral("Bad")},
                                    {QStringLiteral("duration"), QStringLiteral("30")}});
        object.insert(QStringLiteral("presets"), rows);
        rejects(object);
        object = validObject();
        rows = object.value(QStringLiteral("presets")).toArray();
        rows.replace(0, QJsonObject{{QStringLiteral("name"), QStringLiteral("Bad")},
                                    {QStringLiteral("unexpected"), 2}});
        object.insert(QStringLiteral("presets"), rows);
        rejects(object);
        object = validObject();
        rows = object.value(QStringLiteral("presets")).toArray();
        rows.replace(0, QJsonObject{{QStringLiteral("name"), QStringLiteral("Bad")},
                                    {QStringLiteral("temperature"), QJsonValue(QJsonValue::Null)}});
        object.insert(QStringLiteral("presets"), rows);
        rejects(object);
        object = validObject();
        rows = object.value(QStringLiteral("presets")).toArray();
        rows.replace(0, QJsonValue(QStringLiteral("not an object")));
        object.insert(QStringLiteral("presets"), rows);
        rejects(object);
        object = validObject();
        object.insert(QStringLiteral("presets"), QJsonArray{});
        rejects(object);
    }

    void revisionCoversOrderedPresetDataButNotSelection()
    {
        const SteamPitcherSnapshot first = SteamPitcherSnapshot::project(
            {preset(QStringLiteral("Small"), 150.0),
             preset(QStringLiteral("Large"), 250.0), heaterOff()}, 0);
        SteamPitcherSnapshot changedSelection = first;
        changedSelection.selectedIndex = -1;
        QCOMPARE(first.revision(), changedSelection.revision());

        const SteamPitcherSnapshot changedValue = SteamPitcherSnapshot::project(
            {preset(QStringLiteral("Small"), 151.0),
             preset(QStringLiteral("Large"), 250.0), heaterOff()}, 0);
        QVERIFY(first.revision() != changedValue.revision());
        const SteamPitcherSnapshot reordered = SteamPitcherSnapshot::project(
            {preset(QStringLiteral("Large"), 250.0),
             preset(QStringLiteral("Small"), 150.0), heaterOff()}, 0);
        QVERIFY(first.revision() != reordered.revision());
        QCOMPARE(reordered.presets.constLast().toMap(), heaterOff());
    }

    void everyKnownPresetFieldRejectsWrongTypes()
    {
        const QStringList numericFields{
            QStringLiteral("duration"), QStringLiteral("flow"),
            QStringLiteral("temperature"), QStringLiteral("pitcherWeightG"),
            QStringLiteral("calibMilkG")
        };
        QStringList fields{QStringLiteral("name"), QStringLiteral("disabled"),
                           QStringLiteral("builtin")};
        fields.append(numericFields);

        for (const QString& field : fields) {
            QJsonObject object = validObject();
            QJsonArray rows = object.value(QStringLiteral("presets")).toArray();
            QJsonObject first = rows.at(0).toObject();
            first.insert(field, field == QStringLiteral("name")
                ? QJsonValue(false) : QJsonValue(QStringLiteral("wrong type")));
            rows.replace(0, first);
            object.insert(QStringLiteral("presets"), rows);
            SteamPitcherSnapshot parsed;
            QString error;
            QVERIFY2(!SteamPitcherSnapshot::parse(object, &parsed, &error),
                     qPrintable(QStringLiteral("accepted wrong type for %1").arg(field)));
            QVERIFY(!error.isEmpty());
        }
    }
};

QTEST_GUILESS_MAIN(tst_SteamPitcherSnapshot)
#include "tst_steampitchersnapshot.moc"
