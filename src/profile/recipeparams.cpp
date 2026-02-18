#include "recipeparams.h"

// Shared legacy migration for old pourStyle/flowLimit/pressureLimit fields
static void migratePourStyle(RecipeParams& params, const QString& oldStyle,
                             double pourPressure, double pourFlow,
                             double flowLimit, bool hasFlowLimit,
                             double pressureLimit, bool hasPressureLimit)
{
    if (!oldStyle.isEmpty()) {
        if (oldStyle == "pressure") {
            params.pourPressure = pourPressure;
            params.pourFlow = (hasFlowLimit && flowLimit > 0) ? flowLimit : pourFlow;
        } else {
            params.pourFlow = pourFlow;
            params.pourPressure = hasPressureLimit ? pressureLimit : pourPressure;
        }
    } else {
        params.pourPressure = pourPressure;
        params.pourFlow = pourFlow;
    }
}

QStringList RecipeParams::validate() const {
    QStringList issues;

    // Physical range bounds (DE1 hardware limits)
    if (targetWeight < 0 || targetWeight > 500)
        issues << "targetWeight out of range [0, 500]";
    if (targetVolume < 0 || targetVolume > 500)
        issues << "targetVolume out of range [0, 500]";
    if (dose < 0 || dose > 100)
        issues << "dose out of range [0, 100]";

    // Temperature bounds
    auto checkTemp = [&](double temp, const char* name) {
        if (temp < 0 || temp > 110)
            issues << QString("%1 out of range [0, 110]: %2").arg(name).arg(temp);
    };
    checkTemp(fillTemperature, "fillTemperature");
    checkTemp(pourTemperature, "pourTemperature");
    checkTemp(tempStart, "tempStart");
    checkTemp(tempPreinfuse, "tempPreinfuse");
    checkTemp(tempHold, "tempHold");
    checkTemp(tempDecline, "tempDecline");

    // Pressure bounds (0-12 bar)
    auto checkPressure = [&](double p, const char* name) {
        if (p < 0 || p > 12)
            issues << QString("%1 out of range [0, 12]: %2").arg(name).arg(p);
    };
    checkPressure(fillPressure, "fillPressure");
    checkPressure(fillExitPressure, "fillExitPressure");
    checkPressure(infusePressure, "infusePressure");
    checkPressure(pourPressure, "pourPressure");
    checkPressure(espressoPressure, "espressoPressure");
    checkPressure(pressureEnd, "pressureEnd");

    // Flow bounds (0-10 mL/s)
    auto checkFlow = [&](double f, const char* name) {
        if (f < 0 || f > 10)
            issues << QString("%1 out of range [0, 10]: %2").arg(name).arg(f);
    };
    checkFlow(fillFlow, "fillFlow");
    checkFlow(pourFlow, "pourFlow");
    checkFlow(holdFlow, "holdFlow");
    checkFlow(flowEnd, "flowEnd");
    checkFlow(preinfusionFlowRate, "preinfusionFlowRate");
    checkFlow(declineTo, "declineTo");

    // Time bounds (non-negative)
    if (fillTimeout < 0) issues << "fillTimeout is negative";
    if (infuseTime < 0) issues << "infuseTime is negative";
    if (bloomTime < 0) issues << "bloomTime is negative";
    if (rampTime < 0) issues << "rampTime is negative";
    if (declineTime < 0) issues << "declineTime is negative";
    if (preinfusionTime < 0) issues << "preinfusionTime is negative";
    if (holdTime < 0) issues << "holdTime is negative";
    if (simpleDeclineTime < 0) issues << "simpleDeclineTime is negative";

    // Weight bounds
    if (infuseWeight < 0) issues << "infuseWeight is negative";

    // Limiter bounds
    if (limiterValue < 0 || limiterValue > 12)
        issues << "limiterValue out of range [0, 12]";
    if (limiterRange < 0 || limiterRange > 10)
        issues << "limiterRange out of range [0, 10]";

    return issues;
}

