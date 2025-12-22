#include "profile.h"
#include "../ble/protocol/binarycodec.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>

QJsonDocument Profile::toJson() const {
    QJsonObject obj;
    obj["title"] = m_title;
    obj["author"] = m_author;
    obj["notes"] = m_notes;
    obj["beverage_type"] = m_beverageType;
    obj["target_weight"] = m_targetWeight;
    obj["target_volume"] = m_targetVolume;
    obj["preinfuse_frame_count"] = m_preinfuseFrameCount;

    QJsonArray stepsArray;
    for (const auto& step : m_steps) {
        stepsArray.append(step.toJson());
    }
    obj["steps"] = stepsArray;

    return QJsonDocument(obj);
}

Profile Profile::fromJson(const QJsonDocument& doc) {
    Profile profile;
    QJsonObject obj = doc.object();

    profile.m_title = obj["title"].toString("Default");
    profile.m_author = obj["author"].toString();
    profile.m_notes = obj["notes"].toString();
    profile.m_beverageType = obj["beverage_type"].toString("espresso");
    profile.m_targetWeight = obj["target_weight"].toDouble(36.0);
    profile.m_targetVolume = obj["target_volume"].toDouble(36.0);
    profile.m_preinfuseFrameCount = obj["preinfuse_frame_count"].toInt(0);

    QJsonArray stepsArray = obj["steps"].toArray();
    for (const auto& stepVal : stepsArray) {
        profile.m_steps.append(ProfileFrame::fromJson(stepVal.toObject()));
    }

    return profile;
}

Profile Profile::loadFromFile(const QString& filePath) {
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
