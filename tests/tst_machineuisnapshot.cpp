#include <QtTest>

#include <QJsonObject>
#include <QJsonValue>
#include <QUuid>
#include <limits>

#include "machine/machinestate.h"
#include "machine/machinephase.h"
#include "network/machineuisnapshot.h"
#include "webclient/remotemachinestate.h"

#define DECENZA_ASSERT_PHASE_PARITY(name) \
    static_assert(static_cast<int>(MachinePhase::name) \
                  == static_cast<int>(MachineState::Phase::name));
DECENZA_MACHINE_PHASES(DECENZA_ASSERT_PHASE_PARITY)
#undef DECENZA_ASSERT_PHASE_PARITY

namespace {
QJsonObject validObject()
{
    return QJsonObject{
        {QStringLiteral("de1Connected"), true},
        {QStringLiteral("de1GuiEnabled"), true},
        {QStringLiteral("simulationMode"), false},
        {QStringLiteral("de1IsHeadless"), true},
        {QStringLiteral("de1State"), 4},
        {QStringLiteral("de1SubState"), 5},
        {QStringLiteral("stateString"), QStringLiteral("Espresso")},
        {QStringLiteral("subStateString"), QStringLiteral("Pouring")},
        {QStringLiteral("temperature"), 93.25},
        {QStringLiteral("steamTemperature"), 128.5},
        {QStringLiteral("waterLevel"), 0.72},
        {QStringLiteral("waterLevelMm"), 141.0},
        {QStringLiteral("waterLevelMl"), 812},
        {QStringLiteral("refillKitDetected"), 1},
        {QStringLiteral("phase"), QStringLiteral("Pouring")},
        {QStringLiteral("isFlowing"), true},
        {QStringLiteral("isHeating"), false},
        {QStringLiteral("isReady"), false},
        {QStringLiteral("scaleWeight"), 24.6},
        {QStringLiteral("scaleFlowRate"), 1.8},
        {QStringLiteral("cumulativeVolume"), 30.1},
        {QStringLiteral("preinfusionVolume"), 8.4},
        {QStringLiteral("pourVolume"), 21.7},
        {QStringLiteral("shotTime"), 18.4},
        {QStringLiteral("targetWeight"), 36.0},
        {QStringLiteral("targetVolume"), 0.0},
        {QStringLiteral("smoothedScaleFlowRate"), 1.1},
        {QStringLiteral("canDecreaseTargetWeight"), true},
        {QStringLiteral("hotWaterTargetWeight"), 80.0},
        {QStringLiteral("filteredGoalPressure"), 9.0},
        {QStringLiteral("filteredGoalFlow"), 2.0},
        {QStringLiteral("standbySwitchOpen"), false},
        {QStringLiteral("steamHeaterOn"), true},
        {QStringLiteral("steamSoftStopped"), true},
        {QStringLiteral("hardwareSteamTwoTapStop"), true},
        {QStringLiteral("steamAutoFlushCountdown"), 3.7},
        {QStringLiteral("temperatureIsRealOverride"), true},
        {QStringLiteral("brewHasTemperatureOverride"), true},
        {QStringLiteral("brewTemperatureOverride"), 92.5},
        {QStringLiteral("brewSteamTemperature"), 131.0},
        {QStringLiteral("brewSteamTimeout"), 30},
        {QStringLiteral("brewSteamFlow"), 150},
        {QStringLiteral("brewSteamAutoFlushSeconds"), 4},
        {QStringLiteral("brewMilkAutoCaptureEnabled"), true},
        {QStringLiteral("brewSteamSecondsPerGram"), 0.15},
        {QStringLiteral("brewLastSteamMilkG"), 200.0},
        {QStringLiteral("brewLastSteamTimeS"), 30.0},
        {QStringLiteral("brewDoseCupTareWeight"), 12.75},
        {QStringLiteral("appWaterLevelDisplayUnit"), QStringLiteral("ml")},
        {QStringLiteral("appWaterRefillPoint"), 6},
        {QStringLiteral("appRefillKitOverride"), 2},
        {QStringLiteral("profileTargetTemperature"), 94.0},
        {QStringLiteral("profileTargetWeight"), 38.5},
        {QStringLiteral("profileEffectiveTargetWeight"), 40.0},
        {QStringLiteral("profileBrewByRatio"), 2.1},
        {QStringLiteral("profileBrewByRatioDose"), 18.2},
        {QStringLiteral("profileBrewByRatioActive"), true},
        {QStringLiteral("activeBaselineYieldG"), 40.0},
        {QStringLiteral("activeRecipeTempOffsetC"), 1.5},
        {QStringLiteral("yieldIsRealOverride"), false},
        {QStringLiteral("portalState"), QStringLiteral("streaming")},
        {QStringLiteral("portalActive"), true},
        {QStringLiteral("portalHasReading"), true},
        {QStringLiteral("portalTemperatureC"), 23.4},
        {QStringLiteral("portalEcRaw"), 1.234},
        {QStringLiteral("portalOwned"), true},
        {QStringLiteral("portalMachineBusy"), false},
        {QStringLiteral("scaleConnected"), true},
        {QStringLiteral("scaleIsFlowScale"), false},
        {QStringLiteral("scaleName"), QStringLiteral("Decent Scale")},
        {QStringLiteral("scaleBatteryLevel"), 67},
        {QStringLiteral("scaleCharging"), true},
        {QStringLiteral("bleScaleConnectionFailed"), false},
        {QStringLiteral("usbScaleConnected"), true},
        {QStringLiteral("primaryScaleAddress"), QStringLiteral("AA:BB:CC:DD:EE:FF")},
        {QStringLiteral("controlContextId"), QString()}
    };
}

bool parse(const QJsonObject& object, MachineUiSnapshot* result)
{
    return MachineUiSnapshot::parse(object, result);
}

struct FieldTypeCase {
    const char* key;
    QJsonValue wrongValue;
};
} // namespace

