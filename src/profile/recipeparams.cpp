#include "recipeparams.h"

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
    obj["editorType"] = editorType;

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
    QString oldStyle = json["pourStyle"].toString("");
    if (!oldStyle.isEmpty()) {
        // Old format had pourStyle = "pressure" or "flow"
        if (oldStyle == "pressure") {
            // Pressure mode: pourPressure was the setpoint, flowLimit was the cap
            // New model: always flow-driven, so old pressure setpoint becomes the cap
            params.pourPressure = json["pourPressure"].toDouble(9.0);
            // Use flowLimit if present, otherwise default
            params.pourFlow = json.contains("flowLimit") && json["flowLimit"].toDouble(0.0) > 0
                ? json["flowLimit"].toDouble(2.0)
                : json["pourFlow"].toDouble(2.0);
        } else {
            // Flow mode: pourFlow was the setpoint, pressureLimit was the cap
            params.pourFlow = json["pourFlow"].toDouble(2.0);
            params.pourPressure = json.contains("pressureLimit")
                ? json["pressureLimit"].toDouble(6.0)
                : json["pourPressure"].toDouble(9.0);
        }
    } else {
        // New format: pourPressure is always the pressure cap, pourFlow is the flow setpoint
        params.pourPressure = json["pourPressure"].toDouble(9.0);
        params.pourFlow = json["pourFlow"].toDouble(2.0);
    }

    params.rampEnabled = json["rampEnabled"].toBool(true);  // Default true for legacy
    params.rampTime = json["rampTime"].toDouble(5.0);

    // Decline
    params.declineEnabled = json["declineEnabled"].toBool(false);
    params.declineTo = json["declineTo"].toDouble(1.0);
    params.declineTime = json["declineTime"].toDouble(30.0);

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
    params.editorType = json["editorType"].toString("dflow");

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
    map["editorType"] = editorType;

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
    QString oldStyle = map.value("pourStyle", "").toString();
    if (!oldStyle.isEmpty()) {
        if (oldStyle == "pressure") {
            params.pourPressure = map.value("pourPressure", 9.0).toDouble();
            // Use flowLimit if present, otherwise default
            double oldFlowLimit = map.value("flowLimit", 0.0).toDouble();
            params.pourFlow = (map.contains("flowLimit") && oldFlowLimit > 0)
                ? oldFlowLimit
                : map.value("pourFlow", 2.0).toDouble();
        } else {
            params.pourFlow = map.value("pourFlow", 2.0).toDouble();
            params.pourPressure = map.contains("pressureLimit")
                ? map.value("pressureLimit", 6.0).toDouble()
                : map.value("pourPressure", 9.0).toDouble();
        }
    } else {
        params.pourPressure = map.value("pourPressure", 9.0).toDouble();
        params.pourFlow = map.value("pourFlow", 2.0).toDouble();
    }

    params.rampEnabled = map.value("rampEnabled", true).toBool();  // Default true for legacy
    params.rampTime = map.value("rampTime", 5.0).toDouble();

    // Decline
    params.declineEnabled = map.value("declineEnabled", false).toBool();
    params.declineTo = map.value("declineTo", 1.0).toDouble();
    params.declineTime = map.value("declineTime", 30.0).toDouble();

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
    params.editorType = map.value("editorType", "dflow").toString();

    return params;
}
