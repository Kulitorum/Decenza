#include "profileeditmodel.h"

#include "profileanalyzer.h"
#include "profilegenerator.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
QVariantMap frameToMap(const ProfileFrame& frame)
{
    return {{QStringLiteral("name"), frame.name},
            {QStringLiteral("temperature"), frame.temperature},
            {QStringLiteral("sensor"), frame.sensor},
            {QStringLiteral("pump"), frame.pump},
            {QStringLiteral("transition"), frame.transition},
            {QStringLiteral("pressure"), frame.pressure},
            {QStringLiteral("flow"), frame.flow},
            {QStringLiteral("seconds"), frame.seconds},
            {QStringLiteral("volume"), frame.volume},
            {QStringLiteral("exit_if"), frame.exitIf},
            {QStringLiteral("exit_type"), frame.exitType},
            {QStringLiteral("exit_pressure_over"), frame.exitPressureOver},
            {QStringLiteral("exit_pressure_under"), frame.exitPressureUnder},
            {QStringLiteral("exit_flow_over"), frame.exitFlowOver},
            {QStringLiteral("exit_flow_under"), frame.exitFlowUnder},
            {QStringLiteral("exit_weight"), frame.exitWeight},
            {QStringLiteral("popup"), frame.popup},
            {QStringLiteral("max_flow_or_pressure"), frame.maxFlowOrPressure},
            {QStringLiteral("max_flow_or_pressure_range"), frame.maxFlowOrPressureRange}};
}

ProfileFrame frameFromMap(const QVariantMap& value)
{
    ProfileFrame frame;
    frame.name = value.value(QStringLiteral("name")).toString();
    frame.temperature = value.value(QStringLiteral("temperature"), 93.0).toDouble();
    frame.sensor = value.value(QStringLiteral("sensor"), QStringLiteral("coffee")).toString();
    frame.pump = value.value(QStringLiteral("pump"), QStringLiteral("pressure")).toString();
    frame.transition = value.value(QStringLiteral("transition"), QStringLiteral("fast")).toString();
    frame.pressure = value.value(QStringLiteral("pressure"), 9.0).toDouble();
    frame.flow = value.value(QStringLiteral("flow"), 2.0).toDouble();
    frame.seconds = value.value(QStringLiteral("seconds"), 30.0).toDouble();
    frame.volume = value.value(QStringLiteral("volume"), 0.0).toDouble();
    frame.exitIf = value.value(QStringLiteral("exit_if")).toBool();
    frame.exitType = value.value(QStringLiteral("exit_type")).toString();
    frame.exitPressureOver = value.value(QStringLiteral("exit_pressure_over")).toDouble();
    frame.exitPressureUnder = value.value(QStringLiteral("exit_pressure_under")).toDouble();
    frame.exitFlowOver = value.value(QStringLiteral("exit_flow_over")).toDouble();
    frame.exitFlowUnder = value.value(QStringLiteral("exit_flow_under")).toDouble();
    frame.exitWeight = value.value(QStringLiteral("exit_weight")).toDouble();
    frame.popup = value.value(QStringLiteral("popup")).toString();
    frame.maxFlowOrPressure = value.value(QStringLiteral("max_flow_or_pressure")).toDouble();
    frame.maxFlowOrPressureRange = value.value(QStringLiteral("max_flow_or_pressure_range"), 0.6).toDouble();
    return frame;
}

void setIfPresent(QJsonObject& object, const QVariantMap& values, const char* key)
{
    const QString name = QString::fromLatin1(key);
    if (values.contains(name)) object.insert(name, QJsonValue::fromVariant(values.value(name)));
}
}

