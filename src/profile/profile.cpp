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

// Convert a JSON value that may be string or number to double (de1app encodes numbers as strings)
static double jsonToDouble(const QJsonValue& val, double defaultVal = 0.0) {
    if (val.isString()) {
        bool ok;
        double d = val.toString().toDouble(&ok);
        if (!ok) {
            qWarning() << "jsonToDouble: failed to parse string" << val.toString() << "- using default" << defaultVal;
        }
        return ok ? d : defaultVal;
    }
    return val.toDouble(defaultVal);
}

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

    // Preinfusion frame(s) (flow pump, exit on pressure_over)
    // When temp stepping is enabled, split into a 2-second temp boost at temp0
    // followed by remaining time at temp1 (matches de1app's temp_bump_time_seconds)
    if (preinfusionTime > 0) {
        double tempBoostDuration = 2.0;  // de1app: temp_bump_time_seconds

        if (tempStepsEnabled) {
            double boostLen = qMin(tempBoostDuration, preinfusionTime);
            double remainLen = preinfusionTime - tempBoostDuration;
            if (remainLen < 0) remainLen = 0;

            // Temp boost frame at temp0 (no flow exit)
            ProfileFrame boost;
            boost.name = "preinfusion temp boost";
            boost.temperature = temp0;
            boost.sensor = "coffee";
            boost.pump = "flow";
            boost.transition = "fast";
            boost.pressure = 1.0;
            boost.flow = preinfusionFlowRate;
            boost.seconds = boostLen;
            boost.volume = 0;
            boost.exitIf = true;
            boost.exitType = "pressure_over";
            boost.exitPressureOver = preinfusionStopPressure;
            // exitFlowOver = 0 (default) - no flow exit during temp boost
            frames.append(boost);

            // Preinfusion frame at temp1 (with flow exit)
            if (remainLen > 0) {
                ProfileFrame preinfusion;
                preinfusion.name = "preinfusion";
                preinfusion.temperature = temp1;
                preinfusion.sensor = "coffee";
                preinfusion.pump = "flow";
                preinfusion.transition = "fast";
                preinfusion.pressure = 1.0;
                preinfusion.flow = preinfusionFlowRate;
                preinfusion.seconds = remainLen;
                preinfusion.volume = 0;
                preinfusion.exitIf = true;
                preinfusion.exitType = "pressure_over";
                preinfusion.exitPressureOver = preinfusionStopPressure;
                preinfusion.exitFlowOver = 6.0;
                frames.append(preinfusion);
            }
        } else {
            // Single preinfusion frame (no temp stepping)
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
        // Match de1app: add forced rise before decline when hold was short (< 3s after
        // possible decrement) and decline is long enough to split off 3s
        if (holdTime < 3 && declineTime > 3) {
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
        qWarning() << "generatePressureProfileFrames: all time parameters are zero, adding empty fallback frame";
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

    // Preinfusion frame(s) (flow pump, exit on pressure_over)
    // When temp stepping is enabled, split into a 2-second temp boost at temp0
    // followed by remaining time at temp1 (matches de1app's temp_bump_time_seconds)
    if (preinfusionTime > 0) {
        double tempBoostDuration = 2.0;  // de1app: temp_bump_time_seconds

        if (tempStepsEnabled) {
            double boostLen = qMin(tempBoostDuration, preinfusionTime);
            double remainLen = preinfusionTime - tempBoostDuration;
            if (remainLen < 0) remainLen = 0;

            // Temp boost frame at temp0 (no flow exit)
            ProfileFrame boost;
            boost.name = "preinfusion boost";
            boost.temperature = temp0;
            boost.sensor = "coffee";
            boost.pump = "flow";
            boost.transition = "fast";
            boost.pressure = 1.0;
            boost.flow = preinfusionFlowRate;
            boost.seconds = boostLen;
            boost.volume = 0;
            boost.exitIf = true;
            boost.exitType = "pressure_over";
            boost.exitPressureOver = preinfusionStopPressure;
            // exitFlowOver = 0 (default) - no flow exit during temp boost
            frames.append(boost);

            // Preinfusion frame at temp1 (no flow exit for flow profiles)
            if (remainLen > 0) {
                ProfileFrame preinfusion;
                preinfusion.name = "preinfusion";
                preinfusion.temperature = temp1;
                preinfusion.sensor = "coffee";
                preinfusion.pump = "flow";
                preinfusion.transition = "fast";
                preinfusion.pressure = 1.0;
                preinfusion.flow = preinfusionFlowRate;
                preinfusion.seconds = remainLen;
                preinfusion.volume = 0;
                preinfusion.exitIf = true;
                preinfusion.exitType = "pressure_over";
                preinfusion.exitPressureOver = preinfusionStopPressure;
                // exitFlowOver = 0 (default) - flow profiles don't use flow exit
                frames.append(preinfusion);
            }
        } else {
            // Single preinfusion frame (no temp stepping)
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
    // de1app: decline is only generated when holdTime > 0 (not declineTime > 0)
    if (holdTime > 0) {
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
        qWarning() << "generateFlowProfileFrames: all time parameters are zero, adding empty fallback frame";
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

QString Profile::editorType() const {
    // Derived from title + profileType — matches de1app behavior.
    // Title check first (D-Flow/A-Flow), then profileType, then advanced fallback.
    QString t = m_title.startsWith(QLatin1Char('*')) ? m_title.mid(1) : m_title;
    if (t.startsWith(QStringLiteral("D-Flow"), Qt::CaseInsensitive))
        return QStringLiteral("dflow");
    if (t.startsWith(QStringLiteral("A-Flow"), Qt::CaseInsensitive))
        return QStringLiteral("aflow");
    if (m_profileType == QLatin1String("settings_2a"))
        return QStringLiteral("pressure");
    if (m_profileType == QLatin1String("settings_2b"))
        return QStringLiteral("flow");
    return QStringLiteral("advanced");
}

QJsonDocument Profile::toJson() const {
    QJsonObject obj;
    obj["title"] = m_title;
    obj["author"] = m_author;
    obj["notes"] = m_profileNotes;
    obj["beverage_type"] = m_beverageType;
    obj["version"] = QStringLiteral("2");
    obj["legacy_profile_type"] = m_profileType;
    obj["target_weight"] = m_targetWeight;
    obj["target_volume"] = m_targetVolume;
    obj["espresso_temperature"] = m_espressoTemperature;
    obj["maximum_pressure"] = m_maximumPressure;
    obj["maximum_flow"] = m_maximumFlow;
    obj["minimum_pressure"] = m_minimumPressure;
    obj["tank_desired_water_temperature"] = m_tankDesiredWaterTemperature;
    obj["maximum_flow_range_advanced"] = m_maximumFlowRangeAdvanced;
    obj["maximum_pressure_range_advanced"] = m_maximumPressureRangeAdvanced;
    obj["number_of_preinfuse_frames"] = m_preinfuseFrameCount;
    obj["has_recommended_dose"] = m_hasRecommendedDose;
    obj["recommended_dose"] = m_recommendedDose;
    obj["mode"] = (m_mode == Mode::DirectControl) ? "direct" : "frame_based";

    // Simple profile parameters (settings_2a/2b)
    if (m_profileType == "settings_2a" || m_profileType == "settings_2b") {
        obj["preinfusion_time"] = m_preinfusionTime;
        obj["preinfusion_flow_rate"] = m_preinfusionFlowRate;
        obj["preinfusion_stop_pressure"] = m_preinfusionStopPressure;
        obj["espresso_pressure"] = m_espressoPressure;
        obj["espresso_hold_time"] = m_espressoHoldTime;
        obj["espresso_decline_time"] = m_espressoDeclineTime;
        obj["pressure_end"] = m_pressureEnd;
        obj["flow_profile_hold"] = m_flowProfileHold;
        obj["flow_profile_hold_time"] = m_flowProfileHoldTime;
        obj["flow_profile_decline"] = m_flowProfileDecline;
        obj["flow_profile_decline_time"] = m_flowProfileDeclineTime;
        obj["maximum_flow_range_default"] = m_maximumFlowRangeDefault;
        obj["maximum_pressure_range_default"] = m_maximumPressureRangeDefault;
        obj["temp_steps_enabled"] = m_tempStepsEnabled;
    }

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

    // Recipe params — only write when explicitly populated (not default).
    // D-Flow/A-Flow profiles always have recipe data. Simple profiles (settings_2a/2b)
    // only have recipe data if they were edited through the recipe editor.
    QString et = editorType();
    if (et == QLatin1String("dflow") || et == QLatin1String("aflow")) {
        obj["recipe"] = m_recipeParams.toJson();
    } else if ((et == QLatin1String("pressure") || et == QLatin1String("flow"))
               && m_recipeParams.targetWeight > 0
               && m_recipeParams.editorType != EditorType::DFlow) {
        // Only write recipe for simple profiles if params were explicitly set
        // (not default DFlow params from a fresh RecipeParams())
        obj["recipe"] = m_recipeParams.toJson();
    }

    // Read-only flag (de1app compatibility: integer 0/1/2)
    if (m_readOnly != 0) {
        obj["read_only"] = m_readOnly;
    }

    return QJsonDocument(obj);
}

Profile Profile::fromJson(const QJsonDocument& doc) {
    Profile profile;
    QJsonObject obj = doc.object();

    profile.setTitle(obj["title"].toString("Default"));
    profile.m_author = obj["author"].toString();
    // Support both legacy "profile_notes" (old Decenza) and current "notes" (de1app) keys
    profile.m_profileNotes = obj["notes"].toString();
    if (profile.m_profileNotes.isEmpty()) {
        profile.m_profileNotes = obj["profile_notes"].toString();
    }
    profile.m_beverageType = obj["beverage_type"].toString("espresso");

    // Read profile type: prefer legacy_profile_type (de1app), fall back to profile_type (Decenza)
    QString profileType = obj["legacy_profile_type"].toString();
    if (profileType.isEmpty()) profileType = obj["profile_type"].toString("settings_2c");
    profile.m_profileType = profileType;

    profile.m_targetWeight = jsonToDouble(obj["target_weight"], 36.0);
    profile.m_targetVolume = jsonToDouble(obj["target_volume"], 0.0);
    profile.m_espressoTemperature = jsonToDouble(obj["espresso_temperature"], 93.0);
    profile.m_maximumPressure = jsonToDouble(obj["maximum_pressure"], 12.0);
    profile.m_maximumFlow = jsonToDouble(obj["maximum_flow"], 6.0);
    profile.m_minimumPressure = jsonToDouble(obj["minimum_pressure"], 0.0);
    profile.m_tankDesiredWaterTemperature = jsonToDouble(obj["tank_desired_water_temperature"], 0.0);
    profile.m_maximumFlowRangeAdvanced = jsonToDouble(obj["maximum_flow_range_advanced"], 0.6);
    profile.m_maximumPressureRangeAdvanced = jsonToDouble(obj["maximum_pressure_range_advanced"], 0.6);

    // Preinfuse frame count: prefer de1app key, fall back to Decenza key
    if (obj.contains("number_of_preinfuse_frames")) {
        profile.m_preinfuseFrameCount = static_cast<int>(jsonToDouble(obj["number_of_preinfuse_frames"], 0));
    } else {
        profile.m_preinfuseFrameCount = obj["preinfuse_frame_count"].toInt(0);
    }

    profile.m_hasRecommendedDose = obj["has_recommended_dose"].toBool(false);
    profile.m_recommendedDose = jsonToDouble(obj["recommended_dose"], 18.0);

    // Simple profile parameters (settings_2a/2b)
    profile.m_preinfusionTime = jsonToDouble(obj["preinfusion_time"], 5.0);
    profile.m_preinfusionFlowRate = jsonToDouble(obj["preinfusion_flow_rate"], 4.0);
    profile.m_preinfusionStopPressure = jsonToDouble(obj["preinfusion_stop_pressure"], 4.0);
    profile.m_espressoPressure = jsonToDouble(obj["espresso_pressure"], 9.2);
    profile.m_espressoHoldTime = jsonToDouble(obj["espresso_hold_time"], 10.0);
    profile.m_espressoDeclineTime = jsonToDouble(obj["espresso_decline_time"], 25.0);
    profile.m_pressureEnd = jsonToDouble(obj["pressure_end"], 4.0);
    profile.m_flowProfileHold = jsonToDouble(obj["flow_profile_hold"], 2.0);
    profile.m_flowProfileHoldTime = jsonToDouble(obj["flow_profile_hold_time"], 8.0);
    profile.m_flowProfileDeclineTime = jsonToDouble(obj["flow_profile_decline_time"], 17.0);
    profile.m_flowProfileDecline = jsonToDouble(obj["flow_profile_decline"], 1.2);
    profile.m_maximumFlowRangeDefault = jsonToDouble(obj["maximum_flow_range_default"], 1.0);
    profile.m_maximumPressureRangeDefault = jsonToDouble(obj["maximum_pressure_range_default"], 0.9);
    profile.m_tempStepsEnabled = obj["temp_steps_enabled"].toBool(false);

    QString modeStr = obj["mode"].toString("frame_based");
    profile.m_mode = (modeStr == "direct") ? Mode::DirectControl : Mode::FrameBased;

    QJsonArray tempsArray = obj["temperature_presets"].toArray();
    profile.m_temperaturePresets.clear();
    for (const auto& temp : tempsArray) {
        profile.m_temperaturePresets.append(jsonToDouble(temp));
    }
    if (profile.m_temperaturePresets.isEmpty()) {
        profile.m_temperaturePresets = {88.0, 90.0, 93.0, 96.0};
    }

    QJsonArray stepsArray = obj["steps"].toArray();
    for (const auto& stepVal : stepsArray) {
        profile.m_steps.append(ProfileFrame::fromJson(stepVal.toObject()));
    }

    // Generate frames for simple profiles when steps are empty
    // This handles built-in profiles that store scalar parameters instead of pre-generated frames
    if (profile.m_steps.isEmpty() &&
        (profile.m_profileType == "settings_2a" || profile.m_profileType == "settings_2b")) {

        double temp0 = profile.m_temperaturePresets.value(0, profile.m_espressoTemperature);
        double temp1 = profile.m_temperaturePresets.value(1, profile.m_espressoTemperature);
        double temp2 = profile.m_temperaturePresets.value(2, profile.m_espressoTemperature);
        double temp3 = profile.m_temperaturePresets.value(3, profile.m_espressoTemperature);

        if (profile.m_profileType == "settings_2a") {
            profile.m_steps = generatePressureProfileFrames(
                profile.m_preinfusionTime, profile.m_preinfusionFlowRate, profile.m_preinfusionStopPressure,
                profile.m_espressoHoldTime, profile.m_espressoPressure,
                profile.m_espressoDeclineTime, profile.m_pressureEnd,
                profile.m_maximumFlow, profile.m_maximumFlowRangeDefault,
                temp0, temp1, temp2, temp3,
                profile.m_tempStepsEnabled);
            qDebug() << "Generated" << profile.m_steps.size() << "frames from simple pressure profile (JSON)";
        } else {
            profile.m_steps = generateFlowProfileFrames(
                profile.m_preinfusionTime, profile.m_preinfusionFlowRate, profile.m_preinfusionStopPressure,
                profile.m_espressoHoldTime, profile.m_flowProfileHold,
                profile.m_espressoDeclineTime, profile.m_flowProfileDecline,
                profile.m_maximumPressure, profile.m_maximumPressureRangeDefault,
                temp0, temp1, temp2, temp3,
                profile.m_tempStepsEnabled);
            qDebug() << "Generated" << profile.m_steps.size() << "frames from simple flow profile (JSON)";
        }

        // Set preinfuse frame count based on generated preinfusion frames
        profile.m_preinfuseFrameCount = countPreinfuseFrames(profile.m_steps);
    }

    // Read-only flag (de1app compatibility: integer 0/1/2)
    profile.m_readOnly = obj["read_only"].toInt(0);

    // Load recipe params if present
    if (obj.contains("recipe")) {
        profile.m_recipeParams = RecipeParams::fromJson(obj["recipe"].toObject());
        // Infer RecipeParams.editorType from profileType/title when the recipe
        // block does not include an explicit editorType enum value
        if (!obj["recipe"].toObject().contains("editorType")) {
            if (profile.m_profileType == "settings_2a") {
                profile.m_recipeParams.editorType = EditorType::Pressure;
            } else if (profile.m_profileType == "settings_2b") {
                profile.m_recipeParams.editorType = EditorType::Flow;
            } else if (profile.m_title.startsWith(QStringLiteral("A-Flow"), Qt::CaseInsensitive) ||
                       (profile.m_title.startsWith(QLatin1Char('*')) &&
                        profile.m_title.mid(1).startsWith(QStringLiteral("A-Flow"), Qt::CaseInsensitive))) {
                profile.m_recipeParams.editorType = EditorType::AFlow;
            }
        }
    }

    // Default espresso_temperature from first frame only when the JSON omits it,
    // so we never store NaN/0. Otherwise the top-level value is authoritative at
    // load time — it can legitimately differ from steps[0].temperature (e.g. a
    // cooler group preheat target paired with a hotter preinfusion ramp).
    // (regenerateFromRecipe resyncs it from frames after recipe regeneration.)
    if (!obj.contains("espresso_temperature") && !profile.m_steps.isEmpty()) {
        profile.m_espressoTemperature = profile.m_steps.first().temperature;
    }

    // De1app defaults NumberOfPreinfuseFrames to 0 when the field is missing
    // (binary.tcl line 990: ifexists returns empty → 0). For simple profiles
    // (settings_2a/2b), de1app calculates it during frame generation
    // (pressure_to_advanced_list / flow_to_advanced_list in profile.tcl),
    // which Decenza already handles via countPreinfuseFrames() in the simple
    // profile generation block above (~line 540). Do NOT auto-calculate here
    // for advanced profiles — the profile author sets
    // final_desired_shot_volume_advanced_count_start explicitly, and we must
    // match de1app behavior for the same profile.

    return profile;
}

Profile Profile::loadFromFile(const QString& filePath) {
    // Check file extension
    if (filePath.endsWith(".tcl", Qt::CaseInsensitive)) {
        return loadFromTclFile(filePath);
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Profile::loadFromFile: Failed to open file:" << filePath
                   << "- Error:" << file.errorString();
        return Profile();
    }

    QByteArray data = file.readAll();
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (doc.isNull()) {
        qWarning() << "Profile::loadFromFile: JSON parse error:" << parseError.errorString()
                   << "at offset" << parseError.offset << "in file:" << filePath;
        return Profile();
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
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonContent.toUtf8(), &parseError);
    if (doc.isNull()) {
        qWarning() << "Profile::loadFromJsonString: JSON parse error:" << parseError.errorString()
                   << "at offset" << parseError.offset;
        return Profile();
    }
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
        // Use \b word boundary so e.g. "preinfusion_time" doesn't match
        // "flow_profile_preinfusion_time" (substring false-positive).
        const QString pattern = "\\b" + varName;
        // Try braced value first: varName {content}
        QRegularExpression reBraced(pattern + "\\s+\\{([^}]*)\\}");
        QRegularExpressionMatch match = reBraced.match(content);
        if (match.hasMatch()) {
            return match.captured(1);
        }
        // Try quoted value: varName "content"
        QRegularExpression reQuoted(pattern + "\\s+\"([^\"]*)\"");
        match = reQuoted.match(content);
        if (match.hasMatch()) {
            return match.captured(1);
        }
        // Try simple word value: varName word
        QRegularExpression reSimple(pattern + "\\s+(\\S+)");
        match = reSimple.match(content);
        return match.hasMatch() ? match.captured(1) : QString();
    };

    // Extract metadata
    profile.setTitle(extractValue("profile_title"));
    profile.m_author = extractValue("author");
    profile.m_profileNotes = extractValue("profile_notes");
    profile.m_profileType = extractValue("settings_profile_type");
    profile.m_beverageType = extractValue("beverage_type");
    if (profile.m_beverageType.isEmpty()) {
        profile.m_beverageType = "espresso";
    }

    // Read-only flag (de1app: read_only 0/1/2)
    {
        QString roVal = extractValue("read_only");
        if (!roVal.isEmpty()) profile.m_readOnly = roVal.toInt();
    }

    // Determine if this is an advanced profile (settings_2c or settings_2c2)
    bool isAdvancedProfile = profile.m_profileType.startsWith("settings_2c");

    // Extract target weight/volume — de1app uses different keys based on profile type
    // (no cross-key fallback, matching de1app behavior):
    // Simple profiles (settings_2a/2b): final_desired_shot_weight, final_desired_shot_volume
    // Advanced profiles (settings_2c/2c2): final_desired_shot_weight_advanced, final_desired_shot_volume_advanced
    QString val;
    val = extractValue(isAdvancedProfile ? "final_desired_shot_weight_advanced" : "final_desired_shot_weight");
    if (!val.isEmpty()) profile.m_targetWeight = val.toDouble();

    val = extractValue(isAdvancedProfile ? "final_desired_shot_volume_advanced" : "final_desired_shot_volume");
    if (!val.isEmpty()) profile.m_targetVolume = val.toDouble();

    val = extractValue("espresso_temperature");
    if (!val.isEmpty()) profile.m_espressoTemperature = val.toDouble();

    // Extract flow/pressure limits
    val = extractValue("maximum_flow");
    if (!val.isEmpty()) profile.m_maximumFlow = val.toDouble();

    val = extractValue("maximum_pressure");
    if (!val.isEmpty()) profile.m_maximumPressure = val.toDouble();

    val = extractValue("flow_profile_minimum_pressure");
    if (!val.isEmpty()) profile.m_minimumPressure = val.toDouble();

    val = extractValue("tank_desired_water_temperature");
    if (!val.isEmpty()) profile.m_tankDesiredWaterTemperature = val.toDouble();

    val = extractValue("maximum_flow_range_advanced");
    if (!val.isEmpty()) profile.m_maximumFlowRangeAdvanced = val.toDouble();

    val = extractValue("maximum_pressure_range_advanced");
    if (!val.isEmpty()) profile.m_maximumPressureRangeAdvanced = val.toDouble();

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
        // Extract simple profile parameters and store them for JSON serialization
        double preinfusionTime = extractValue("preinfusion_time").toDouble();
        double preinfusionFlowRate = extractValue("preinfusion_flow_rate").toDouble();
        double preinfusionStopPressure = extractValue("preinfusion_stop_pressure").toDouble();
        double holdTime = extractValue("espresso_hold_time").toDouble();
        double declineTime = extractValue("espresso_decline_time").toDouble();
        bool tempStepsEnabled = extractValue("espresso_temperature_steps_enabled").toInt() == 1;

        // Store scalar params so they persist when saved to JSON
        profile.m_preinfusionTime = preinfusionTime;
        profile.m_preinfusionFlowRate = preinfusionFlowRate;
        profile.m_preinfusionStopPressure = preinfusionStopPressure;
        profile.m_espressoHoldTime = holdTime;
        profile.m_espressoDeclineTime = declineTime;
        profile.m_tempStepsEnabled = tempStepsEnabled;

        // Temperature presets
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
            if (qFuzzyIsNull(maximumFlowRange)) maximumFlowRange = 1.0;

            // Store pressure-specific params
            profile.m_espressoPressure = espressoPressure;
            profile.m_pressureEnd = pressureEnd;
            profile.m_maximumFlowRangeDefault = maximumFlowRange;

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
            if (qFuzzyIsNull(maximumPressureRange)) maximumPressureRange = 0.9;

            // Store flow-specific params
            profile.m_flowProfileHold = flowHold;
            profile.m_flowProfileHoldTime = extractValue("flow_profile_hold_time").toDouble();
            profile.m_flowProfileDecline = flowDecline;
            profile.m_flowProfileDeclineTime = extractValue("flow_profile_decline_time").toDouble();
            profile.m_maximumPressureRangeDefault = maximumPressureRange;

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
    if (qFuzzyIsNull(profile.m_espressoTemperature) && !profile.m_steps.isEmpty()) {
        profile.m_espressoTemperature = profile.m_steps.first().temperature;
    }

    // Read preinfuse frame count from TCL data
    // de1app uses "final_desired_shot_volume_advanced_count_start" as NumberOfPreinfuseFrames
    // (binary.tcl line 990). Default to 0 when missing, matching de1app's ifexists behavior.
    // For simple TCL profiles (settings_2a/2b), de1app always includes this field
    // (set by pressure_to_advanced_list / flow_to_advanced_list in profile.tcl).
    val = extractValue("final_desired_shot_volume_advanced_count_start");
    profile.m_preinfuseFrameCount = val.isEmpty() ? 0 : val.toInt();

    qDebug() << "Loaded Tcl profile:" << profile.m_title
             << "with" << profile.m_steps.size() << "steps";

    return profile;
}

void Profile::moveStep(int from, int to) {
    if (from < 0 || from >= m_steps.size() || to < 0 || to >= m_steps.size()) {
        qWarning() << "Cannot move step: invalid indices from" << from << "to" << to << "(size:" << m_steps.size() << ")";
        return;
    }
    m_steps.move(from, to);
}

bool Profile::isValid() const {
    return !m_steps.isEmpty() && m_steps.size() <= MAX_FRAMES;
}

bool Profile::functionallyEqual(const Profile& a, const Profile& b)
{
    if (a.steps().isEmpty() || b.steps().isEmpty())
        return false;

    // Profile-level limits (maximumPressure, maximumFlow, etc.) are intentionally
    // NOT compared here. The built-in JSONs may have stale/zero values for these
    // fields from older writes, while TCL files carry the original de1app defaults.
    // Import identity is determined by the extraction frame sequence, not global limits.
    //
    // preinfuseFrameCount IS compared because it is sent to the DE1 as
    // NumberOfPreinfuseFrames in the BLE header and directly affects machine behavior.
    if (a.preinfuseFrameCount() != b.preinfuseFrameCount()) return false;

    const auto& stepsA = a.steps();
    const auto& stepsB = b.steps();
    if (stepsA.size() != stepsB.size()) return false;

    for (qsizetype i = 0; i < stepsA.size(); i++) {
        const ProfileFrame& fa = stepsA[i];
        const ProfileFrame& fb = stepsB[i];

        if (qAbs(fa.temperature - fb.temperature) > 0.1) return false;
        if (fa.sensor     != fb.sensor)     return false;
        if (fa.pump       != fb.pump)       return false;
        if (fa.transition != fb.transition) return false;
        // Always compare the active axis. For the inactive axis, skip comparison
        // when either side is 0 — de1app TCL stores a default value (e.g. flow=2
        // on pressure frames) that our JSON writer omits (stored as 0). If both
        // sides are non-zero the value was explicitly set and must match.
        if (fa.pump == "pressure") {
            if (qAbs(fa.pressure - fb.pressure) > 0.1) return false;
            if (fa.flow > 0.1 && fb.flow > 0.1 && qAbs(fa.flow - fb.flow) > 0.1) return false;
        } else {
            if (qAbs(fa.flow - fb.flow) > 0.1) return false;
            if (fa.pressure > 0.1 && fb.pressure > 0.1 && qAbs(fa.pressure - fb.pressure) > 0.1) return false;
        }
        if (qAbs(fa.seconds  - fb.seconds)  > 0.1) return false;
        if (qAbs(fa.volume   - fb.volume)   > 0.1) return false;
        if (fa.popup != fb.popup) return false;

        if (fa.exitIf != fb.exitIf) return false;
        if (fa.exitIf) {
            if (fa.exitType != fb.exitType) return false;
            // Only compare the threshold field for the active exit type.
            // TCL sets exit_flow_over=6 as a safety cap on pressure frames; JSON only
            // serialises the relevant field — comparing all four causes false mismatches.
            if (fa.exitType == "pressure_over")  { if (qAbs(fa.exitPressureOver  - fb.exitPressureOver)  > 0.1) return false; }
            else if (fa.exitType == "pressure_under") { if (qAbs(fa.exitPressureUnder - fb.exitPressureUnder) > 0.1) return false; }
            else if (fa.exitType == "flow_over")  { if (qAbs(fa.exitFlowOver     - fb.exitFlowOver)     > 0.1) return false; }
            else if (fa.exitType == "flow_under") { if (qAbs(fa.exitFlowUnder    - fb.exitFlowUnder)    > 0.1) return false; }
        }

        if (qAbs(fa.exitWeight          - fb.exitWeight)          > 0.1) return false;
        if (qAbs(fa.maxFlowOrPressure   - fb.maxFlowOrPressure)   > 0.1) return false;
        if (qAbs(fa.maxFlowOrPressureRange - fb.maxFlowOrPressureRange) > 0.1) return false;
    }

    return true;
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
        if (step.seconds < 0) {
            errors << QString("Step %1 has negative duration").arg(i + 1);
        }
        if (step.temperature < 70 || step.temperature > 100) {
            errors << QString("Step %1 temperature out of range (70-100°C)").arg(i + 1);
        }
    }

    return errors;
}

QString Profile::describeFrames() const
{
    if (m_steps.isEmpty()) return QString();

    // Compact format: one dense line per frame to minimise AI token usage
    // while keeping diagnostically useful info (control mode, setpoint, temp, transitions, exits, limiters).
    QString result;
    QTextStream out(&result);
    out << "## Profile Recipe (" << m_steps.size() << " frames)\n\n";

    for (int i = 0; i < m_steps.size(); i++) {
        const auto& f = m_steps[i];
        bool isFlow = f.isFlowControl();

        out << (i + 1) << ". ";
        if (!f.name.isEmpty())
            out << f.name << " ";
        out << "(" << QString::number(f.seconds, 'f', 0) << "s) ";
        if (isFlow)
            out << "FLOW " << QString::number(f.flow, 'f', 1) << "ml/s";
        else
            out << "PRESSURE " << QString::number(f.pressure, 'f', 1) << "bar";
        out << " " << QString::number(f.temperature, 'f', 0) << "\u00B0C";

        // Smooth transition from previous frame — shows intended ramp direction.
        // Critical for lever/d-flow profiles where control mode switches (e.g. pressure→flow).
        if (i > 0 && f.transition == "smooth") {
            const auto& prev = m_steps[i - 1];
            bool prevIsFlow = prev.isFlowControl();
            if (prevIsFlow != isFlow) {
                // Control mode switch: show what we're transitioning from
                if (prevIsFlow)
                    out << " (from FLOW " << QString::number(prev.flow, 'f', 1) << "ml/s)";
                else
                    out << " (from PRESSURE " << QString::number(prev.pressure, 'f', 1) << "bar)";
            } else {
                // Same control mode but ramping value
                double prevVal = isFlow ? prev.flow : prev.pressure;
                double curVal = isFlow ? f.flow : f.pressure;
                if (std::abs(prevVal - curVal) > 0.1) {
                    out << " (ramp from " << QString::number(prevVal, 'f', 1) << ")";
                }
            }
        }

        // Exit conditions — compact
        if (f.exitIf) {
            if (f.exitType == "pressure_over" && f.exitPressureOver > 0)
                out << " exit:p>" << QString::number(f.exitPressureOver, 'f', 1);
            else if (f.exitType == "pressure_under" && f.exitPressureUnder > 0)
                out << " exit:p<" << QString::number(f.exitPressureUnder, 'f', 1);
            else if (f.exitType == "flow_over" && f.exitFlowOver > 0)
                out << " exit:f>" << QString::number(f.exitFlowOver, 'f', 1);
            else if (f.exitType == "flow_under" && f.exitFlowUnder > 0)
                out << " exit:f<" << QString::number(f.exitFlowUnder, 'f', 1);
        }
        if (f.exitWeight > 0)
            out << " exit:w" << QString::number(f.exitWeight, 'f', 1) << "g";

        // Limiter — just the value, skip range
        if (f.maxFlowOrPressure > 0)
            out << " lim:" << QString::number(f.maxFlowOrPressure, 'f', 1)
                << (isFlow ? "bar" : "ml/s");

        out << "\n";
    }

    return result;
}

QString Profile::describeFramesFromJson(const QString& json)
{
    if (json.isEmpty()) return QString();
    Profile p = Profile::loadFromJsonString(json);
    if (p.steps().isEmpty()) {
        // Distinguish between valid profile with no steps vs parse failure
        if (p.title().isEmpty()) {
            qWarning() << "Profile::describeFramesFromJson: Could not parse profile JSON";
            return QStringLiteral("(Profile recipe not available — stored profile data could not be parsed)\n");
        }
        return QString();
    }
    return p.describeFrames();
}

int Profile::countPreinfuseFrames(const QList<ProfileFrame>& steps) {
    int count = 0;
    for (const auto& step : steps) {
        if (step.exitIf) {
            count++;
        } else {
            break;
        }
    }
    return count;
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
    // De1app defaults to IgnoreLimit, so MinimumPressure and MaximumFlow are not
    // used as constraints. Hardcode to match de1app (binary.tcl:867-868).
    header[3] = 0;  // MinimumPressure
    header[4] = BinaryCodec::encodeU8P4(6.0);  // MaximumFlow

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

void Profile::regenerateSimpleFrames() {
    double temp0 = m_temperaturePresets.value(0, m_espressoTemperature);
    double temp1 = m_temperaturePresets.value(1, m_espressoTemperature);
    double temp2 = m_temperaturePresets.value(2, m_espressoTemperature);
    double temp3 = m_temperaturePresets.value(3, m_espressoTemperature);

    if (m_profileType == QLatin1String("settings_2a")) {
        m_steps = generatePressureProfileFrames(
            m_preinfusionTime, m_preinfusionFlowRate, m_preinfusionStopPressure,
            m_espressoHoldTime, m_espressoPressure,
            m_espressoDeclineTime, m_pressureEnd,
            m_maximumFlow, m_maximumFlowRangeDefault,
            temp0, temp1, temp2, temp3,
            m_tempStepsEnabled);
    } else if (m_profileType == QLatin1String("settings_2b")) {
        m_steps = generateFlowProfileFrames(
            m_preinfusionTime, m_preinfusionFlowRate, m_preinfusionStopPressure,
            m_espressoHoldTime, m_flowProfileHold,
            m_espressoDeclineTime, m_flowProfileDecline,
            m_maximumPressure, m_maximumPressureRangeDefault,
            temp0, temp1, temp2, temp3,
            m_tempStepsEnabled);
    } else {
        qWarning() << "regenerateSimpleFrames called on non-simple profile type:" << m_profileType;
        return;
    }

    m_preinfuseFrameCount = countPreinfuseFrames(m_steps);

    // Do NOT sync m_espressoTemperature from first frame here.
    // The caller (applyRecipeToScalarFields) already set it from tempStart.
    // Syncing from the first frame is wrong when preinfusionTime=0 and
    // tempStepsEnabled=true — the first frame would be the hold frame at
    // temp2, not temp0.
}

void Profile::regenerateFromRecipe() {
    if (editorType() == QLatin1String("advanced")) {
        return;
    }

    // Save old frames so we can preserve passthrough fields after regeneration
    QList<ProfileFrame> oldSteps = m_steps;

    // Regenerate frames from recipe parameters
    m_steps = RecipeGenerator::generateFrames(m_recipeParams);

    if (m_steps.size() == 1 && m_steps[0].name == "empty") {
        qWarning() << "regenerateFromRecipe: recipe produced fallback empty frame"
                   << "- check recipe parameters for" << m_title;
    }

    // Preserve passthrough fields (volume cap, fill weight safety exit) from old frames.
    // RecipeParams controls volume/exitWeight only on infuse frames ("Infusing"/"Infuse");
    // for all other frames these fields are hardcoded defaults in RecipeGenerator, so we
    // restore the stored values to avoid silently dropping them on each save (issue #331).
    if (!oldSteps.isEmpty()) {
        for (ProfileFrame& newFrame : m_steps) {
            // Skip infuse frames — RecipeParams is authoritative for their volume/exitWeight
            if (newFrame.name == "Infusing" || newFrame.name == "Infuse")
                continue;
            for (const ProfileFrame& oldFrame : oldSteps) {
                if (oldFrame.name == newFrame.name) {
                    newFrame.volume = oldFrame.volume;
                    newFrame.exitWeight = oldFrame.exitWeight;
                    break;
                }
            }
        }
    }

    // Update profile metadata from recipe
    m_targetWeight = m_recipeParams.targetWeight;
    m_targetVolume = m_recipeParams.targetVolume;
    // Use first frame temperature (matches de1app behavior)
    if (!m_steps.isEmpty()) {
        m_espressoTemperature = m_steps.first().temperature;
    }

    // De1app recomputes preinfuseFrameCount for simple profiles (pressure_to_advanced_list,
    // flow_to_advanced_list rebuild frames and count each time) but preserves it for advanced
    // profiles (settings_to_advanced_list copies as-is). D-Flow/A-Flow are advanced profiles.
    if (m_recipeParams.editorType == EditorType::Pressure
        || m_recipeParams.editorType == EditorType::Flow) {
        m_preinfuseFrameCount = countPreinfuseFrames(m_steps);
    }
}
