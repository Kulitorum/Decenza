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

    // Editor type
    obj["editorType"] = editorType;

    // A-Flow extensions
    obj["secondFillEnabled"] = secondFillEnabled;
    obj["rampDownEnabled"] = rampDownEnabled;
    obj["rampDownPressure"] = rampDownPressure;
    obj["flowUpEnabled"] = flowUpEnabled;

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

    // Editor type
    params.editorType = json["editorType"].toString("dflow");

    // A-Flow extensions
    params.secondFillEnabled = json["secondFillEnabled"].toBool(false);
    params.rampDownEnabled = json["rampDownEnabled"].toBool(false);
    params.rampDownPressure = json["rampDownPressure"].toDouble(4.0);
    params.flowUpEnabled = json["flowUpEnabled"].toBool(false);

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

    // Editor type
    map["editorType"] = editorType;

    // A-Flow extensions
    map["secondFillEnabled"] = secondFillEnabled;
    map["rampDownEnabled"] = rampDownEnabled;
    map["rampDownPressure"] = rampDownPressure;
    map["flowUpEnabled"] = flowUpEnabled;

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

    // Editor type
    params.editorType = map.value("editorType", "dflow").toString();

    // A-Flow extensions
    params.secondFillEnabled = map.value("secondFillEnabled", false).toBool();
    params.rampDownEnabled = map.value("rampDownEnabled", false).toBool();
    params.rampDownPressure = map.value("rampDownPressure", 4.0).toDouble();
    params.flowUpEnabled = map.value("flowUpEnabled", false).toBool();

    return params;
}

// === D-Flow Presets ===

RecipeParams RecipeParams::classic() {
    RecipeParams params;
    params.editorType = "dflow";
    params.targetWeight = 36.0;
    params.dose = 18.0;

    params.fillTemperature = 93.0;
    params.fillPressure = 3.0;
    params.fillFlow = 8.0;
    params.fillTimeout = 25.0;
    params.fillExitPressure = 3.0;

    params.infusePressure = 3.0;
    params.infuseTime = 8.0;
    params.infuseByWeight = false;
    params.bloomEnabled = false;

    params.pourTemperature = 93.0;
    params.pourFlow = 2.0;
    params.pourPressure = 9.0;
    params.rampTime = 2.0;

    params.declineEnabled = false;

    return params;
}

RecipeParams RecipeParams::londinium() {
    RecipeParams params;
    params.editorType = "dflow";
    params.targetWeight = 36.0;
    params.dose = 18.0;

    params.fillTemperature = 88.0;
    params.fillPressure = 3.0;
    params.fillFlow = 8.0;
    params.fillTimeout = 25.0;
    params.fillExitPressure = 3.0;

    params.infusePressure = 3.0;
    params.infuseTime = 20.0;
    params.infuseByWeight = false;
    params.bloomEnabled = false;

    params.pourTemperature = 90.0;
    params.pourFlow = 2.0;
    params.pourPressure = 9.0;
    params.rampTime = 5.0;

    params.declineEnabled = true;
    params.declineTo = 1.0;
    params.declineTime = 30.0;

    return params;
}

RecipeParams RecipeParams::turbo() {
    RecipeParams params;
    params.editorType = "dflow";
    params.targetWeight = 50.0;
    params.dose = 18.0;

    params.fillTemperature = 90.0;
    params.fillPressure = 3.0;
    params.fillFlow = 8.0;
    params.fillTimeout = 8.0;
    params.fillExitPressure = 2.0;

    params.infuseEnabled = false;  // No infuse for turbo
    params.infusePressure = 3.0;
    params.infuseTime = 0.0;
    params.infuseByWeight = false;
    params.bloomEnabled = false;

    params.pourTemperature = 90.0;
    params.pourFlow = 4.5;
    params.pourPressure = 6.0;
    params.rampEnabled = false;  // No ramp for turbo - instant jump to pour
    params.rampTime = 0.0;

    params.declineEnabled = false;

    return params;
}

