#include "profile.h"
#include "recipegenerator.h"
#include "recipeanalyzer.h"
#include "../ble/protocol/binarycodec.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>

// Generate frames for simple pressure profile (settings_2a)
// Based on de1app's pressure_to_advanced_list()
static QVector<ProfileFrame> generatePressureProfileFrames(
    double preinfusionTime, double preinfusionFlowRate, double preinfusionStopPressure,
    double holdTime, double espressoPressure,
    double declineTime, double pressureEnd,
    double maximumFlow, double maximumFlowRange,
    double temp0, double temp1, double temp2, double temp3,
    bool tempStepsEnabled)
{
    QVector<ProfileFrame> frames;

    // Use same temperature for all frames if stepping not enabled
    if (!tempStepsEnabled) {
        temp1 = temp0;
        temp2 = temp0;
        temp3 = temp0;
    }

    // Preinfusion frame (flow pump, exit on pressure_over)
    if (preinfusionTime > 0) {
        ProfileFrame preinfusion;
        preinfusion.name = "preinfusion";
        preinfusion.temperature = temp1;
        preinfusion.sensor = "coffee";
        preinfusion.pump = "flow";
        preinfusion.transition = "fast";
        preinfusion.pressure = 1.0;
        preinfusion.flow = preinfusionFlowRate;
        preinfusion.seconds = preinfusionTime;
        preinfusion.volume = 0;
        preinfusion.exitIf = true;
        preinfusion.exitType = "pressure_over";
        preinfusion.exitPressureOver = preinfusionStopPressure;
        preinfusion.exitFlowOver = 6.0;
        frames.append(preinfusion);
    }

    // Rise and hold frame (pressure pump)
    if (holdTime > 0) {
        // If hold time > 3s, add a forced rise frame without limiter first
        if (holdTime > 3) {
            ProfileFrame riseNoLimit;
            riseNoLimit.name = "forced rise without limit";
            riseNoLimit.temperature = temp2;
            riseNoLimit.sensor = "coffee";
            riseNoLimit.pump = "pressure";
            riseNoLimit.transition = "fast";
            riseNoLimit.pressure = espressoPressure;
            riseNoLimit.seconds = 3.0;
            riseNoLimit.volume = 0;
            riseNoLimit.exitIf = false;
            frames.append(riseNoLimit);
            holdTime -= 3;
        }

        ProfileFrame hold;
        hold.name = "rise and hold";
        hold.temperature = temp2;
        hold.sensor = "coffee";
        hold.pump = "pressure";
        hold.transition = "fast";
        hold.pressure = espressoPressure;
        hold.seconds = holdTime;
        hold.volume = 0;
        hold.exitIf = false;
        if (maximumFlow > 0) {
            hold.maxFlowOrPressure = maximumFlow;
            hold.maxFlowOrPressureRange = maximumFlowRange;
        }
        frames.append(hold);
    }

    // Decline frame (pressure pump, smooth transition)
    if (declineTime > 0) {
        // If this is the first pressurized step and decline time > 3s, add forced rise first
        if (holdTime <= 0 && declineTime > 3) {
            ProfileFrame riseNoLimit;
            riseNoLimit.name = "forced rise without limit";
            riseNoLimit.temperature = temp3;
            riseNoLimit.sensor = "coffee";
            riseNoLimit.pump = "pressure";
            riseNoLimit.transition = "fast";
            riseNoLimit.pressure = espressoPressure;
            riseNoLimit.seconds = 3.0;
            riseNoLimit.volume = 0;
            riseNoLimit.exitIf = false;
            frames.append(riseNoLimit);
            declineTime -= 3;
        }

        ProfileFrame decline;
        decline.name = "decline";
        decline.temperature = temp3;
        decline.sensor = "coffee";
        decline.pump = "pressure";
        decline.transition = "smooth";
        decline.pressure = pressureEnd;
        decline.seconds = declineTime;
        decline.volume = 0;
        decline.exitIf = false;
        decline.exitFlowOver = 6.0;
        if (maximumFlow > 0) {
            decline.maxFlowOrPressure = maximumFlow;
            decline.maxFlowOrPressureRange = maximumFlowRange;
        }
        frames.append(decline);
    }

    // Add empty frame if no frames were created
    if (frames.isEmpty()) {
        ProfileFrame empty;
        empty.name = "empty";
        empty.temperature = 90.0;
        empty.sensor = "coffee";
        empty.pump = "flow";
        empty.transition = "smooth";
        empty.flow = 0;
        empty.seconds = 0;
        empty.volume = 0;
        empty.exitIf = false;
        frames.append(empty);
    }

    return frames;
}

