#include "recipeparams.h"
#include "profile.h"
#include <QtMath>

bool RecipeParams::frameAffectingFieldsEqual(const RecipeParams& other) const {
    auto eq = [](double a, double b) { return qFuzzyCompare(1.0 + a, 1.0 + b); };
    // Compare all fields that affect frame generation.
    // Excluded: targetWeight, targetVolume (metadata only — don't affect frames).
    return editorType == other.editorType
        // Fill
        && eq(fillTemperature, other.fillTemperature)
        // Infuse
        && eq(infusePressure, other.infusePressure)
        && eq(infuseTime, other.infuseTime)
        && eq(infuseWeight, other.infuseWeight)
        && eq(infuseVolume, other.infuseVolume)
        // Pour
        && eq(pourTemperature, other.pourTemperature)
        && eq(pourPressure, other.pourPressure)
        && eq(pourFlow, other.pourFlow)
        && eq(rampTime, other.rampTime)
        // A-Flow specific
        && rampDownEnabled == other.rampDownEnabled
        && flowExtractionUp == other.flowExtractionUp
        && secondFillEnabled == other.secondFillEnabled
        // Simple profile params
        && eq(preinfusionTime, other.preinfusionTime)
        && eq(preinfusionFlowRate, other.preinfusionFlowRate)
        && eq(preinfusionStopPressure, other.preinfusionStopPressure)
        && eq(holdTime, other.holdTime)
        && eq(espressoPressure, other.espressoPressure)
        && eq(holdFlow, other.holdFlow)
        && eq(simpleDeclineTime, other.simpleDeclineTime)
        && eq(pressureEnd, other.pressureEnd)
        && eq(flowEnd, other.flowEnd)
        && eq(limiterValue, other.limiterValue)
        && eq(limiterRange, other.limiterRange)
        // Per-step temperatures
        && eq(tempStart, other.tempStart)
        && eq(tempPreinfuse, other.tempPreinfuse)
        && eq(tempHold, other.tempHold)
        && eq(tempDecline, other.tempDecline)
        // BLE header
        && preinfuseFrameCount == other.preinfuseFrameCount;
}

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

void RecipeParams::applyEditorDefaults() {
    switch (editorType) {
    case EditorType::DFlow:
        // From D-Flow____default.tcl stock profile (de1app)
        fillTemperature = 88.0;
        infuseTime = 60.0;
        infusePressure = 3.0;
        infuseWeight = 4.0;
        pourTemperature = 88.0;
        pourPressure = 8.5;
        pourFlow = 1.7;
        targetWeight = 50.0;
        preinfuseFrameCount = 2;  // D_Flow/code.tcl line 182
        break;
    case EditorType::AFlow:
        // From A-Flow____default-medium.tcl stock profile (de1app)
        fillTemperature = 95.0;
        infuseTime = 60.0;
        infusePressure = 3.0;
        infuseWeight = 3.6;
        pourTemperature = 95.0;
        pourPressure = 10.0;
        pourFlow = 2.0;
        rampTime = 10.0;
        targetWeight = 36.0;
        preinfuseFrameCount = 2;  // All A-Flow stock profiles use 2
        break;
    case EditorType::Pressure:
    case EditorType::Flow:
        // Simple profiles use struct defaults
        break;
    }
}