QVariantMap ProfileEditModel::toVariantMap(const Profile& profile)
{
    QVariantMap result;
    const QJsonObject json = profile.toJsonObject();
    for (auto it = json.constBegin(); it != json.constEnd(); ++it) {
        if (it.key() != QLatin1String("steps"))
            result.insert(it.key(), it.value().toVariant());
    }
    // The file/interop JSON format deliberately stores numeric fields as
    // strings for ecosystem compatibility. This map is the QML-facing model,
    // whose historical ProfileManager contract exposed these as numbers.
    static const QStringList numericFields{
        QStringLiteral("target_weight"), QStringLiteral("target_volume"),
        QStringLiteral("espresso_temperature"), QStringLiteral("maximum_pressure"),
        QStringLiteral("maximum_flow"), QStringLiteral("minimum_pressure"),
        QStringLiteral("tank_desired_water_temperature"), QStringLiteral("tank_temperature"),
        QStringLiteral("maximum_flow_range_advanced"), QStringLiteral("maximum_pressure_range_advanced"),
        QStringLiteral("recommended_dose"), QStringLiteral("preinfusion_time"),
        QStringLiteral("preinfusion_flow_rate"), QStringLiteral("preinfusion_stop_pressure"),
        QStringLiteral("espresso_pressure"), QStringLiteral("espresso_hold_time"),
        QStringLiteral("espresso_decline_time"), QStringLiteral("pressure_end"),
        QStringLiteral("flow_profile_hold"), QStringLiteral("flow_profile_hold_time"),
        QStringLiteral("flow_profile_decline"), QStringLiteral("flow_profile_decline_time"),
        QStringLiteral("maximum_flow_range_default"), QStringLiteral("maximum_pressure_range_default")
    };
    for (const QString& key : numericFields) {
        const QJsonValue value = json.value(key);
        if (value.isString())
            result.insert(key, profileJsonToDouble(value));
    }
    if (json.value(QStringLiteral("temperature_presets")).isArray()) {
        QVariantList presets;
        const QJsonArray values = json.value(QStringLiteral("temperature_presets")).toArray();
        presets.reserve(values.size());
        for (const QJsonValue& value : values)
            presets.append(profileJsonToDouble(value));
        result.insert(QStringLiteral("temperature_presets"), presets);
    }
    result.insert(QStringLiteral("profile_type"), profile.profileType());
    result.insert(QStringLiteral("beverage_type"), profile.beverageType());
    result.insert(QStringLiteral("editorType"), profile.editorType());
    result.insert(QStringLiteral("profile_notes"), profile.profileNotes());
    result.insert(QStringLiteral("mode"), profile.mode() == Profile::Mode::FrameBased
                      ? QStringLiteral("frame_based") : QStringLiteral("direct"));
    result.insert(QStringLiteral("maximum_flow_range_advanced"), profile.maximumFlowRangeAdvanced());
    result.insert(QStringLiteral("maximum_pressure_range_advanced"), profile.maximumPressureRangeAdvanced());
    result.insert(QStringLiteral("preinfuse_frame_count"), profile.preinfuseFrameCount());

    QVariantList steps;
    steps.reserve(profile.steps().size());
    for (const ProfileFrame& frame : profile.steps())
        steps.append(frameToMap(frame));
    result.insert(QStringLiteral("steps"), steps);
    // This source document carries ecosystem fields that the editor does not
    // expose. The draft reuses it as its lossless base when applying UI edits.
    result.insert(QStringLiteral("_profileJson"), profile.toJsonString());
    return result;
}