// Generate frames for simple flow profile (settings_2b)
// Based on de1app's flow_to_advanced_list()
static QVector<ProfileFrame> generateFlowProfileFrames(
    double preinfusionTime, double preinfusionFlowRate, double preinfusionStopPressure,
    double holdTime, double flowHold,
    double declineTime, double flowDecline,
    double maximumPressure, double maximumPressureRange,
    double temp0, double temp1, double temp2, double temp3,
    bool tempStepsEnabled)
{
    QVector<ProfileFrame> frames;

    // Use same temperature for all frames if stepping not enabled
    if (!tempStepsEnabled) {
        temp1 = temp0;
        temp2 = temp0;
        temp3 = temp0;
    }

    // Preinfusion frame (flow pump, exit on pressure_over)
    if (preinfusionTime > 0) {
        ProfileFrame preinfusion;
        preinfusion.name = "preinfusion";
        preinfusion.temperature = temp1;
        preinfusion.sensor = "coffee";
        preinfusion.pump = "flow";
        preinfusion.transition = "fast";
        preinfusion.pressure = 1.0;
        preinfusion.flow = preinfusionFlowRate;
        preinfusion.seconds = preinfusionTime;
        preinfusion.volume = 0;
        preinfusion.exitIf = true;
        preinfusion.exitType = "pressure_over";
        preinfusion.exitPressureOver = preinfusionStopPressure;
        frames.append(preinfusion);
    }

    // Hold frame (flow pump)
    if (holdTime > 0) {
        ProfileFrame hold;
        hold.name = "hold";
        hold.temperature = temp2;
        hold.sensor = "coffee";
        hold.pump = "flow";
        hold.transition = "fast";
        hold.flow = flowHold;
        hold.seconds = holdTime;
        hold.volume = 0;
        hold.exitIf = false;
        hold.exitFlowOver = 6.0;
        if (maximumPressure > 0) {
            hold.maxFlowOrPressure = maximumPressure;
            hold.maxFlowOrPressureRange = maximumPressureRange;
        }
        frames.append(hold);
    }

    // Decline frame (flow pump, smooth transition)
    if (declineTime > 0) {
        ProfileFrame decline;
        decline.name = "decline";
        decline.temperature = temp3;
        decline.sensor = "coffee";
        decline.pump = "flow";
        decline.transition = "smooth";
        decline.flow = flowDecline;
        decline.seconds = declineTime;
        decline.volume = 0;
        decline.exitIf = false;
        if (maximumPressure > 0) {
            decline.maxFlowOrPressure = maximumPressure;
            decline.maxFlowOrPressureRange = maximumPressureRange;
        }
        frames.append(decline);
    }

    // Add empty frame if no frames were created
    if (frames.isEmpty()) {
        ProfileFrame empty;
        empty.name = "empty";
        empty.temperature = 90.0;
        empty.sensor = "coffee";
        empty.pump = "flow";
        empty.transition = "smooth";
        empty.flow = 0;
        empty.seconds = 0;
        empty.volume = 0;
        empty.exitIf = false;
        frames.append(empty);
    }

    return frames;
}

QJsonDocument Profile::toJson() const {
    QJsonObject obj;
    obj["title"] = m_title;
    obj["author"] = m_author;
    obj["profile_notes"] = m_profileNotes;
    obj["beverage_type"] = m_beverageType;
    obj["profile_type"] = m_profileType;
    obj["target_weight"] = m_targetWeight;
    obj["target_volume"] = m_targetVolume;
    obj["stop_at_type"] = (m_stopAtType == StopAtType::Volume) ? "volume" : "weight";
    obj["espresso_temperature"] = m_espressoTemperature;
    obj["maximum_pressure"] = m_maximumPressure;
    obj["maximum_flow"] = m_maximumFlow;
    obj["minimum_pressure"] = m_minimumPressure;
    obj["preinfuse_frame_count"] = m_preinfuseFrameCount;
    obj["has_recommended_dose"] = m_hasRecommendedDose;
    obj["recommended_dose"] = m_recommendedDose;
    obj["mode"] = (m_mode == Mode::DirectControl) ? "direct" : "frame_based";

    QJsonArray tempsArray;
    for (double temp : m_temperaturePresets) {
        tempsArray.append(temp);
    }
    obj["temperature_presets"] = tempsArray;

    QJsonArray stepsArray;
    for (const auto& step : m_steps) {
        stepsArray.append(step.toJson());
    }
    obj["steps"] = stepsArray;

    // Recipe mode data
    obj["is_recipe_mode"] = m_isRecipeMode;
    if (m_isRecipeMode) {
        obj["recipe"] = m_recipeParams.toJson();
    }

    return QJsonDocument(obj);
}