QStringList RecipeParams::validate() const {
    QStringList issues;

    // Physical range bounds (DE1 hardware limits)
    if (targetWeight < 0 || targetWeight > 500)
        issues << "targetWeight out of range [0, 500]";
    if (targetVolume < 0 || targetVolume > 500)
        issues << "targetVolume out of range [0, 500]";

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
    checkPressure(infusePressure, "infusePressure");
    checkPressure(pourPressure, "pourPressure");
    checkPressure(espressoPressure, "espressoPressure");
    checkPressure(pressureEnd, "pressureEnd");

    // Flow bounds. Same ceiling as clamp(), because stating it twice is how they drift:
    // widening one and not the other makes every save of a legally-authored high-flow
    // recipe log a false "out of range". clampProducesValuesValidateAccepts is the test
    // that ties them together.
    auto checkFlow = [&](double f, const char* name) {
        if (f < 0 || f > Profile::kMaxSettableFlow)
            issues << QString("%1 out of range [0, %2]: %3")
                          .arg(name).arg(Profile::kMaxSettableFlow).arg(f);
    };
    checkFlow(pourFlow, "pourFlow");
    checkFlow(holdFlow, "holdFlow");
    checkFlow(flowEnd, "flowEnd");
    checkFlow(preinfusionFlowRate, "preinfusionFlowRate");

    // Time bounds (non-negative)
    if (infuseTime < 0) issues << "infuseTime is negative";
    if (rampTime < 0) issues << "rampTime is negative";
    if (preinfusionTime < 0) issues << "preinfusionTime is negative";
    if (holdTime < 0) issues << "holdTime is negative";
    if (simpleDeclineTime < 0) issues << "simpleDeclineTime is negative";

    // Weight bounds
    if (infuseWeight < 0) issues << "infuseWeight is negative";

    // Limiter bounds
    if (limiterValue < 0 || limiterValue > Profile::kMaxSettableFlow)
        issues << QString("limiterValue out of range [0, %1]").arg(Profile::kMaxSettableFlow);
    if (limiterRange < 0 || limiterRange > 10)
        issues << "limiterRange out of range [0, 10]";

    // BLE header bounds (-1 sentinel is valid, means "use countPreinfuseFrames()")
    if (preinfuseFrameCount < -1 || preinfuseFrameCount > 20)
        issues << "preinfuseFrameCount out of range [-1, 20]";

    return issues;
}

void RecipeParams::clamp() {
    auto clampVal = [](double& v, double lo, double hi) { v = qBound(lo, v, hi); };

    clampVal(targetWeight, 0.0, 500.0);
    clampVal(targetVolume, 0.0, 500.0);

    // Temperatures (0-110)
    for (double* t : {&fillTemperature, &pourTemperature, &tempStart, &tempPreinfuse, &tempHold, &tempDecline})
        clampVal(*t, 0.0, 110.0);

    // Pressures (0-12)
    for (double* p : {&infusePressure, &pourPressure, &espressoPressure, &pressureEnd})
        clampVal(*p, 0.0, 12.0);

    for (double* f : {&pourFlow, &holdFlow, &flowEnd, &preinfusionFlowRate})
        clampVal(*f, 0.0, Profile::kMaxSettableFlow);

    // Times (non-negative)
    for (double* t : {&infuseTime, &rampTime, &preinfusionTime, &holdTime, &simpleDeclineTime})
        if (*t < 0) *t = 0;

    if (infuseWeight < 0) infuseWeight = 0;
    // One field, two units: a flow limit in mL/s on a pressure step, a pressure limit in
    // bar on a flow step. The clamp has to admit the larger of the two ranges; each
    // editor imposes its own ceiling for the unit it is showing.
    clampVal(limiterValue, 0.0, Profile::kMaxSettableFlow);
    clampVal(limiterRange, 0.0, 10.0);
}

QJsonObject RecipeParams::toJson() const {
    QJsonObject obj;

    // Core
    obj["targetWeight"] = targetWeight;
    obj["targetVolume"] = targetVolume;

    // Fill
    obj["fillTemperature"] = fillTemperature;

    // Infuse
    obj["infusePressure"] = infusePressure;
    obj["infuseTime"] = infuseTime;
    obj["infuseWeight"] = infuseWeight;
    obj["infuseVolume"] = infuseVolume;

    // Pour (always flow-driven with pressure limit)
    obj["pourTemperature"] = pourTemperature;
    obj["pourPressure"] = pourPressure;
    obj["pourFlow"] = pourFlow;
    obj["rampTime"] = rampTime;

    // A-Flow specific
    obj["rampDownEnabled"] = rampDownEnabled;
    obj["flowExtractionUp"] = flowExtractionUp;
    obj["secondFillEnabled"] = secondFillEnabled;

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

    // BLE header
    if (preinfuseFrameCount >= 0)
        obj["preinfuseFrameCount"] = preinfuseFrameCount;

    return obj;
}