class tst_MachineUiSnapshot : public QObject
{
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }

    void qmlPhaseMetaobjectsExposeTheSameNamesAndValues()
    {
        const QMetaEnum nativePhase = QMetaEnum::fromType<MachineState::Phase>();
        const QMetaEnum remote = QMetaEnum::fromType<RemoteMachineState::Phase>();
        QVERIFY(nativePhase.isValid());
        QVERIFY(remote.isValid());
        QVERIFY(RemoteMachineState::staticMetaObject.indexOfSignal("scaleWeightChanged()") >= 0);
        QCOMPARE(nativePhase.keyCount(), 16);
        QCOMPARE(remote.keyCount(), nativePhase.keyCount());
        for (int index = 0; index < nativePhase.keyCount(); ++index) {
            QCOMPARE(QByteArray(remote.key(index)), QByteArray(nativePhase.key(index)));
            QCOMPARE(remote.value(index), nativePhase.value(index));
        }
    }

    void validSnapshotRoundTripsEveryField()
    {
        MachineUiSnapshot source;
        source.de1Connected = true;
        source.simulationMode = true;
        source.brewSteamTimeout = 30;
        source.brewSteamFlow = 150;
        source.brewSteamAutoFlushSeconds = 4;
        source.brewMilkAutoCaptureEnabled = true;
        source.brewSteamSecondsPerGram = 0.15;
        source.brewLastSteamMilkG = 200.0;
        source.brewLastSteamTimeS = 30.0;
        source.de1IsHeadless = true;
        source.de1State = 5;
        source.de1SubState = 20;
        source.stateString = QStringLiteral("Brew");
        source.subStateString = QStringLiteral("Steam");
        source.temperature = 91.125;
        source.steamTemperature = 126.75;
        source.waterLevel = 0.625;
        source.waterLevelMm = 123.5;
        source.waterLevelMl = 734;
        source.refillKitDetected = -1;
        source.phase = MachinePhase::Steaming;
        source.isFlowing = false;
        source.isHeating = true;
        source.isReady = false;
        source.scaleWeight = 47.25;
        source.scaleFlowRate = 0.625;
        source.cumulativeVolume = 31.5;
        source.preinfusionVolume = 8.25;
        source.pourVolume = 23.25;
        source.shotTime = 31.75;
        source.targetWeight = 42.0;
        source.targetVolume = 100.0;
        source.smoothedScaleFlowRate = 2.3;
        source.canDecreaseTargetWeight = true;
        source.hotWaterTargetWeight = 250.0;
        source.filteredGoalPressure = 8.5;
        source.filteredGoalFlow = 1.75;
        source.standbySwitchOpen = true;
        source.steamHeaterOn = false;
        source.steamSoftStopped = true;
        source.hardwareSteamTwoTapStop = true;
        source.steamAutoFlushCountdown = 2.75;
        source.temperatureIsRealOverride = true;
        source.de1GuiEnabled = true;
        source.scaleName = QStringLiteral("Decent Scale");
        source.scaleBatteryLevel = 67;
        source.scaleCharging = true;
        source.brewHasTemperatureOverride = true;
        source.brewTemperatureOverride = 92.5;
        source.brewSteamTemperature = 131.0;
        source.brewDoseCupTareWeight = 12.75;
        source.appWaterLevelDisplayUnit = QStringLiteral("ml");
        source.appWaterRefillPoint = 6;
        source.appRefillKitOverride = 2;
        source.profileTargetTemperature = 94.0;
        source.profileTargetWeight = 38.5;
        source.profileEffectiveTargetWeight = 40.0;
        source.profileBrewByRatio = 2.1;
        source.profileBrewByRatioDose = 18.2;
        source.profileBrewByRatioActive = true;
        source.activeBaselineYieldG = 40.0;
        source.activeRecipeTempOffsetC = 1.5;
        source.yieldIsRealOverride = true;
        source.portalState = QStringLiteral("streaming");
        source.portalActive = true;
        source.portalHasReading = true;
        source.portalTemperatureC = 23.4;
        source.portalEcRaw = 1.234;
        source.portalOwned = true;
        source.portalMachineBusy = false;
        source.scaleConnected = true;
        source.scaleIsFlowScale = false;
        source.bleScaleConnectionFailed = false;
        source.usbScaleConnected = true;
        source.primaryScaleAddress = QStringLiteral("AA:BB:CC:DD:EE:FF");
        source.controlContextId = QUuid::createUuid().toString(QUuid::WithoutBraces);

        const QJsonObject wire = source.toJson();
        QCOMPARE(wire.size(), validObject().size());

        MachineUiSnapshot parsed;
        QVERIFY(parse(wire, &parsed));
        QCOMPARE(parsed.toJson(), wire);
        QCOMPARE(parsed.de1Connected, source.de1Connected);
        QCOMPARE(parsed.simulationMode, source.simulationMode);
        QCOMPARE(parsed.stateString, source.stateString);
        QCOMPARE(parsed.subStateString, source.subStateString);
        QCOMPARE(parsed.temperature, source.temperature);
        QCOMPARE(parsed.steamTemperature, source.steamTemperature);
        QCOMPARE(parsed.waterLevel, source.waterLevel);
        QCOMPARE(parsed.waterLevelMm, source.waterLevelMm);
        QCOMPARE(parsed.waterLevelMl, source.waterLevelMl);
        QCOMPARE(parsed.refillKitDetected, source.refillKitDetected);
        QCOMPARE(static_cast<int>(parsed.phase), static_cast<int>(source.phase));
        QCOMPARE(parsed.isFlowing, source.isFlowing);
        QCOMPARE(parsed.isHeating, source.isHeating);
        QCOMPARE(parsed.isReady, source.isReady);
        QCOMPARE(parsed.scaleWeight, source.scaleWeight);
        QCOMPARE(parsed.scaleFlowRate, source.scaleFlowRate);
        QCOMPARE(parsed.cumulativeVolume, source.cumulativeVolume);
        QCOMPARE(parsed.preinfusionVolume, source.preinfusionVolume);
        QCOMPARE(parsed.pourVolume, source.pourVolume);
        QCOMPARE(parsed.shotTime, source.shotTime);
        QCOMPARE(parsed.targetWeight, source.targetWeight);
        QCOMPARE(parsed.targetVolume, source.targetVolume);
        QCOMPARE(parsed.smoothedScaleFlowRate, source.smoothedScaleFlowRate);
        QCOMPARE(parsed.canDecreaseTargetWeight, source.canDecreaseTargetWeight);
        QCOMPARE(parsed.hotWaterTargetWeight, source.hotWaterTargetWeight);
        QCOMPARE(parsed.filteredGoalPressure, source.filteredGoalPressure);
        QCOMPARE(parsed.filteredGoalFlow, source.filteredGoalFlow);
        QCOMPARE(parsed.standbySwitchOpen, source.standbySwitchOpen);
        QCOMPARE(parsed.steamHeaterOn, source.steamHeaterOn);
        QCOMPARE(parsed.steamSoftStopped, source.steamSoftStopped);
        QCOMPARE(parsed.hardwareSteamTwoTapStop, source.hardwareSteamTwoTapStop);
        QCOMPARE(parsed.steamAutoFlushCountdown, source.steamAutoFlushCountdown);
        QCOMPARE(parsed.temperatureIsRealOverride, source.temperatureIsRealOverride);
        QCOMPARE(parsed.de1GuiEnabled, source.de1GuiEnabled);
        QCOMPARE(parsed.scaleName, source.scaleName);
        QCOMPARE(parsed.scaleBatteryLevel, source.scaleBatteryLevel);
        QCOMPARE(parsed.scaleCharging, source.scaleCharging);
        QCOMPARE(parsed.brewHasTemperatureOverride, source.brewHasTemperatureOverride);
        QCOMPARE(parsed.brewTemperatureOverride, source.brewTemperatureOverride);
        QCOMPARE(parsed.brewSteamTemperature, source.brewSteamTemperature);
        QCOMPARE(parsed.brewDoseCupTareWeight, source.brewDoseCupTareWeight);
        QCOMPARE(parsed.appWaterLevelDisplayUnit, source.appWaterLevelDisplayUnit);
        QCOMPARE(parsed.appWaterRefillPoint, source.appWaterRefillPoint);
        QCOMPARE(parsed.appRefillKitOverride, source.appRefillKitOverride);
        QCOMPARE(parsed.profileTargetTemperature, source.profileTargetTemperature);
        QCOMPARE(parsed.profileTargetWeight, source.profileTargetWeight);
        QCOMPARE(parsed.profileEffectiveTargetWeight, source.profileEffectiveTargetWeight);
        QCOMPARE(parsed.profileBrewByRatio, source.profileBrewByRatio);
        QCOMPARE(parsed.profileBrewByRatioDose, source.profileBrewByRatioDose);
        QCOMPARE(parsed.profileBrewByRatioActive, source.profileBrewByRatioActive);
        QCOMPARE(parsed.activeBaselineYieldG, source.activeBaselineYieldG);
        QCOMPARE(parsed.activeRecipeTempOffsetC, source.activeRecipeTempOffsetC);
        QCOMPARE(parsed.yieldIsRealOverride, source.yieldIsRealOverride);
        QCOMPARE(parsed.portalState, source.portalState);
        QCOMPARE(parsed.portalActive, source.portalActive);
        QCOMPARE(parsed.portalHasReading, source.portalHasReading);
        QCOMPARE(parsed.portalTemperatureC, source.portalTemperatureC);
        QCOMPARE(parsed.portalEcRaw, source.portalEcRaw);
        QCOMPARE(parsed.portalOwned, source.portalOwned);
        QCOMPARE(parsed.portalMachineBusy, source.portalMachineBusy);
        QCOMPARE(parsed.scaleConnected, source.scaleConnected);
        QCOMPARE(parsed.scaleIsFlowScale, source.scaleIsFlowScale);
        QCOMPARE(parsed.bleScaleConnectionFailed, source.bleScaleConnectionFailed);
        QCOMPARE(parsed.usbScaleConnected, source.usbScaleConnected);
        QCOMPARE(parsed.primaryScaleAddress, source.primaryScaleAddress);
        QCOMPARE(parsed.controlContextId, source.controlContextId);
    }

    void defaultSnapshotUsesDisconnectedPortalState()
    {
        const MachineUiSnapshot source;
        QCOMPARE(source.portalState, QStringLiteral("disconnected"));
        MachineUiSnapshot parsed;
        QVERIFY(parse(source.toJson(), &parsed));
        QCOMPARE(parsed.portalState, QStringLiteral("disconnected"));
    }

    void everyRequiredFieldRejectsMissingAndWrongType()
    {
        const FieldTypeCase fields[] = {
            {"de1Connected", QStringLiteral("true")},
            {"de1GuiEnabled", 1},
            {"simulationMode", 1},
            {"brewSteamTimeout", false},
            {"brewSteamFlow", false},
            {"brewSteamAutoFlushSeconds", false},
            {"brewMilkAutoCaptureEnabled", 1},
            {"brewSteamSecondsPerGram", false},
            {"brewLastSteamMilkG", false},
            {"brewLastSteamTimeS", false},
            {"de1IsHeadless", 1},
            {"de1State", QStringLiteral("5")},
            {"de1SubState", false},
            {"stateString", false},
            {"subStateString", 1},
            {"temperature", QStringLiteral("93.2")},
            {"steamTemperature", true},
            {"waterLevel", QStringLiteral("0.5")},
            {"waterLevelMm", false},
            {"waterLevelMl", QStringLiteral("812")},
            {"refillKitDetected", QStringLiteral("2")},
            {"phase", 4},
            {"isFlowing", QStringLiteral("false")},
            {"isHeating", 0},
            {"isReady", QStringLiteral("true")},
            {"scaleWeight", false},
            {"scaleFlowRate", QStringLiteral("1.2")},
            {"cumulativeVolume", false},
            {"preinfusionVolume", QStringLiteral("8.4")},
            {"pourVolume", false},
            {"shotTime", true},
            {"targetWeight", QStringLiteral("36")},
            {"targetVolume", false},
            {"smoothedScaleFlowRate", QStringLiteral("1.1")},
            {"canDecreaseTargetWeight", 1},
            {"hotWaterTargetWeight", false},
            {"filteredGoalPressure", QStringLiteral("9")},
            {"filteredGoalFlow", false},
            {"standbySwitchOpen", 0},
            {"steamHeaterOn", QStringLiteral("false")},
            {"steamSoftStopped", QStringLiteral("false")},
            {"hardwareSteamTwoTapStop", QStringLiteral("false")},
            {"steamAutoFlushCountdown", QStringLiteral("3.7")},
            {"temperatureIsRealOverride", 1},
            {"brewHasTemperatureOverride", 1},
            {"brewTemperatureOverride", QStringLiteral("92.5")},
            {"brewSteamTemperature", false},
            {"brewDoseCupTareWeight", QStringLiteral("12.75")},
            {"appWaterLevelDisplayUnit", 1},
            {"appWaterRefillPoint", QStringLiteral("6")},
            {"appRefillKitOverride", false},
            {"profileTargetTemperature", QStringLiteral("94")},
            {"profileTargetWeight", false},
            {"profileEffectiveTargetWeight", false},
            {"profileBrewByRatio", QStringLiteral("2.1")},
            {"profileBrewByRatioDose", false},
            {"profileBrewByRatioActive", 1},
            {"activeBaselineYieldG", QStringLiteral("40")},
            {"activeRecipeTempOffsetC", false},
            {"yieldIsRealOverride", 1},
            {"portalState", false},
            {"portalActive", QStringLiteral("true")},
            {"portalHasReading", 1},
            {"portalEcRaw", QStringLiteral("1.234")},
            {"portalOwned", QStringLiteral("true")},
            {"portalMachineBusy", 0},
            {"scaleConnected", 1},
            {"scaleIsFlowScale", QStringLiteral("false")},
            {"scaleName", false},
            {"scaleBatteryLevel", QStringLiteral("67")},
            {"scaleCharging", 0},
            {"bleScaleConnectionFailed", 0},
            {"usbScaleConnected", QStringLiteral("true")},
            {"primaryScaleAddress", false},
            {"controlContextId", false}
        };

        for (const FieldTypeCase& field : fields) {
            QJsonObject object = validObject();
            object.remove(QLatin1String(field.key));
            MachineUiSnapshot parsed;
            QVERIFY2(!parse(object, &parsed), field.key);

            object = validObject();
            object.insert(QLatin1String(field.key), field.wrongValue);
            QVERIFY2(!parse(object, &parsed), field.key);
        }
    }

    void rejectsNullAndNonFiniteNumericValues()
    {
        const char* doubleFields[] = {
            "temperature", "steamTemperature", "waterLevel", "waterLevelMm",
            "scaleWeight", "scaleFlowRate", "shotTime", "targetWeight",
            "targetVolume", "smoothedScaleFlowRate", "filteredGoalPressure", "filteredGoalFlow",
            "cumulativeVolume", "preinfusionVolume", "pourVolume",
            "steamAutoFlushCountdown",
            "brewTemperatureOverride", "brewSteamTemperature", "brewDoseCupTareWeight",
            "brewSteamSecondsPerGram", "brewLastSteamMilkG", "brewLastSteamTimeS",
            "profileTargetTemperature", "profileTargetWeight", "profileEffectiveTargetWeight", "profileBrewByRatio",
            "profileBrewByRatioDose", "activeBaselineYieldG", "activeRecipeTempOffsetC", "portalEcRaw"
        };
        for (const char* field : doubleFields) {
            QJsonObject object = validObject();
            object.insert(QLatin1String(field), QJsonValue(QJsonValue::Null));
            MachineUiSnapshot parsed;
            QVERIFY2(!parse(object, &parsed), field);

            const double nonFinite[] = {
                std::numeric_limits<double>::infinity(),
                -std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::quiet_NaN()
            };
            for (double value : nonFinite) {
                object = validObject();
                object.insert(QLatin1String(field), value);
                QVERIFY2(!parse(object, &parsed), field);
            }
        }

        const char* intFields[] = {
            "waterLevelMl", "refillKitDetected", "appWaterRefillPoint", "appRefillKitOverride",
            "brewSteamTimeout", "brewSteamFlow", "brewSteamAutoFlushSeconds",
            "de1State", "de1SubState", "scaleBatteryLevel"
        };
        for (const char* field : intFields) {
            QJsonObject object = validObject();
            object.insert(QLatin1String(field), QJsonValue(QJsonValue::Null));
            MachineUiSnapshot parsed;
            QVERIFY2(!parse(object, &parsed), field);

            object = validObject();
            object.insert(QLatin1String(field),
                          static_cast<double>(std::numeric_limits<int>::max()) + 1.0);
            QVERIFY2(!parse(object, &parsed), field);
        }
    }

    void rejectsUnknownAndFractionalEnums()
    {
        MachineUiSnapshot parsed;
        QJsonObject object = validObject();
        object.insert(QStringLiteral("phase"), QStringLiteral("MakingCoffee"));
        QVERIFY(!parse(object, &parsed));

        object = validObject();
        object.insert(QStringLiteral("phase"), 2.5);
        QVERIFY(!parse(object, &parsed));

        object = validObject();
        object.insert(QStringLiteral("portalState"), QStringLiteral("connecting-ish"));
        QVERIFY(!parse(object, &parsed));

        object = validObject();
        object.insert(QStringLiteral("refillKitDetected"), 1.5);
        QVERIFY(!parse(object, &parsed));

        for (int invalidValue : {-2, 2}) {
            object = validObject();
            object.insert(QStringLiteral("refillKitDetected"), invalidValue);
            QVERIFY(!parse(object, &parsed));
        }

        object = validObject();
        object.insert(QStringLiteral("appWaterLevelDisplayUnit"), QStringLiteral("gallons"));
        QVERIFY(!parse(object, &parsed));

        for (int invalidValue : {-1, 3}) {
            object = validObject();
            object.insert(QStringLiteral("appRefillKitOverride"), invalidValue);
            QVERIFY(!parse(object, &parsed));
        }

        for (int invalidValue : {-2, 101}) {
            object = validObject();
            object.insert(QStringLiteral("scaleBatteryLevel"), invalidValue);
            QVERIFY(!parse(object, &parsed));
        }

        object = validObject();
        object.insert(QStringLiteral("steamAutoFlushCountdown"), -0.1);
        QVERIFY(!parse(object, &parsed));
    }

    void deviceStateCodesPreserveFirmwareByteRange()
    {
        for (const char* field : {"de1State", "de1SubState"}) {
            for (int value : {-1, 0, 20, 217, 255, 256}) {
                auto object = validObject();
                object.insert(QLatin1String(field), value);
                MachineUiSnapshot parsed;
                QCOMPARE(parse(object, &parsed), value >= 0 && value <= 255);
            }
        }
    }

    void parseFailureLeavesExistingOutputUnchanged()
    {
        MachineUiSnapshot prior;
        prior.de1Connected = true;
        prior.stateString = QStringLiteral("Keep this snapshot");
        prior.temperature = 88.5;
        prior.phase = MachinePhase::Ready;
        const QJsonObject before = prior.toJson();

        QJsonObject invalid = validObject();
        invalid.remove(QStringLiteral("phase"));
        QString error;
        QVERIFY(!MachineUiSnapshot::parse(invalid, &prior, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(prior.toJson(), before);
    }

    void controlContextAllowsUnavailableOrCanonicalUuidOnly()
    {
        MachineUiSnapshot parsed;
        QJsonObject object = validObject();
        object.insert(QStringLiteral("controlContextId"), QString());
        QVERIFY(parse(object, &parsed));

        const QString validId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        object.insert(QStringLiteral("controlContextId"), validId);
        QVERIFY(parse(object, &parsed));
        QCOMPARE(parsed.controlContextId, validId);

        for (const QString& invalidId : {
                 QStringLiteral("not-a-uuid"),
                 QStringLiteral("{123e4567-e89b-12d3-a456-426614174000}"),
                 QStringLiteral("123E4567-E89B-12D3-A456-426614174000")}) {
            object.insert(QStringLiteral("controlContextId"), invalidId);
            QVERIFY(!parse(object, &parsed));
        }
    }
};

QTEST_GUILESS_MAIN(tst_MachineUiSnapshot)
#include "tst_machineuisnapshot.moc"