Profile Profile::fromJson(const QJsonDocument& doc) {
    Profile profile;
    QJsonObject obj = doc.object();

    profile.m_title = obj["title"].toString("Default");
    profile.m_author = obj["author"].toString();
    // Support both new "profile_notes" and legacy "notes" keys
    profile.m_profileNotes = obj["profile_notes"].toString();
    if (profile.m_profileNotes.isEmpty()) {
        profile.m_profileNotes = obj["notes"].toString();
    }
    profile.m_beverageType = obj["beverage_type"].toString("espresso");
    profile.m_profileType = obj["profile_type"].toString("settings_2c");
    profile.m_targetWeight = obj["target_weight"].toDouble(36.0);
    profile.m_targetVolume = obj["target_volume"].toDouble(36.0);
    QString stopAtStr = obj["stop_at_type"].toString("weight");
    profile.m_stopAtType = (stopAtStr == "volume") ? StopAtType::Volume : StopAtType::Weight;
    profile.m_espressoTemperature = obj["espresso_temperature"].toDouble(93.0);
    profile.m_maximumPressure = obj["maximum_pressure"].toDouble(12.0);
    profile.m_maximumFlow = obj["maximum_flow"].toDouble(6.0);
    profile.m_minimumPressure = obj["minimum_pressure"].toDouble(0.0);
    profile.m_preinfuseFrameCount = obj["preinfuse_frame_count"].toInt(0);
    profile.m_hasRecommendedDose = obj["has_recommended_dose"].toBool(false);
    profile.m_recommendedDose = obj["recommended_dose"].toDouble(18.0);

    QString modeStr = obj["mode"].toString("frame_based");
    profile.m_mode = (modeStr == "direct") ? Mode::DirectControl : Mode::FrameBased;

    QJsonArray tempsArray = obj["temperature_presets"].toArray();
    profile.m_temperaturePresets.clear();
    for (const auto& temp : tempsArray) {
        profile.m_temperaturePresets.append(temp.toDouble());
    }
    if (profile.m_temperaturePresets.isEmpty()) {
        profile.m_temperaturePresets = {88.0, 90.0, 93.0, 96.0};
    }

    QJsonArray stepsArray = obj["steps"].toArray();
    for (const auto& stepVal : stepsArray) {
        profile.m_steps.append(ProfileFrame::fromJson(stepVal.toObject()));
    }

    // Recipe mode data
    profile.m_isRecipeMode = obj["is_recipe_mode"].toBool(false);
    if (profile.m_isRecipeMode && obj.contains("recipe")) {
        profile.m_recipeParams = RecipeParams::fromJson(obj["recipe"].toObject());
    }

    // Sync espresso_temperature with first frame if they differ
    // This handles profiles edited before the bug fix where only frame temps were updated
    if (!profile.m_steps.isEmpty()) {
        double firstFrameTemp = profile.m_steps.first().temperature;
        if (qAbs(profile.m_espressoTemperature - firstFrameTemp) > 0.1) {
            qDebug() << "Syncing espresso_temperature from" << profile.m_espressoTemperature
                     << "to first frame temp" << firstFrameTemp;
            profile.m_espressoTemperature = firstFrameTemp;
        }
    }

    return profile;
}