void RecipeParams::clamp() {
    auto clampVal = [](double& v, double lo, double hi) { v = qBound(lo, v, hi); };

    clampVal(targetWeight, 0.0, 500.0);
    clampVal(targetVolume, 0.0, 500.0);
    clampVal(dose, 0.0, 100.0);

    // Temperatures (0-110)
    for (double* t : {&fillTemperature, &pourTemperature, &tempStart, &tempPreinfuse, &tempHold, &tempDecline})
        clampVal(*t, 0.0, 110.0);

    // Pressures (0-12)
    for (double* p : {&fillPressure, &fillExitPressure, &infusePressure, &pourPressure, &espressoPressure, &pressureEnd})
        clampVal(*p, 0.0, 12.0);

    // Flows (0-10)
    for (double* f : {&fillFlow, &pourFlow, &holdFlow, &flowEnd, &preinfusionFlowRate, &declineTo})
        clampVal(*f, 0.0, 10.0);

    // Times (non-negative)
    for (double* t : {&fillTimeout, &infuseTime, &bloomTime, &rampTime, &declineTime, &preinfusionTime, &holdTime, &simpleDeclineTime})
        if (*t < 0) *t = 0;

    if (infuseWeight < 0) infuseWeight = 0;
    clampVal(limiterValue, 0.0, 12.0);
    clampVal(limiterRange, 0.0, 10.0);
}

QJsonObject RecipeParams::toJson() const {
    QJsonObject obj;

    // Core
    obj["targetWeight"] = targetWeight;
    obj["targetVolume"] = targetVolume;
    obj["dose"] = dose;

    // Fill
    obj["fillTemperature"] = fillTemperature;
    obj["fillPressure"] = fillPressure;
    obj["fillFlow"] = fillFlow;
    obj["fillTimeout"] = fillTimeout;
    obj["fillExitPressure"] = fillExitPressure;

    // Infuse
    obj["infuseEnabled"] = infuseEnabled;
    obj["infusePressure"] = infusePressure;
    obj["infuseTime"] = infuseTime;
    obj["infuseByWeight"] = infuseByWeight;
    obj["infuseWeight"] = infuseWeight;
    obj["infuseVolume"] = infuseVolume;
    obj["bloomEnabled"] = bloomEnabled;
    obj["bloomTime"] = bloomTime;

    // Pour (always flow-driven with pressure limit)
    obj["pourTemperature"] = pourTemperature;
    obj["pourPressure"] = pourPressure;
    obj["pourFlow"] = pourFlow;
    obj["rampEnabled"] = rampEnabled;
    obj["rampTime"] = rampTime;

    // Decline (D-Flow only)
    obj["declineEnabled"] = declineEnabled;
    obj["declineTo"] = declineTo;
    obj["declineTime"] = declineTime;

    // A-Flow specific
    obj["rampDownEnabled"] = rampDownEnabled;
    obj["flowExtractionUp"] = flowExtractionUp;

    // Simple profile parameters (pressure/flow editors)
    obj["preinfusionTime"] = preinfusionTime;
    obj["preinfusionFlowRate"] = preinfusionFlowRate;
    obj["preinfusionStopPressure"] = preinfusionStopPressure;
    obj["holdTime"] = holdTime;
    obj["espressoPressure"] = espressoPressure;
    obj["holdFlow"] = holdFlow;
    obj["simpleDeclineTime"] = simpleDeclineTime;
    obj["pressureEnd"] = pressureEnd;
    obj["flowEnd"] = flowEnd;
    obj["limiterValue"] = limiterValue;
    obj["limiterRange"] = limiterRange;

    // Per-step temperatures
    obj["tempStart"] = tempStart;
    obj["tempPreinfuse"] = tempPreinfuse;
    obj["tempHold"] = tempHold;
    obj["tempDecline"] = tempDecline;

    // Editor type
    obj["editorType"] = editorTypeToString(editorType);

    return obj;
}

