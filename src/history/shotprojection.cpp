#include "shotprojection.h"

#include <QMetaType>

QVariantMap ShotProjection::toVariantMap() const
{
    if (!isValid()) return {};

    QVariantMap m;
    m["id"] = id;
    m["uuid"] = uuid;
    m["timestampIso"] = timestampIso;
    m["profileName"] = profileName;
    m["durationSec"] = durationSec;
    m["finalWeightG"] = finalWeightG;
    m["doseWeightG"] = doseWeightG;
    m["beanBrand"] = beanBrand;
    m["beanType"] = beanType;
    m["enjoyment0to100"] = enjoyment0to100;
    m["hasVisualizerUpload"] = hasVisualizerUpload;
    m["beverageType"] = beverageType;
    m["roastDate"] = roastDate;
    m["roastLevel"] = roastLevel;
    m["grinderBrand"] = grinderBrand;
    m["grinderModel"] = grinderModel;
    m["grinderBurrs"] = grinderBurrs;
    m["grinderSetting"] = grinderSetting;
    m["drinkTdsPct"] = drinkTdsPct;
    m["drinkEyPct"] = drinkEyPct;
    m["espressoNotes"] = espressoNotes;
    m["beanNotes"] = beanNotes;
    m["barista"] = barista;
    m["profileNotes"] = profileNotes;
    m["visualizerId"] = visualizerId;
    m["visualizerUrl"] = visualizerUrl;
    m["debugLog"] = debugLog;
    m["temperatureOverrideC"] = temperatureOverrideC;
    m["targetWeightG"] = targetWeightG;
    m["profileJson"] = profileJson;
    m["profileKbId"] = profileKbId;

    m["pressure"] = pressure;
    m["flow"] = flow;
    m["temperature"] = temperature;
    m["temperatureMix"] = temperatureMix;
    m["resistance"] = resistance;
    m["conductance"] = conductance;
    m["darcyResistance"] = darcyResistance;
    m["conductanceDerivative"] = conductanceDerivative;
    m["waterDispensed"] = waterDispensed;
    m["pressureGoal"] = pressureGoal;
    m["flowGoal"] = flowGoal;
    m["temperatureGoal"] = temperatureGoal;
    m["weight"] = weight;
    m["weightFlowRate"] = weightFlowRate;

    m["channelingDetected"] = channelingDetected;
    m["temperatureUnstable"] = temperatureUnstable;
    m["grindIssueDetected"] = grindIssueDetected;
    m["skipFirstFrameDetected"] = skipFirstFrameDetected;
    m["pourTruncatedDetected"] = pourTruncatedDetected;

    m["summaryLines"] = summaryLines;
    m["detectorResults"] = detectorResults;
    if (phaseSummaries.isValid())
        m["phaseSummaries"] = phaseSummaries;
    m["phases"] = phases;
    return m;
}

QJsonObject ShotProjection::toJsonObject() const
{
    return QJsonObject::fromVariantMap(toVariantMap());
}

ShotProjection ShotProjection::fromVariantMap(const QVariantMap& m)
{
    ShotProjection p;
    p.id = m.value("id").toLongLong();
    p.uuid = m.value("uuid").toString();
    p.timestamp = m.value("timestamp").toLongLong();
    p.timestampIso = m.value("timestampIso").toString();
    p.profileName = m.value("profileName").toString();
    p.durationSec = m.value("durationSec").toDouble();
    p.finalWeightG = m.value("finalWeightG").toDouble();
    p.doseWeightG = m.value("doseWeightG").toDouble();
    p.beanBrand = m.value("beanBrand").toString();
    p.beanType = m.value("beanType").toString();
    p.enjoyment0to100 = m.value("enjoyment0to100").toInt();
    p.hasVisualizerUpload = m.value("hasVisualizerUpload").toBool();
    p.beverageType = m.value("beverageType").toString();
    p.roastDate = m.value("roastDate").toString();
    p.roastLevel = m.value("roastLevel").toString();
    p.grinderBrand = m.value("grinderBrand").toString();
    p.grinderModel = m.value("grinderModel").toString();
    p.grinderBurrs = m.value("grinderBurrs").toString();
    p.grinderSetting = m.value("grinderSetting").toString();
    p.drinkTdsPct = m.value("drinkTdsPct").toDouble();
    p.drinkEyPct = m.value("drinkEyPct").toDouble();
    p.espressoNotes = m.value("espressoNotes").toString();
    p.beanNotes = m.value("beanNotes").toString();
    p.barista = m.value("barista").toString();
    p.profileNotes = m.value("profileNotes").toString();
    p.visualizerId = m.value("visualizerId").toString();
    p.visualizerUrl = m.value("visualizerUrl").toString();
    p.debugLog = m.value("debugLog").toString();
    p.temperatureOverrideC = m.value("temperatureOverrideC").toDouble();
    p.targetWeightG = m.value("targetWeightG").toDouble();
    p.profileJson = m.value("profileJson").toString();
    p.profileKbId = m.value("profileKbId").toString();

    p.channelingDetected = m.value("channelingDetected").toBool();
    p.temperatureUnstable = m.value("temperatureUnstable").toBool();
    p.grindIssueDetected = m.value("grindIssueDetected").toBool();
    p.skipFirstFrameDetected = m.value("skipFirstFrameDetected").toBool();
    p.pourTruncatedDetected = m.value("pourTruncatedDetected").toBool();

    p.pressure = m.value("pressure").toList();
    p.flow = m.value("flow").toList();
    p.temperature = m.value("temperature").toList();
    p.temperatureMix = m.value("temperatureMix").toList();
    p.resistance = m.value("resistance").toList();
    p.conductance = m.value("conductance").toList();
    p.darcyResistance = m.value("darcyResistance").toList();
    p.conductanceDerivative = m.value("conductanceDerivative").toList();
    p.waterDispensed = m.value("waterDispensed").toList();
    p.pressureGoal = m.value("pressureGoal").toList();
    p.flowGoal = m.value("flowGoal").toList();
    p.temperatureGoal = m.value("temperatureGoal").toList();
    p.weight = m.value("weight").toList();
    p.weightFlowRate = m.value("weightFlowRate").toList();

    p.summaryLines = m.value("summaryLines").toList();
    p.detectorResults = m.value("detectorResults").toMap();
    p.phaseSummaries = m.value("phaseSummaries");
    p.phases = m.value("phases").toList();
    return p;
}

void ShotProjection::registerMetaTypeConverters()
{
    static bool registered = false;
    if (registered) return;
    registered = true;
    QMetaType::registerConverter<QVariantMap, ShotProjection>(&ShotProjection::fromVariantMap);
}