Profile ProfileEditModel::fromVariantMap(const QVariantMap& values, bool* ok)
{
    bool parsed = false;
    Profile profile;
    const QByteArray source = values.value(QStringLiteral("_profileJson")).toString().toUtf8();
    if (!source.isEmpty()) {
        const QJsonDocument document = QJsonDocument::fromJson(source);
        if (document.isObject()) {
            profile = Profile::fromJson(document);
            parsed = true;
        }
    }
    if (!parsed && values.contains(QStringLiteral("title"))) {
        QJsonObject object;
        for (const char* key : {"title", "author", "notes", "profile_notes", "beverage_type",
                                "profile_type", "legacy_profile_type", "target_weight", "target_volume",
                                "espresso_temperature", "maximum_pressure", "maximum_flow", "minimum_pressure",
                                "tank_desired_water_temperature", "tank_temperature", "maximum_flow_range_advanced",
                                "maximum_pressure_range_advanced", "number_of_preinfuse_frames", "preinfuse_frame_count",
                                "has_recommended_dose", "recommended_dose", "mode", "preinfusion_time",
                                "preinfusion_flow_rate", "preinfusion_stop_pressure", "espresso_pressure",
                                "espresso_hold_time", "espresso_decline_time", "pressure_end", "flow_profile_hold",
                                "flow_profile_hold_time", "flow_profile_decline", "flow_profile_decline_time",
                                "maximum_flow_range_default", "maximum_pressure_range_default", "temp_steps_enabled"})
            setIfPresent(object, values, key);
        if (values.contains(QStringLiteral("steps"))) {
            QJsonArray frames;
            for (const QVariant& step : values.value(QStringLiteral("steps")).toList())
                frames.append(frameFromMap(step.toMap()).toJson());
            object.insert(QStringLiteral("steps"), frames);
        }
        profile = Profile::fromJson(QJsonDocument(object));
        parsed = true;
    }
    if (parsed) {
        // Apply exposed properties over the source document. Unknown top-level
        // profile fields remain preserved by Profile's canonical JSON reader.
        QJsonObject object = profile.toJsonObject();
        for (const char* key : {"title", "author", "beverage_type", "target_weight", "target_volume",
                                "espresso_temperature", "maximum_pressure", "maximum_flow", "minimum_pressure",
                                "tank_desired_water_temperature", "maximum_flow_range_advanced",
                                "maximum_pressure_range_advanced", "has_recommended_dose", "recommended_dose",
                                "preinfusion_time", "preinfusion_flow_rate", "preinfusion_stop_pressure",
                                "espresso_pressure", "espresso_hold_time", "espresso_decline_time", "pressure_end",
                                "flow_profile_hold", "flow_profile_hold_time", "flow_profile_decline",
                                "flow_profile_decline_time", "maximum_flow_range_default", "maximum_pressure_range_default",
                                "temp_steps_enabled", "mode"})
            setIfPresent(object, values, key);
        if (values.contains(QStringLiteral("profile_type")))
            object.insert(QStringLiteral("legacy_profile_type"), values.value(QStringLiteral("profile_type")).toString());
        if (values.contains(QStringLiteral("preinfuse_frame_count")))
            object.insert(QStringLiteral("number_of_preinfuse_frames"), values.value(QStringLiteral("preinfuse_frame_count")).toInt());
        if (values.contains(QStringLiteral("profile_notes")))
            object.insert(QStringLiteral("notes"), values.value(QStringLiteral("profile_notes")).toString());
        if (values.contains(QStringLiteral("steps"))) {
            QJsonArray frames;
            for (const QVariant& step : values.value(QStringLiteral("steps")).toList())
                frames.append(frameFromMap(step.toMap()).toJson());
            object.insert(QStringLiteral("steps"), frames);
        }
        profile = Profile::fromJson(QJsonDocument(object));
    }
    if (ok) *ok = parsed;
    return profile;
}