RecipeParams RecipeParams::fromJson(const QJsonObject& json) {
    RecipeParams params;

    // Core
    params.targetWeight = json["targetWeight"].toDouble(36.0);
    params.targetVolume = json["targetVolume"].toDouble(0.0);
    params.dose = json["dose"].toDouble(18.0);

    // Fill
    params.fillTemperature = json["fillTemperature"].toDouble(88.0);
    // Legacy support: use "temperature" if "fillTemperature" not present
    if (!json.contains("fillTemperature") && json.contains("temperature")) {
        params.fillTemperature = json["temperature"].toDouble(88.0);
    }
    params.fillPressure = json["fillPressure"].toDouble(3.0);
    params.fillFlow = json["fillFlow"].toDouble(8.0);
    params.fillTimeout = json["fillTimeout"].toDouble(25.0);
    params.fillExitPressure = json["fillExitPressure"].toDouble(3.0);

    // Infuse
    params.infuseEnabled = json["infuseEnabled"].toBool(true);  // Default true for legacy
    params.infusePressure = json["infusePressure"].toDouble(3.0);
    params.infuseTime = json["infuseTime"].toDouble(20.0);
    params.infuseByWeight = json["infuseByWeight"].toBool(false);
    params.infuseWeight = json["infuseWeight"].toDouble(4.0);
    params.infuseVolume = json["infuseVolume"].toDouble(100.0);
    params.bloomEnabled = json["bloomEnabled"].toBool(false);
    params.bloomTime = json["bloomTime"].toDouble(10.0);

    // Pour
    params.pourTemperature = json["pourTemperature"].toDouble(93.0);
    // Legacy support: use "temperature" if "pourTemperature" not present
    if (!json.contains("pourTemperature") && json.contains("temperature")) {
        params.pourTemperature = json["temperature"].toDouble(93.0);
    }

    // Backward compatibility: migrate old pourStyle/flowLimit/pressureLimit fields
    migratePourStyle(params,
        json["pourStyle"].toString(""),
        json["pourPressure"].toDouble(9.0),
        json["pourFlow"].toDouble(2.0),
        json["flowLimit"].toDouble(0.0),
        json.contains("flowLimit"),
        json["pressureLimit"].toDouble(6.0),
        json.contains("pressureLimit"));

    params.rampEnabled = json["rampEnabled"].toBool(true);  // Default true for legacy
    params.rampTime = json["rampTime"].toDouble(5.0);

    // Decline
    params.declineEnabled = json["declineEnabled"].toBool(false);
    params.declineTo = json["declineTo"].toDouble(1.0);
    params.declineTime = json["declineTime"].toDouble(30.0);

    // Migration: old profiles stored declineTo in bar (pressure). New model uses mL/s (flow).
    // Convert using same formula as RecipeAnalyzer::forceConvertToRecipe().
    if (!json["pourStyle"].toString("").isEmpty() && params.declineEnabled) {
        params.declineTo = params.pourFlow * 0.5;
    }

    // A-Flow specific
    params.rampDownEnabled = json["rampDownEnabled"].toBool(false);
    params.flowExtractionUp = json["flowExtractionUp"].toBool(true);

    // Simple profile parameters
    params.preinfusionTime = json["preinfusionTime"].toDouble(20.0);
    params.preinfusionFlowRate = json["preinfusionFlowRate"].toDouble(8.0);
    params.preinfusionStopPressure = json["preinfusionStopPressure"].toDouble(4.0);
    params.holdTime = json["holdTime"].toDouble(10.0);
    params.espressoPressure = json["espressoPressure"].toDouble(8.4);
    params.holdFlow = json["holdFlow"].toDouble(2.2);
    params.simpleDeclineTime = json["simpleDeclineTime"].toDouble(30.0);
    params.pressureEnd = json["pressureEnd"].toDouble(6.0);
    params.flowEnd = json["flowEnd"].toDouble(1.8);
    params.limiterValue = json["limiterValue"].toDouble(3.5);
    params.limiterRange = json["limiterRange"].toDouble(1.0);

    // Per-step temperatures
    params.tempStart = json["tempStart"].toDouble(json["pourTemperature"].toDouble(90.0));
    params.tempPreinfuse = json["tempPreinfuse"].toDouble(json["pourTemperature"].toDouble(90.0));
    params.tempHold = json["tempHold"].toDouble(json["pourTemperature"].toDouble(90.0));
    params.tempDecline = json["tempDecline"].toDouble(json["pourTemperature"].toDouble(90.0));

    // Editor type
    params.editorType = editorTypeFromString(json["editorType"].toString("dflow"));

    return params;
}