Profile Profile::loadFromFile(const QString& filePath) {
    // Check file extension
    if (filePath.endsWith(".tcl", Qt::CaseInsensitive)) {
        return loadFromTclFile(filePath);
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return Profile();
    }

    QByteArray data = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);

    // Detect de1app v2 JSON format by checking for v2-specific fields.
    // The de1app v2 format uses "version", "legacy_profile_type", and/or "type"
    // (with values "advanced"/"pressure"/"flow") which our native format doesn't have.
    // It also encodes numbers as strings (Tcl huddle behavior) and uses nested
    // "exit"/"limiter" objects instead of our flat field names.
    QJsonObject obj = doc.object();
    if (obj.contains("version") || obj.contains("legacy_profile_type")) {
        return loadFromDE1AppJson(QString::fromUtf8(data));
    }

    return fromJson(doc);
}

bool Profile::saveToFile(const QString& filePath) const {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Profile::saveToFile: Failed to open file for writing:" << filePath
                   << "- Error:" << file.errorString();
        return false;
    }

    QByteArray data = toJson().toJson(QJsonDocument::Indented);
    qint64 bytesWritten = file.write(data);
    if (bytesWritten != data.size()) {
        qWarning() << "Profile::saveToFile: Failed to write all data to:" << filePath
                   << "- Expected:" << data.size() << "bytes, wrote:" << bytesWritten
                   << "- Error:" << file.errorString();
        return false;
    }

    return true;
}

Profile Profile::loadFromJsonString(const QString& jsonContent) {
    QJsonDocument doc = QJsonDocument::fromJson(jsonContent.toUtf8());
    return fromJson(doc);
}

QString Profile::toJsonString() const {
    return QString::fromUtf8(toJson().toJson(QJsonDocument::Indented));
}

Profile Profile::loadFromTclFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Failed to open Tcl profile:" << filePath;
        return Profile();
    }

    QString content = QTextStream(&file).readAll();
    return loadFromTclString(content);
}