bool ProfileEditModel::applyParams(Profile& profile, const QVariantMap& values,
                                   QStringList* validationIssues)
{
    ProfileParams params = ProfileParams::fromVariantMap(values);
    const QStringList issues = params.validate();
    if (validationIssues) *validationIssues = issues;
    params.clamp();
    const QString& type = profile.profileType();
    const bool simple = type == QLatin1String("settings_2a") || type == QLatin1String("settings_2b");
    if (simple) {
        const auto presets = QList<double>{params.tempStart, params.tempPreinfuse,
                                           params.tempHold, params.tempDecline};
        profile.setPreinfusionTime(params.preinfusionTime);
        profile.setPreinfusionFlowRate(params.preinfusionFlowRate);
        profile.setPreinfusionStopPressure(params.preinfusionStopPressure);
        profile.setTemperaturePresets(presets);
        const bool tempsDiffer = !qFuzzyCompare(params.tempStart, params.tempPreinfuse)
            || !qFuzzyCompare(params.tempStart, params.tempHold)
            || !qFuzzyCompare(params.tempStart, params.tempDecline);
        profile.setTempStepsEnabled(tempsDiffer);
        profile.setEspressoHoldTime(params.holdTime);
        profile.setEspressoDeclineTime(params.simpleDeclineTime);
        if (type == QLatin1String("settings_2a")) {
            profile.setEspressoPressure(params.espressoPressure);
            profile.setPressureEnd(params.pressureEnd);
            profile.setMaximumFlow(params.limiterValue);
            profile.setMaximumFlowRangeDefault(params.limiterRange);
        } else {
            profile.setFlowProfileHoldTime(params.holdTime);
            profile.setFlowProfileDeclineTime(params.simpleDeclineTime);
            profile.setFlowProfileHold(params.holdFlow);
            profile.setFlowProfileDecline(params.flowEnd);
            profile.setMaximumPressure(params.limiterValue);
            profile.setMaximumPressureRangeDefault(params.limiterRange);
        }
        profile.setEspressoTemperature(params.tempStart);
        profile.regenerateSimpleFrames();
        profile.setTargetWeight(params.targetWeight);
        profile.setTargetVolume(params.targetVolume);
        return false;
    }

    const QString editorType = profile.editorType();
    const bool derivesFromFrames = editorType == QLatin1String("dflow")
        || editorType == QLatin1String("aflow");
    const ProfileParams oldParams = derivesFromFrames
        ? ProfileAnalyzer::extractProfileParams(profile) : profile.profileParams();
    bool needFrameRegen = profile.steps().isEmpty()
        || !oldParams.frameAffectingFieldsEqual(params);
    const bool fits = profile.steps().isEmpty() || ProfileAnalyzer::framesFitEditorLayout(profile);
    const bool refusedRegen = needFrameRegen && !fits;
    if (refusedRegen) needFrameRegen = false;
    profile.setProfileParams(params);
    if (needFrameRegen) profile.regenerateFromParams();
    else {
        profile.setTargetWeight(params.targetWeight);
        profile.setTargetVolume(params.targetVolume);
    }
    return refusedRegen;
}

QVariantMap ProfileEditModel::paramsForProfile(const Profile& profile)
{
    const QString editorType = profile.editorType();
    if (editorType == QLatin1String("dflow") || editorType == QLatin1String("aflow")) {
        bool derived = false;
        QVariantMap out = ProfileAnalyzer::extractProfileParams(profile, &derived).toVariantMap();
        if (!derived) {
            out.insert(QStringLiteral("parametersDerivedFromFrames"), false);
            out.insert(QStringLiteral("warning"),
                       QStringLiteral("This profile's %1 frames do not fit the layout its editor reads, so these values were not derived from it.")
                           .arg(profile.steps().size()));
        }
        return out;
    }
    const QString& type = profile.profileType();
    if (type == QLatin1String("settings_2a") || type == QLatin1String("settings_2b")) {
        ProfileParams params;
        params.targetWeight = profile.targetWeight();
        params.targetVolume = profile.targetVolume();
        params.fillTemperature = profile.espressoTemperature();
        params.pourTemperature = profile.espressoTemperature();
        const auto presets = profile.temperaturePresets();
        const double baseTemp = profile.espressoTemperature();
        params.tempStart = presets.value(0, baseTemp);
        params.tempPreinfuse = presets.value(1, baseTemp);
        params.tempHold = presets.value(2, baseTemp);
        params.tempDecline = presets.value(3, baseTemp);
        params.preinfusionTime = profile.preinfusionTime();
        params.preinfusionFlowRate = profile.preinfusionFlowRate();
        params.preinfusionStopPressure = profile.preinfusionStopPressure();
        params.holdTime = profile.espressoHoldTime();
        params.simpleDeclineTime = profile.espressoDeclineTime();
        if (type == QLatin1String("settings_2a")) {
            params.editorType = EditorType::Pressure;
            params.espressoPressure = profile.espressoPressure();
            params.pressureEnd = profile.pressureEnd();
            params.limiterValue = profile.maximumFlow();
            params.limiterRange = profile.maximumFlowRangeDefault();
        } else {
            params.editorType = EditorType::Flow;
            params.holdFlow = profile.flowProfileHold();
            params.flowEnd = profile.flowProfileDecline();
            params.limiterValue = profile.maximumPressure();
            params.limiterRange = profile.maximumPressureRangeDefault();
        }
        return params.toVariantMap();
    }
    return ProfileParams().toVariantMap();
}