QVariantMap RecipeParams::toVariantMap() const {
    QVariantMap map;

    // Core
    map["targetWeight"] = targetWeight;
    map["targetVolume"] = targetVolume;
    map["dose"] = dose;

    // Fill
    map["fillTemperature"] = fillTemperature;
    map["fillPressure"] = fillPressure;
    map["fillFlow"] = fillFlow;
    map["fillTimeout"] = fillTimeout;
    map["fillExitPressure"] = fillExitPressure;

    // Infuse
    map["infuseEnabled"] = infuseEnabled;
    map["infusePressure"] = infusePressure;
    map["infuseTime"] = infuseTime;
    map["infuseByWeight"] = infuseByWeight;
    map["infuseWeight"] = infuseWeight;
    map["infuseVolume"] = infuseVolume;
    map["bloomEnabled"] = bloomEnabled;
    map["bloomTime"] = bloomTime;

    // Pour (always flow-driven with pressure limit)
    map["pourTemperature"] = pourTemperature;
    map["pourPressure"] = pourPressure;
    map["pourFlow"] = pourFlow;
    map["rampEnabled"] = rampEnabled;
    map["rampTime"] = rampTime;

    // Decline
    map["declineEnabled"] = declineEnabled;
    map["declineTo"] = declineTo;
    map["declineTime"] = declineTime;

    // A-Flow specific
    map["rampDownEnabled"] = rampDownEnabled;
    map["flowExtractionUp"] = flowExtractionUp;

    // Simple profile parameters
    map["preinfusionTime"] = preinfusionTime;
    map["preinfusionFlowRate"] = preinfusionFlowRate;
    map["preinfusionStopPressure"] = preinfusionStopPressure;
    map["holdTime"] = holdTime;
    map["espressoPressure"] = espressoPressure;
    map["holdFlow"] = holdFlow;
    map["simpleDeclineTime"] = simpleDeclineTime;
    map["pressureEnd"] = pressureEnd;
    map["flowEnd"] = flowEnd;
    map["limiterValue"] = limiterValue;
    map["limiterRange"] = limiterRange;

    // Per-step temperatures
    map["tempStart"] = tempStart;
    map["tempPreinfuse"] = tempPreinfuse;
    map["tempHold"] = tempHold;
    map["tempDecline"] = tempDecline;

    // Editor type
    map["editorType"] = editorTypeToString(editorType);

    return map;
}