RecipeParams RecipeParams::blooming() {
    RecipeParams params;
    params.editorType = "dflow";
    params.targetWeight = 40.0;
    params.dose = 18.0;

    params.fillTemperature = 92.0;
    params.fillPressure = 6.0;
    params.fillFlow = 6.0;
    params.fillTimeout = 8.0;
    params.fillExitPressure = 1.5;

    params.infusePressure = 0.0;  // Bloom uses 0 flow
    params.infuseTime = 20.0;
    params.infuseByWeight = false;
    params.bloomEnabled = true;
    params.bloomTime = 20.0;

    params.pourTemperature = 92.0;
    params.pourFlow = 2.0;
    params.pourPressure = 9.0;
    params.rampTime = 10.0;

    params.declineEnabled = false;

    return params;
}

RecipeParams RecipeParams::dflowDefault() {
    // D-Flow default settings based on Damian's plugin
    RecipeParams params;
    params.editorType = "dflow";
    params.targetWeight = 36.0;
    params.dose = 18.0;

    params.fillTemperature = 88.0;
    params.fillPressure = 3.0;
    params.fillFlow = 8.0;
    params.fillTimeout = 15.0;
    params.fillExitPressure = 3.0;

    params.infusePressure = 3.0;
    params.infuseTime = 60.0;
    params.infuseByWeight = true;
    params.infuseWeight = 4.0;
    params.infuseVolume = 100.0;
    params.bloomEnabled = false;

    params.pourTemperature = 88.0;
    params.pourFlow = 1.7;
    params.pourPressure = 4.8;
    params.rampTime = 5.0;

    params.declineEnabled = false;

    return params;
}

// === A-Flow Presets ===

RecipeParams RecipeParams::aflowDefault() {
    RecipeParams params;
    params.editorType = "aflow";
    params.targetWeight = 36.0;
    params.dose = 18.0;

    params.fillTemperature = 88.0;
    params.fillPressure = 3.0;
    params.fillFlow = 8.0;
    params.fillTimeout = 15.0;
    params.fillExitPressure = 3.0;

    params.infusePressure = 3.0;
    params.infuseTime = 60.0;
    params.infuseByWeight = true;
    params.infuseWeight = 4.0;
    params.infuseVolume = 100.0;
    params.bloomEnabled = false;

    params.pourTemperature = 88.0;
    params.pourFlow = 1.5;
    params.pourPressure = 8.5;
    params.rampEnabled = true;
    params.rampTime = 16.0;

    params.rampDownEnabled = true;
    params.rampDownPressure = 4.0;
    params.flowUpEnabled = false;
    params.secondFillEnabled = false;

    params.declineEnabled = false;

    return params;
}

RecipeParams RecipeParams::aflowMedium() {
    RecipeParams params;
    params.editorType = "aflow";
    params.targetWeight = 36.0;
    params.dose = 18.0;

    params.fillTemperature = 90.0;
    params.fillPressure = 3.0;
    params.fillFlow = 8.0;
    params.fillTimeout = 15.0;
    params.fillExitPressure = 3.0;

    params.infusePressure = 3.0;
    params.infuseTime = 60.0;
    params.infuseByWeight = true;
    params.infuseWeight = 4.0;
    params.infuseVolume = 100.0;
    params.bloomEnabled = false;

    params.pourTemperature = 90.0;
    params.pourFlow = 2.0;
    params.pourPressure = 8.5;
    params.rampEnabled = true;
    params.rampTime = 12.0;

    params.rampDownEnabled = true;
    params.rampDownPressure = 5.0;
    params.flowUpEnabled = true;
    params.secondFillEnabled = false;

    params.declineEnabled = false;

    return params;
}

RecipeParams RecipeParams::aflowLever() {
    RecipeParams params;
    params.editorType = "aflow";
    params.targetWeight = 36.0;
    params.dose = 18.0;

    params.fillTemperature = 88.0;
    params.fillPressure = 1.5;
    params.fillFlow = 8.0;
    params.fillTimeout = 20.0;
    params.fillExitPressure = 1.5;

    params.infusePressure = 1.5;
    params.infuseTime = 60.0;
    params.infuseByWeight = true;
    params.infuseWeight = 4.0;
    params.infuseVolume = 100.0;
    params.bloomEnabled = false;

    params.pourTemperature = 88.0;
    params.pourFlow = 1.2;
    params.pourPressure = 6.0;
    params.rampEnabled = true;
    params.rampTime = 20.0;

    params.rampDownEnabled = true;
    params.rampDownPressure = 3.0;
    params.flowUpEnabled = false;
    params.secondFillEnabled = true;

    params.declineEnabled = false;

    return params;
}
