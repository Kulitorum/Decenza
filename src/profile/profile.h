#pragma once

#include <QString>
#include <QList>
#include <QJsonDocument>
#include <QByteArray>
#include "profileframe.h"

class Profile {
public:
    Profile() = default;

    // Metadata
    QString title() const { return m_title; }
    void setTitle(const QString& title) { m_title = title; }

    QString author() const { return m_author; }
    void setAuthor(const QString& author) { m_author = author; }

    QString notes() const { return m_notes; }
    void setNotes(const QString& notes) { m_notes = notes; }

    QString beverageType() const { return m_beverageType; }
    void setBeverageType(const QString& type) { m_beverageType = type; }

    // Target values
    double targetWeight() const { return m_targetWeight; }
    void setTargetWeight(double weight) { m_targetWeight = weight; }

    double targetVolume() const { return m_targetVolume; }
    void setTargetVolume(double volume) { m_targetVolume = volume; }

    // Steps/Frames
    const QList<ProfileFrame>& steps() const { return m_steps; }
    void setSteps(const QList<ProfileFrame>& steps) { m_steps = steps; }
    void addStep(const ProfileFrame& step) { m_steps.append(step); }

    int preinfuseFrameCount() const { return m_preinfuseFrameCount; }
    void setPreinfuseFrameCount(int count) { m_preinfuseFrameCount = count; }

    // Serialization
    QJsonDocument toJson() const;
    static Profile fromJson(const QJsonDocument& doc);

    // Load from file
    static Profile loadFromFile(const QString& filePath);
    bool saveToFile(const QString& filePath) const;

    // Generate BLE upload data
    QByteArray toHeaderBytes() const;
    QList<QByteArray> toFrameBytes() const;

private:
    QString m_title = "Default";
    QString m_author;
    QString m_notes;
    QString m_beverageType = "espresso";
    double m_targetWeight = 36.0;
    double m_targetVolume = 36.0;
    int m_preinfuseFrameCount = 0;
    QList<ProfileFrame> m_steps;
};

Q_DECLARE_METATYPE(Profile)