RecipeParams RecipeParams::fromVariantMap(const QVariantMap& map) {
    RecipeParams params;

    // Core
    params.targetWeight = map.value("targetWeight", 36.0).toDouble();
    params.targetVolume = map.value("targetVolume", 0.0).toDouble();
    params.dose = map.value("dose", 18.0).toDouble();

    // Fill
    params.fillTemperature = map.value("fillTemperature", 88.0).toDouble();
    // Legacy support
    if (!map.contains("fillTemperature") && map.contains("temperature")) {
        params.fillTemperature = map.value("temperature", 88.0).toDouble();
    }
    params.fillPressure = map.value("fillPressure", 3.0).toDouble();
    params.fillFlow = map.value("fillFlow", 8.0).toDouble();
    params.fillTimeout = map.value("fillTimeout", 25.0).toDouble();
    params.fillExitPressure = map.value("fillExitPressure", 3.0).toDouble();

    // Infuse
    params.infuseEnabled = map.value("infuseEnabled", true).toBool();  // Default true for legacy
    params.infusePressure = map.value("infusePressure", 3.0).toDouble();
    params.infuseTime = map.value("infuseTime", 20.0).toDouble();
    params.infuseByWeight = map.value("infuseByWeight", false).toBool();
    params.infuseWeight = map.value("infuseWeight", 4.0).toDouble();
    params.infuseVolume = map.value("infuseVolume", 100.0).toDouble();
    params.bloomEnabled = map.value("bloomEnabled", false).toBool();
    params.bloomTime = map.value("bloomTime", 10.0).toDouble();

    // Pour
    params.pourTemperature = map.value("pourTemperature", 93.0).toDouble();
    // Legacy support
    if (!map.contains("pourTemperature") && map.contains("temperature")) {
        params.pourTemperature = map.value("temperature", 93.0).toDouble();
    }

    // Backward compatibility: migrate old pourStyle/flowLimit/pressureLimit fields
    migratePourStyle(params,
        map.value("pourStyle", "").toString(),
        map.value("pourPressure", 9.0).toDouble(),
        map.value("pourFlow", 2.0).toDouble(),
        map.value("flowLimit", 0.0).toDouble(),
        map.contains("flowLimit"),
        map.value("pressureLimit", 6.0).toDouble(),
        map.contains("pressureLimit"));

    params.rampEnabled = map.value("rampEnabled", true).toBool();  // Default true for legacy
    params.rampTime = map.value("rampTime", 5.0).toDouble();

    // Decline
    params.declineEnabled = map.value("declineEnabled", false).toBool();
    params.declineTo = map.value("declineTo", 1.0).toDouble();
    params.declineTime = map.value("declineTime", 30.0).toDouble();

    // Migration: old profiles stored declineTo in bar (pressure). New model uses mL/s (flow).
    // Convert using same formula as RecipeAnalyzer::forceConvertToRecipe().
    if (!map.value("pourStyle", "").toString().isEmpty() && params.declineEnabled) {
        params.declineTo = params.pourFlow * 0.5;
    }

    // A-Flow specific
    params.rampDownEnabled = map.value("rampDownEnabled", false).toBool();
    params.flowExtractionUp = map.value("flowExtractionUp", true).toBool();

    // Simple profile parameters
    params.preinfusionTime = map.value("preinfusionTime", 20.0).toDouble();
    params.preinfusionFlowRate = map.value("preinfusionFlowRate", 8.0).toDouble();
    params.preinfusionStopPressure = map.value("preinfusionStopPressure", 4.0).toDouble();
    params.holdTime = map.value("holdTime", 10.0).toDouble();
    params.espressoPressure = map.value("espressoPressure", 8.4).toDouble();
    params.holdFlow = map.value("holdFlow", 2.2).toDouble();
    params.simpleDeclineTime = map.value("simpleDeclineTime", 30.0).toDouble();
    params.pressureEnd = map.value("pressureEnd", 6.0).toDouble();
    params.flowEnd = map.value("flowEnd", 1.8).toDouble();
    params.limiterValue = map.value("limiterValue", 3.5).toDouble();
    params.limiterRange = map.value("limiterRange", 1.0).toDouble();

    // Per-step temperatures
    double defaultTemp = map.value("pourTemperature", 90.0).toDouble();
    params.tempStart = map.value("tempStart", defaultTemp).toDouble();
    params.tempPreinfuse = map.value("tempPreinfuse", defaultTemp).toDouble();
    params.tempHold = map.value("tempHold", defaultTemp).toDouble();
    params.tempDecline = map.value("tempDecline", defaultTemp).toDouble();

    // Editor type
    params.editorType = editorTypeFromString(map.value("editorType", "dflow").toString());

    return params;
}
