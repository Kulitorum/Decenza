#include "recipeparams.h"

QJsonObject RecipeParams::toJson() const {
    QJsonObject obj;

    // Core
    obj["targetWeight"] = targetWeight;
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

    // Pour
    obj["pourTemperature"] = pourTemperature;
    obj["pourStyle"] = pourStyle;
    obj["pourPressure"] = pourPressure;
    obj["pourFlow"] = pourFlow;
    obj["flowLimit"] = flowLimit;
    obj["pressureLimit"] = pressureLimit;
    obj["rampEnabled"] = rampEnabled;
    obj["rampTime"] = rampTime;

    // Decline
    obj["declineEnabled"] = declineEnabled;
    obj["declineTo"] = declineTo;
    obj["declineTime"] = declineTime;

    return obj;
}

RecipeParams RecipeParams::fromJson(const QJsonObject& json) {
    RecipeParams params;

    // Core
    params.targetWeight = json["targetWeight"].toDouble(36.0);
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
    params.pourStyle = json["pourStyle"].toString("flow");
    params.pourPressure = json["pourPressure"].toDouble(9.0);
    params.pourFlow = json["pourFlow"].toDouble(2.0);
    params.flowLimit = json["flowLimit"].toDouble(0.0);
    params.pressureLimit = json["pressureLimit"].toDouble(6.0);
    params.rampEnabled = json["rampEnabled"].toBool(true);  // Default true for legacy
    params.rampTime = json["rampTime"].toDouble(5.0);

    // Decline
    params.declineEnabled = json["declineEnabled"].toBool(false);
    params.declineTo = json["declineTo"].toDouble(6.0);
    params.declineTime = json["declineTime"].toDouble(30.0);

    return params;
}

QVariantMap RecipeParams::toVariantMap() const {
    QVariantMap map;

    // Core
    map["targetWeight"] = targetWeight;
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

    // Pour
    map["pourTemperature"] = pourTemperature;
    map["pourStyle"] = pourStyle;
    map["pourPressure"] = pourPressure;
    map["pourFlow"] = pourFlow;
    map["flowLimit"] = flowLimit;
    map["pressureLimit"] = pressureLimit;
    map["rampEnabled"] = rampEnabled;
    map["rampTime"] = rampTime;

    // Decline
    map["declineEnabled"] = declineEnabled;
    map["declineTo"] = declineTo;
    map["declineTime"] = declineTime;

    return map;
}

RecipeParams RecipeParams::fromVariantMap(const QVariantMap& map) {
    RecipeParams params;

    // Core
    params.targetWeight = map.value("targetWeight", 36.0).toDouble();
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
    params.pourStyle = map.value("pourStyle", "flow").toString();
    params.pourPressure = map.value("pourPressure", 9.0).toDouble();
    params.pourFlow = map.value("pourFlow", 2.0).toDouble();
    params.flowLimit = map.value("flowLimit", 0.0).toDouble();
    params.pressureLimit = map.value("pressureLimit", 6.0).toDouble();
    params.rampEnabled = map.value("rampEnabled", true).toBool();  // Default true for legacy
    params.rampTime = map.value("rampTime", 5.0).toDouble();

    // Decline
    params.declineEnabled = map.value("declineEnabled", false).toBool();
    params.declineTo = map.value("declineTo", 6.0).toDouble();
    params.declineTime = map.value("declineTime", 30.0).toDouble();

    return params;
}

// === Presets ===

RecipeParams RecipeParams::classic() {
    RecipeParams params;
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
    params.pourStyle = "pressure";
    params.pourPressure = 9.0;
    params.flowLimit = 0.0;
    params.rampTime = 2.0;

    params.declineEnabled = false;

    return params;
}

RecipeParams RecipeParams::londinium() {
    RecipeParams params;
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
    params.pourStyle = "pressure";
    params.pourPressure = 9.0;
    params.flowLimit = 2.5;
    params.rampTime = 5.0;

    params.declineEnabled = true;
    params.declineTo = 6.0;
    params.declineTime = 30.0;

    return params;
}

RecipeParams RecipeParams::turbo() {
    RecipeParams params;
    params.targetWeight = 50.0;
    params.dose = 18.0;

    params.fillTemperature = 90.0;
    params.fillPressure = 3.0;
    params.fillFlow = 8.0;
    params.fillTimeout = 8.0;
    params.fillExitPressure = 2.0;

    params.infusePressure = 3.0;
    params.infuseTime = 0.0;  // No infuse for turbo
    params.infuseByWeight = false;
    params.bloomEnabled = false;

    params.pourTemperature = 90.0;
    params.pourStyle = "flow";
    params.pourFlow = 4.5;
    params.pressureLimit = 6.0;
    params.rampTime = 0.0;

    params.declineEnabled = false;

    return params;
}

RecipeParams RecipeParams::blooming() {
    RecipeParams params;
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
    params.pourStyle = "flow";
    params.pourFlow = 2.0;
    params.pressureLimit = 9.0;
    params.rampTime = 10.0;

    params.declineEnabled = false;

    return params;
}

RecipeParams RecipeParams::dflowDefault() {
    // D-Flow default settings based on Damian's plugin
    RecipeParams params;
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
    params.pourStyle = "flow";
    params.pourFlow = 1.7;
    params.pressureLimit = 4.8;
    params.rampTime = 5.0;

    params.declineEnabled = false;

    return params;
}