Profile ProfileEditModel::newEditorProfile(const QString& editorType, const QString& title)
{
    if (editorType == QLatin1String("advanced")) {
        Profile profile;
        profile.setTitle(title);
        profile.setAuthor(QString());
        profile.setProfileNotes(QString());
        profile.setBeverageType(QStringLiteral("espresso"));
        profile.setProfileType(QStringLiteral("settings_2c"));
        profile.setTargetWeight(36.0);
        profile.setTargetVolume(0.0);
        profile.setEspressoTemperature(93.0);
        ProfileFrame frame;
        frame.name = QStringLiteral("Extraction");
        frame.temperature = 93.0;
        frame.sensor = QStringLiteral("coffee");
        frame.pump = QStringLiteral("pressure");
        frame.transition = QStringLiteral("fast");
        frame.pressure = 9.0;
        frame.flow = 2.0;
        frame.seconds = 60.0;
        profile.addStep(frame);
        return profile;
    }

    EditorType type = EditorType::DFlow;
    if (editorType == QLatin1String("aflow")) type = EditorType::AFlow;
    else if (editorType == QLatin1String("pressure")) type = EditorType::Pressure;
    else if (editorType == QLatin1String("flow")) type = EditorType::Flow;
    ProfileParams params;
    params.editorType = type;
    params.applyEditorDefaults();
    params.clamp();
    Profile profile = ProfileGenerator::createProfile(params, title);
    if (type == EditorType::Pressure || type == EditorType::Flow) {
        // Simple editors read the legacy scalar fields. Initialize them through
        // the same edit path that generates their frames, so opening a new
        // draft cannot show different settings from its graph.
        applyParams(profile, params.toVariantMap());
    }
    return profile;
}

bool ProfileEditModel::addFrame(Profile& profile, int afterIndex)
{
    if (profile.steps().size() >= Profile::MAX_FRAMES) return false;
    ProfileFrame frame;
    frame.name = QStringLiteral("Step %1").arg(profile.steps().size() + 1);
    frame.temperature = 93.0;
    frame.sensor = QStringLiteral("coffee");
    frame.pump = QStringLiteral("pressure");
    frame.transition = QStringLiteral("fast");
    frame.pressure = 9.0;
    frame.flow = 2.0;
    frame.seconds = 30.0;
    frame.maxFlowOrPressure = Profile::kDefaultPressureFlowLimit;
    frame.maxFlowOrPressureRange = profile.maximumFlowRangeAdvanced() > 0.0
        ? profile.maximumFlowRangeAdvanced() : 0.6;
    if (afterIndex < 0 || afterIndex >= profile.steps().size()) return profile.addStep(frame);
    return profile.insertStep(afterIndex + 1, frame);
}

bool ProfileEditModel::deleteFrame(Profile& profile, int index)
{
    return profile.steps().size() > 1 && index >= 0 && index < profile.steps().size()
        && profile.removeStep(index);
}

bool ProfileEditModel::moveFrame(Profile& profile, int index, int delta)
{
    const int target = index + delta;
    if (delta != -1 && delta != 1) return false;
    if (index < 0 || index >= profile.steps().size() || target < 0 || target >= profile.steps().size())
        return false;
    profile.moveStep(index, target);
    return true;
}