Profile Profile::loadFromTclString(const QString& content) {
    // Parse de1app .tcl profile format
    // Format: Tcl script with "array set" commands setting profile variables

    Profile profile;

    // Helper to extract Tcl variable value
    // Handles: varName {braced value} OR varName "quoted" OR varName simple_word
    auto extractValue = [&content](const QString& varName) -> QString {
        // Try braced value first: varName {content}
        QRegularExpression reBraced(varName + "\\s+\\{([^}]*)\\}");
        QRegularExpressionMatch match = reBraced.match(content);
        if (match.hasMatch()) {
            return match.captured(1);
        }
        // Try quoted value: varName "content"
        QRegularExpression reQuoted(varName + "\\s+\"([^\"]*)\"");
        match = reQuoted.match(content);
        if (match.hasMatch()) {
            return match.captured(1);
        }
        // Try simple word value: varName word
        QRegularExpression reSimple(varName + "\\s+(\\S+)");
        match = reSimple.match(content);
        return match.hasMatch() ? match.captured(1) : QString();
    };

    // Extract metadata
    profile.m_title = extractValue("profile_title");
    profile.m_author = extractValue("author");
    profile.m_profileNotes = extractValue("profile_notes");
    profile.m_profileType = extractValue("settings_profile_type");
    profile.m_beverageType = extractValue("beverage_type");
    if (profile.m_beverageType.isEmpty()) {
        profile.m_beverageType = "espresso";
    }

    // Determine if this is an advanced profile (settings_2c or settings_2c2)
    bool isAdvancedProfile = profile.m_profileType.startsWith("settings_2c");

    // Extract target weight - use _advanced value for advanced profiles
    QString val;
    if (isAdvancedProfile) {
        val = extractValue("final_desired_shot_weight_advanced");
        if (val.isEmpty() || val.toDouble() == 0) {
            val = extractValue("final_desired_shot_weight");
        }
    } else {
        val = extractValue("final_desired_shot_weight");
    }
    if (!val.isEmpty()) profile.m_targetWeight = val.toDouble();

    // Extract target volume - use _advanced value for advanced profiles
    if (isAdvancedProfile) {
        val = extractValue("final_desired_shot_volume_advanced");
        if (val.isEmpty() || val.toDouble() == 0) {
            val = extractValue("final_desired_shot_volume");
        }
    } else {
        val = extractValue("final_desired_shot_volume");
    }
    if (!val.isEmpty()) profile.m_targetVolume = val.toDouble();

    val = extractValue("espresso_temperature");
    if (!val.isEmpty()) profile.m_espressoTemperature = val.toDouble();

    // Extract flow/pressure limits
    val = extractValue("maximum_flow");
    if (!val.isEmpty()) profile.m_maximumFlow = val.toDouble();

    val = extractValue("maximum_pressure");
    if (!val.isEmpty()) profile.m_maximumPressure = val.toDouble();

    // Extract temperature presets
    profile.m_temperaturePresets.clear();
    for (int i = 0; i <= 3; i++) {
        val = extractValue(QString("espresso_temperature_%1").arg(i));
        if (!val.isEmpty()) {
            profile.m_temperaturePresets.append(val.toDouble());
        }
    }
    if (profile.m_temperaturePresets.isEmpty()) {
        profile.m_temperaturePresets = {88.0, 90.0, 93.0, 96.0};
    }

    // Extract advanced_shot steps
    // Format: advanced_shot {{step1 props} {step2 props} ...}
    QRegularExpression shotRe("advanced_shot\\s+\\{(.*?)\\}\\s*$",
        QRegularExpression::MultilineOption | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch shotMatch = shotRe.match(content);

    if (shotMatch.hasMatch()) {
        QString stepsContent = shotMatch.captured(1);

        // Parse each step (nested braces)
        int depth = 0;
        int stepStart = -1;

        for (int i = 0; i < stepsContent.length(); i++) {
            QChar c = stepsContent[i];
            if (c == '{') {
                if (depth == 0) stepStart = i;
                depth++;
            } else if (c == '}') {
                depth--;
                if (depth == 0 && stepStart >= 0) {
                    QString stepStr = stepsContent.mid(stepStart, i - stepStart + 1);
                    ProfileFrame frame = ProfileFrame::fromTclList(stepStr);
                    if (!frame.name.isEmpty() || frame.seconds > 0) {
                        profile.m_steps.append(frame);
                    }
                    stepStart = -1;
                }
            }
        }
    }

    // For simple profiles (settings_2a = pressure, settings_2b = flow), generate frames from
    // individual parameters if advanced_shot was empty
    if (profile.m_steps.isEmpty() && !isAdvancedProfile) {
        // Extract simple profile parameters
        double preinfusionTime = extractValue("preinfusion_time").toDouble();
        double preinfusionFlowRate = extractValue("preinfusion_flow_rate").toDouble();
        double preinfusionStopPressure = extractValue("preinfusion_stop_pressure").toDouble();
        double holdTime = extractValue("espresso_hold_time").toDouble();
        double declineTime = extractValue("espresso_decline_time").toDouble();

        // Temperature presets
        bool tempStepsEnabled = extractValue("espresso_temperature_steps_enabled").toInt() == 1;
        double temp0 = profile.m_espressoTemperature;
        double temp1 = temp0, temp2 = temp0, temp3 = temp0;
        if (!profile.m_temperaturePresets.isEmpty()) {
            temp0 = profile.m_temperaturePresets.value(0, temp0);
            temp1 = profile.m_temperaturePresets.value(1, temp0);
            temp2 = profile.m_temperaturePresets.value(2, temp0);
            temp3 = profile.m_temperaturePresets.value(3, temp0);
        }

        if (profile.m_profileType == "settings_2a") {
            // Simple pressure profile
            double espressoPressure = extractValue("espresso_pressure").toDouble();
            double pressureEnd = extractValue("pressure_end").toDouble();
            double maximumFlow = profile.m_maximumFlow;
            double maximumFlowRange = extractValue("maximum_flow_range_default").toDouble();
            if (maximumFlowRange == 0) maximumFlowRange = 1.0;

            profile.m_steps = generatePressureProfileFrames(
                preinfusionTime, preinfusionFlowRate, preinfusionStopPressure,
                holdTime, espressoPressure,
                declineTime, pressureEnd,
                maximumFlow, maximumFlowRange,
                temp0, temp1, temp2, temp3,
                tempStepsEnabled);

            qDebug() << "Generated" << profile.m_steps.size() << "frames from simple pressure profile";
        } else if (profile.m_profileType == "settings_2b") {
            // Simple flow profile
            double flowHold = extractValue("flow_profile_hold").toDouble();
            double flowDecline = extractValue("flow_profile_decline").toDouble();
            double maximumPressure = profile.m_maximumPressure;
            double maximumPressureRange = extractValue("maximum_pressure_range_default").toDouble();
            if (maximumPressureRange == 0) maximumPressureRange = 0.9;

            profile.m_steps = generateFlowProfileFrames(
                preinfusionTime, preinfusionFlowRate, preinfusionStopPressure,
                holdTime, flowHold,
                declineTime, flowDecline,
                maximumPressure, maximumPressureRange,
                temp0, temp1, temp2, temp3,
                tempStepsEnabled);

            qDebug() << "Generated" << profile.m_steps.size() << "frames from simple flow profile";
        }
    }

    // Set espresso temperature from first step if not set
    if (profile.m_espressoTemperature == 0 && !profile.m_steps.isEmpty()) {
        profile.m_espressoTemperature = profile.m_steps.first().temperature;
    }

    // Determine preinfuse frame count (steps before extraction phase)
    // Usually the first step(s) with exit conditions
    profile.m_preinfuseFrameCount = 0;
    for (const auto& step : profile.m_steps) {
        if (step.exitIf && (step.exitType == "pressure_over" || step.exitType == "flow_over")) {
            profile.m_preinfuseFrameCount++;
        } else {
            break;
        }
    }

    qDebug() << "Loaded Tcl profile:" << profile.m_title
             << "with" << profile.m_steps.size() << "steps";

    // Keep all imported profiles as frame-based to preserve exact frame structure and timing
    // Converting to recipe mode would regenerate frames with different timing via RecipeGenerator

    return profile;
}

Profile Profile::loadFromDE1AppJson(const QString& jsonContent) {
    // Parse DE1 app / Visualizer JSON format
    // This format uses different field names and structures than our native format
    QJsonDocument doc = QJsonDocument::fromJson(jsonContent.toUtf8());
    if (doc.isNull() || !doc.isObject()) {
        qWarning() << "loadFromDE1AppJson: Invalid JSON";
        return Profile();
    }

    QJsonObject json = doc.object();
    Profile profile;

    // Helper to convert value that may be string or number
    auto toDouble = [](const QJsonValue& val, double defaultVal = 0.0) -> double {
        if (val.isString()) {
            return val.toString().toDouble();
        }
        return val.toDouble(defaultVal);
    };

    // Extract metadata
    profile.m_title = json["title"].toString("Imported Profile");
    profile.m_author = json["author"].toString();
    // Support both "profile_notes" and "notes" keys
    profile.m_profileNotes = json["profile_notes"].toString();
    if (profile.m_profileNotes.isEmpty()) {
        profile.m_profileNotes = json["notes"].toString();
    }
    profile.m_beverageType = json["beverage_type"].toString("espresso");

    QString profileType = json["legacy_profile_type"].toString();
    if (profileType.isEmpty()) {
        profileType = json["profile_type"].toString("settings_2c");
    }
    profile.m_profileType = profileType;

    profile.m_targetWeight = toDouble(json["target_weight"], 36.0);
    profile.m_targetVolume = toDouble(json["target_volume"], 0.0);

    // Parse steps array
    QJsonArray stepsArray = json["steps"].toArray();
    for (const auto& stepVal : stepsArray) {
        QJsonObject stepJson = stepVal.toObject();
        ProfileFrame frame;

        frame.name = stepJson["name"].toString();
        frame.temperature = toDouble(stepJson["temperature"], 93.0);
        frame.sensor = stepJson["sensor"].toString("coffee");
        frame.pump = stepJson["pump"].toString("flow");
        frame.transition = stepJson["transition"].toString("fast");
        frame.pressure = toDouble(stepJson["pressure"], 0.0);
        frame.flow = toDouble(stepJson["flow"], 0.0);
        frame.seconds = toDouble(stepJson["seconds"], 0.0);
        frame.volume = toDouble(stepJson["volume"], 0.0);

        // Parse exit condition
        QJsonObject exitObj = stepJson["exit"].toObject();
        if (!exitObj.isEmpty()) {
            frame.exitIf = true;
            QString exitType = exitObj["type"].toString();
            double exitValue = toDouble(exitObj["value"]);
            QString exitCondition = exitObj["condition"].toString("over");

            // Handle specific exit types
            if (exitType == "pressure") {
                if (exitCondition == "over") {
                    frame.exitType = "pressure_over";
                    frame.exitPressureOver = exitValue;
                } else {
                    frame.exitType = "pressure_under";
                    frame.exitPressureUnder = exitValue;
                }
            } else if (exitType == "flow") {
                if (exitCondition == "over") {
                    frame.exitType = "flow_over";
                    frame.exitFlowOver = exitValue;
                } else {
                    frame.exitType = "flow_under";
                    frame.exitFlowUnder = exitValue;
                }
            } else if (exitType == "weight") {
                frame.exitType = "weight";
                frame.exitWeight = exitValue;
            }
        }

        // Also check for standalone weight and exit_weight fields
        // NOTE: Weight exit is INDEPENDENT of exitIf - a frame can have machine-side
        // exit (pressure/flow) via exit_if + exit_type, AND app-side weight exit via
        // the standalone "weight" or "exit_weight" property. Both can coexist on the same frame.
        double weightExit = toDouble(stepJson["weight"], 0.0);
        if (weightExit <= 0) {
            // Also check exit_weight (our native format field name)
            weightExit = toDouble(stepJson["exit_weight"], 0.0);
        }
        if (weightExit > 0) {
            frame.exitWeight = weightExit;
            // Only set exitIf/exitType if no machine-side exit is defined
            if (frame.exitType.isEmpty()) {
                frame.exitIf = true;
                frame.exitType = "weight";
            }
        }

        // Parse limiter
        QJsonObject limiterObj = stepJson["limiter"].toObject();
        if (!limiterObj.isEmpty()) {
            frame.maxFlowOrPressure = toDouble(limiterObj["value"]);
            frame.maxFlowOrPressureRange = toDouble(limiterObj["range"], 0.6);
        }

        profile.m_steps.append(frame);
    }

    // Set espresso temperature from first step
    if (!profile.m_steps.isEmpty()) {
        profile.m_espressoTemperature = profile.m_steps.first().temperature;
    }

    // Count preinfusion frames
    profile.m_preinfuseFrameCount = 0;
    for (const auto& step : profile.m_steps) {
        if (step.exitIf) {
            profile.m_preinfuseFrameCount++;
        } else {
            break;
        }
    }

    qDebug() << "Loaded DE1 app profile:" << profile.m_title
             << "with" << profile.m_steps.size() << "steps";

    return profile;
}

void Profile::moveStep(int from, int to) {
    if (from < 0 || from >= m_steps.size() || to < 0 || to >= m_steps.size()) {
        return;
    }
    m_steps.move(from, to);
}

bool Profile::isValid() const {
    return !m_steps.isEmpty() && m_steps.size() <= MAX_FRAMES;
}

QStringList Profile::validationErrors() const {
    QStringList errors;

    if (m_steps.isEmpty()) {
        errors << "Profile has no steps";
    }

    if (m_steps.size() > MAX_FRAMES) {
        errors << QString("Profile has %1 steps, maximum is %2").arg(m_steps.size()).arg(MAX_FRAMES);
    }

    for (int i = 0; i < m_steps.size(); i++) {
        const auto& step = m_steps[i];
        if (step.seconds <= 0) {
            errors << QString("Step %1 has invalid duration").arg(i + 1);
        }
        if (step.temperature < 70 || step.temperature > 100) {
            errors << QString("Step %1 temperature out of range (70-100°C)").arg(i + 1);
        }
    }

    return errors;
}

QByteArray Profile::toDirectControlFrame(int frameIndex, const ProfileFrame& frame) const {
    // Generate a single frame for direct control mode
    // Same format as toFrameBytes but for live updates

    QByteArray frameData(8, 0);
    frameData[0] = static_cast<char>(frameIndex);  // FrameToWrite
    frameData[1] = static_cast<char>(frame.computeFlags());  // Flag
    frameData[2] = BinaryCodec::encodeU8P4(frame.getSetVal());  // SetVal
    frameData[3] = BinaryCodec::encodeU8P1(frame.temperature);  // Temp
    frameData[4] = BinaryCodec::encodeF8_1_7(frame.seconds);  // FrameLen
    frameData[5] = BinaryCodec::encodeU8P4(frame.getTriggerVal());  // TriggerVal

    uint16_t maxVol = BinaryCodec::encodeU10P0(frame.volume);
    frameData[6] = static_cast<char>((maxVol >> 8) & 0xFF);
    frameData[7] = static_cast<char>(maxVol & 0xFF);

    return frameData;
}

QByteArray Profile::toHeaderBytes() const {
    // Profile header: 5 bytes
    // HeaderV (1), NumberOfFrames (1), NumberOfPreinfuseFrames (1),
    // MinimumPressure (U8P4, 1), MaximumFlow (U8P4, 1)

    QByteArray header(5, 0);
    header[0] = 1;  // HeaderV
    header[1] = static_cast<char>(m_steps.size());  // NumberOfFrames
    header[2] = static_cast<char>(m_preinfuseFrameCount);  // NumberOfPreinfuseFrames
    header[3] = BinaryCodec::encodeU8P4(0.0);  // MinimumPressure (0 = no limit)
    header[4] = BinaryCodec::encodeU8P4(6.0);  // MaximumFlow (6 mL/s default)

    return header;
}

QList<QByteArray> Profile::toFrameBytes() const {
    QList<QByteArray> frames;

    // Regular frames
    for (int i = 0; i < m_steps.size(); i++) {
        const ProfileFrame& step = m_steps[i];

        // Frame: 8 bytes
        // FrameToWrite (1), Flag (1), SetVal (U8P4, 1), Temp (U8P1, 1),
        // FrameLen (F8_1_7, 1), TriggerVal (U8P4, 1), MaxVol (U10P0, 2)

        QByteArray frame(8, 0);
        frame[0] = static_cast<char>(i);  // FrameToWrite
        frame[1] = static_cast<char>(step.computeFlags());  // Flag
        frame[2] = BinaryCodec::encodeU8P4(step.getSetVal());  // SetVal
        frame[3] = BinaryCodec::encodeU8P1(step.temperature);  // Temp
        frame[4] = BinaryCodec::encodeF8_1_7(step.seconds);  // FrameLen
        frame[5] = BinaryCodec::encodeU8P4(step.getTriggerVal());  // TriggerVal

        uint16_t maxVol = BinaryCodec::encodeU10P0(step.volume);
        frame[6] = static_cast<char>((maxVol >> 8) & 0xFF);
        frame[7] = static_cast<char>(maxVol & 0xFF);

        frames.append(frame);
    }

    // Extension frames (for max flow/pressure limits)
    for (int i = 0; i < m_steps.size(); i++) {
        const ProfileFrame& step = m_steps[i];

        if (step.maxFlowOrPressure > 0) {
            QByteArray extFrame(8, 0);
            extFrame[0] = static_cast<char>(i + 32);  // FrameToWrite + 32 for extension
            extFrame[1] = BinaryCodec::encodeU8P4(step.maxFlowOrPressure);
            extFrame[2] = BinaryCodec::encodeU8P4(step.maxFlowOrPressureRange);
            // Bytes 3-7 are padding (zeros)

            frames.append(extFrame);
        }
    }

    // Tail frame
    QByteArray tailFrame(8, 0);
    tailFrame[0] = static_cast<char>(m_steps.size());  // FrameToWrite = number of frames

    uint16_t maxTotalVol = BinaryCodec::encodeU10P0(0);  // 0 = no limit
    tailFrame[1] = static_cast<char>((maxTotalVol >> 8) & 0xFF);
    tailFrame[2] = static_cast<char>(maxTotalVol & 0xFF);
    // Bytes 3-7 are padding (zeros)

    frames.append(tailFrame);

    return frames;
}

void Profile::regenerateFromRecipe() {
    if (!m_isRecipeMode) {
        return;
    }

    // Regenerate frames from recipe parameters
    m_steps = RecipeGenerator::generateFrames(m_recipeParams);

    // Update profile metadata from recipe
    m_targetWeight = m_recipeParams.targetWeight;
    m_espressoTemperature = m_recipeParams.pourTemperature;

    // Calculate preinfuse frame count (fill + bloom + infuse)
    int preinfuseCount = 1;  // Fill is always preinfuse
    if (m_recipeParams.bloomEnabled && m_recipeParams.bloomTime > 0) {
        preinfuseCount++;
    }
    if (m_recipeParams.infuseEnabled) {
        preinfuseCount++;
    }
    m_preinfuseFrameCount = preinfuseCount;
}
