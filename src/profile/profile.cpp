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

QJsonDocument Profile::toJson() const {
    QJsonObject obj;
    obj["title"] = m_title;
    obj["author"] = m_author;
    obj["notes"] = m_notes;
    obj["beverage_type"] = m_beverageType;
    obj["profile_type"] = m_profileType;
    obj["target_weight"] = m_targetWeight;
    obj["target_volume"] = m_targetVolume;
    obj["espresso_temperature"] = m_espressoTemperature;
    obj["maximum_pressure"] = m_maximumPressure;
    obj["maximum_flow"] = m_maximumFlow;
    obj["minimum_pressure"] = m_minimumPressure;
    obj["preinfuse_frame_count"] = m_preinfuseFrameCount;
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
    profile.m_notes = obj["notes"].toString();
    profile.m_beverageType = obj["beverage_type"].toString("espresso");
    profile.m_profileType = obj["profile_type"].toString("settings_2c");
    profile.m_targetWeight = obj["target_weight"].toDouble(36.0);
    profile.m_targetVolume = obj["target_volume"].toDouble(36.0);
    profile.m_espressoTemperature = obj["espresso_temperature"].toDouble(93.0);
    profile.m_maximumPressure = obj["maximum_pressure"].toDouble(12.0);
    profile.m_maximumFlow = obj["maximum_flow"].toDouble(6.0);
    profile.m_minimumPressure = obj["minimum_pressure"].toDouble(0.0);
    profile.m_preinfuseFrameCount = obj["preinfuse_frame_count"].toInt(0);

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

    return fromJson(doc);
}

bool Profile::saveToFile(const QString& filePath) const {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    file.write(toJson().toJson(QJsonDocument::Indented));
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
    // Parse de1app .tcl profile files
    // Format: Tcl script with "array set" commands setting profile variables

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Failed to open Tcl profile:" << filePath;
        return Profile();
    }

    Profile profile;
    QString content = QTextStream(&file).readAll();

    // Helper to extract Tcl variable value
    auto extractValue = [&content](const QString& varName) -> QString {
        // Match patterns like: profile_title {My Profile} or profile_title "My Profile"
        // Pattern: varName + whitespace + {content} OR varName + whitespace + "content"
        QRegularExpression re(varName + "\\s+\\{([^}]*)\\}|" + varName + "\\s+\"([^\"]*)\"");
        QRegularExpressionMatch match = re.match(content);
        if (match.hasMatch()) {
            return match.captured(1).isEmpty() ? match.captured(2) : match.captured(1);
        }
        // Also try simple word value: profile_title MyProfile
        QRegularExpression reSimple(varName + "\\s+(\\S+)");
        match = reSimple.match(content);
        return match.hasMatch() ? match.captured(1) : QString();
    };

    // Extract metadata
    profile.m_title = extractValue("profile_title");
    profile.m_author = extractValue("author");
    profile.m_notes = extractValue("profile_notes");
    profile.m_profileType = extractValue("settings_profile_type");

    // Extract numeric values
    QString val = extractValue("final_desired_shot_weight");
    if (!val.isEmpty()) profile.m_targetWeight = val.toDouble();

    val = extractValue("final_desired_shot_volume");
    if (!val.isEmpty()) profile.m_targetVolume = val.toDouble();

    val = extractValue("espresso_temperature");
    if (!val.isEmpty()) profile.m_espressoTemperature = val.toDouble();

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

    // Try to convert to recipe mode if the frame pattern matches
    if (RecipeAnalyzer::canConvertToRecipe(profile)) {
        RecipeAnalyzer::convertToRecipeMode(profile);
        qDebug() << "  → Converted to recipe mode";
    }

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
    if (m_recipeParams.infuseTime > 0 || m_recipeParams.infuseByWeight) {
        preinfuseCount++;
    }
    m_preinfuseFrameCount = preinfuseCount;
}