RecipeParams RecipeParams::fromJson(const QJsonObject& json) {
    RecipeParams params;

    // Core
    params.targetWeight = json["targetWeight"].toDouble(36.0);
    params.targetVolume = json["targetVolume"].toDouble(0.0);

    // Fill
    params.fillTemperature = json["fillTemperature"].toDouble(88.0);
    // Legacy support: use "temperature" if "fillTemperature" not present
    if (!json.contains("fillTemperature") && json.contains("temperature")) {
        params.fillTemperature = json["temperature"].toDouble(88.0);
    }

    // Infuse
    params.infusePressure = json["infusePressure"].toDouble(3.0);
    params.infuseTime = json["infuseTime"].toDouble(20.0);
    params.infuseWeight = json["infuseWeight"].toDouble(4.0);
    params.infuseVolume = json["infuseVolume"].toDouble(100.0);

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

    params.rampTime = json["rampTime"].toDouble(5.0);

    // A-Flow specific
    params.rampDownEnabled = json["rampDownEnabled"].toBool(false);
    params.flowExtractionUp = json["flowExtractionUp"].toBool(true);
    params.secondFillEnabled = json["secondFillEnabled"].toBool(false);

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

    // BLE header
    if (json.contains("preinfuseFrameCount"))
        params.preinfuseFrameCount = json["preinfuseFrameCount"].toInt(-1);

    return params;
}

QVariantMap RecipeParams::toVariantMap() const {
    QVariantMap map;

    // Core
    map["targetWeight"] = targetWeight;
    map["targetVolume"] = targetVolume;

    // Fill
    map["fillTemperature"] = fillTemperature;

    // Infuse
    map["infusePressure"] = infusePressure;
    map["infuseTime"] = infuseTime;
    map["infuseWeight"] = infuseWeight;
    map["infuseVolume"] = infuseVolume;

    // Pour (always flow-driven with pressure limit)
    map["pourTemperature"] = pourTemperature;
    map["pourPressure"] = pourPressure;
    map["pourFlow"] = pourFlow;
    map["rampTime"] = rampTime;

    // A-Flow specific
    map["rampDownEnabled"] = rampDownEnabled;
    map["flowExtractionUp"] = flowExtractionUp;
    map["secondFillEnabled"] = secondFillEnabled;

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

    // BLE header
    if (preinfuseFrameCount >= 0)
        map["preinfuseFrameCount"] = preinfuseFrameCount;

    return map;
}

RecipeParams RecipeParams::fromVariantMap(const QVariantMap& map) {
    RecipeParams params;

    // Core
    params.targetWeight = map.value("targetWeight", 36.0).toDouble();
    params.targetVolume = map.value("targetVolume", 0.0).toDouble();

    // Fill
    params.fillTemperature = map.value("fillTemperature", 88.0).toDouble();
    // Legacy support
    if (!map.contains("fillTemperature") && map.contains("temperature")) {
        params.fillTemperature = map.value("temperature", 88.0).toDouble();
    }

    // Infuse
    params.infusePressure = map.value("infusePressure", 3.0).toDouble();
    params.infuseTime = map.value("infuseTime", 20.0).toDouble();
    params.infuseWeight = map.value("infuseWeight", 4.0).toDouble();
    params.infuseVolume = map.value("infuseVolume", 100.0).toDouble();

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

    params.rampTime = map.value("rampTime", 5.0).toDouble();

    // A-Flow specific
    params.rampDownEnabled = map.value("rampDownEnabled", false).toBool();
    params.flowExtractionUp = map.value("flowExtractionUp", true).toBool();
    params.secondFillEnabled = map.value("secondFillEnabled", false).toBool();

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

    // BLE header
    if (map.contains("preinfuseFrameCount"))
        params.preinfuseFrameCount = map.value("preinfuseFrameCount", -1).toInt();

    return params;
}