bool ProfileEditModel::duplicateFrame(Profile& profile, int index)
{
    if (index < 0 || index >= profile.steps().size() || profile.steps().size() >= Profile::MAX_FRAMES)
        return false;
    ProfileFrame copy = profile.steps().at(index);
    copy.name += QStringLiteral(" (copy)");
    return profile.insertStep(index + 1, copy);
}

bool ProfileEditModel::setFrameProperty(Profile& profile, int index, const QString& property,
                                        const QVariant& value)
{
    if (index < 0 || index >= profile.steps().size()) return false;
    ProfileFrame frame = profile.steps().at(index);
    if (property == QLatin1String("name")) frame.name = value.toString();
    else if (property == QLatin1String("temperature")) frame.temperature = value.toDouble();
    else if (property == QLatin1String("sensor")) frame.sensor = value.toString();
    else if (property == QLatin1String("pump")) frame.pump = value.toString();
    else if (property == QLatin1String("transition")) frame.transition = value.toString();
    else if (property == QLatin1String("pressure")) frame.pressure = value.toDouble();
    else if (property == QLatin1String("flow")) frame.flow = value.toDouble();
    else if (property == QLatin1String("seconds")) frame.seconds = value.toDouble();
    else if (property == QLatin1String("volume")) frame.volume = value.toDouble();
    else if (property == QLatin1String("exitIf")) frame.exitIf = value.toBool();
    else if (property == QLatin1String("exitType")) frame.exitType = value.toString();
    else if (property == QLatin1String("exitPressureOver")) frame.exitPressureOver = value.toDouble();
    else if (property == QLatin1String("exitPressureUnder")) frame.exitPressureUnder = value.toDouble();
    else if (property == QLatin1String("exitFlowOver")) frame.exitFlowOver = value.toDouble();
    else if (property == QLatin1String("exitFlowUnder")) frame.exitFlowUnder = value.toDouble();
    else if (property == QLatin1String("exitWeight")) frame.exitWeight = value.toDouble();
    else if (property == QLatin1String("maxFlowOrPressure")) frame.maxFlowOrPressure = value.toDouble();
    else if (property == QLatin1String("maxFlowOrPressureRange")) frame.maxFlowOrPressureRange = value.toDouble();
    else if (property == QLatin1String("popup")) frame.popup = value.toString();
    else return false;
    profile.setStepAt(index, frame);
    return true;
}

QVariantMap ProfileEditModel::frameAt(const Profile& profile, int index)
{
    if (index < 0 || index >= profile.steps().size()) return {};
    const ProfileFrame& frame = profile.steps().at(index);
    return {{QStringLiteral("name"), frame.name},
            {QStringLiteral("temperature"), frame.temperature},
            {QStringLiteral("sensor"), frame.sensor},
            {QStringLiteral("pump"), frame.pump},
            {QStringLiteral("transition"), frame.transition},
            {QStringLiteral("pressure"), frame.pressure},
            {QStringLiteral("flow"), frame.flow},
            {QStringLiteral("seconds"), frame.seconds},
            {QStringLiteral("volume"), frame.volume},
            {QStringLiteral("exitIf"), frame.exitIf},
            {QStringLiteral("exitType"), frame.exitType},
            {QStringLiteral("exitPressureOver"), frame.exitPressureOver},
            {QStringLiteral("exitPressureUnder"), frame.exitPressureUnder},
            {QStringLiteral("exitFlowOver"), frame.exitFlowOver},
            {QStringLiteral("exitFlowUnder"), frame.exitFlowUnder},
            {QStringLiteral("exitWeight"), frame.exitWeight},
            {QStringLiteral("maxFlowOrPressure"), frame.maxFlowOrPressure},
            {QStringLiteral("maxFlowOrPressureRange"), frame.maxFlowOrPressureRange},
            {QStringLiteral("popup"), frame.popup}};
}
