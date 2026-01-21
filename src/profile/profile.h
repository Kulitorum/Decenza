#pragma once

#include <QString>
#include <QList>
#include <QJsonDocument>
#include <QByteArray>
#include "profileframe.h"
#include "recipeparams.h"

/**
 * Profile represents a complete espresso profile for the DE1.
 *
 * Supports two execution modes:
 * 1. Frame-Based Mode: Profile is uploaded to machine, which executes it autonomously
 * 2. Direct Setpoint Control: App sends live setpoints frame-by-frame during extraction
 *
 * Profile File Formats:
 * - .json: Native format (our own JSON schema)
 * - .tcl: de1app format (Tcl list syntax, importable)
 */
class Profile {
public:
    // Execution modes
    enum class Mode {
        FrameBased,     // Upload to machine, execute autonomously
        DirectControl   // App sends live setpoints during shot
    };

    // Stop-at modes (what triggers end of shot)
    enum class StopAtType {
        Weight,         // Stop when scale reaches target weight (brown curve)
        Volume          // Stop when flow meter reaches target volume (blue curve)
    };

    Profile() = default;

    // === Metadata ===
    QString title() const { return m_title; }
    void setTitle(const QString& title) { m_title = title; }

    QString author() const { return m_author; }
    void setAuthor(const QString& author) { m_author = author; }

    QString profileNotes() const { return m_profileNotes; }
    void setProfileNotes(const QString& notes) { m_profileNotes = notes; }

    QString beverageType() const { return m_beverageType; }
    void setBeverageType(const QString& type) { m_beverageType = type; }

    // Profile type for compatibility with de1app settings
    // "settings_2a" = simple pressure, "settings_2b" = simple flow,
    // "settings_2c" = advanced (our default), "settings_2c2" = advanced with limiter
    QString profileType() const { return m_profileType; }
    void setProfileType(const QString& type) { m_profileType = type; }

    // === Target Values ===
    double targetWeight() const { return m_targetWeight; }
    void setTargetWeight(double weight) { m_targetWeight = weight; }

    double targetVolume() const { return m_targetVolume; }
    void setTargetVolume(double volume) { m_targetVolume = volume; }

    StopAtType stopAtType() const { return m_stopAtType; }
    void setStopAtType(StopAtType type) { m_stopAtType = type; }

    // === Temperature Settings ===
    // Primary espresso temperature (often mirrors first frame temp)
    double espressoTemperature() const { return m_espressoTemperature; }
    void setEspressoTemperature(double temp) { m_espressoTemperature = temp; }

    // Temperature presets for quick adjustment (de1app feature)
    QList<double> temperaturePresets() const { return m_temperaturePresets; }
    void setTemperaturePresets(const QList<double>& presets) { m_temperaturePresets = presets; }

    // === Flow/Pressure Limits ===
    double maximumPressure() const { return m_maximumPressure; }
    void setMaximumPressure(double pressure) { m_maximumPressure = pressure; }

    double maximumFlow() const { return m_maximumFlow; }
    void setMaximumFlow(double flow) { m_maximumFlow = flow; }

    double minimumPressure() const { return m_minimumPressure; }
    void setMinimumPressure(double pressure) { m_minimumPressure = pressure; }

    // === Steps/Frames ===
    const QList<ProfileFrame>& steps() const { return m_steps; }
    QList<ProfileFrame>& steps() { return m_steps; }
    void setSteps(const QList<ProfileFrame>& steps) { m_steps = steps; }
    void addStep(const ProfileFrame& step) { m_steps.append(step); }
    void insertStep(int index, const ProfileFrame& step) { m_steps.insert(index, step); }
    void removeStep(int index) { m_steps.removeAt(index); }
    void moveStep(int from, int to);
    void setStepAt(int index, const ProfileFrame& step) {
        if (index >= 0 && index < m_steps.size()) m_steps[index] = step;
    }

    int preinfuseFrameCount() const { return m_preinfuseFrameCount; }
    void setPreinfuseFrameCount(int count) { m_preinfuseFrameCount = count; }

    // Max frames the DE1 accepts (hardware limit)
    static constexpr int MAX_FRAMES = 20;

    // === Execution Mode ===
    Mode mode() const { return m_mode; }
    void setMode(Mode mode) { m_mode = mode; }

    // === Recipe Mode ===
    // Recipe mode stores high-level parameters that generate frames automatically
    bool isRecipeMode() const { return m_isRecipeMode; }
    void setRecipeMode(bool enabled) { m_isRecipeMode = enabled; }

    RecipeParams recipeParams() const { return m_recipeParams; }
    void setRecipeParams(const RecipeParams& params) { m_recipeParams = params; }

    // Regenerate frames from stored recipe parameters
    void regenerateFromRecipe();

    // === Serialization ===
    QJsonDocument toJson() const;
    static Profile fromJson(const QJsonDocument& doc);

    // === File I/O ===
    static Profile loadFromFile(const QString& filePath);
    static Profile loadFromJsonString(const QString& jsonContent);
    bool saveToFile(const QString& filePath) const;
    QString toJsonString() const;

    // Import from de1app .tcl file
    static Profile loadFromTclFile(const QString& filePath);

    // Import from de1app TCL format string (used by Visualizer API)
    static Profile loadFromTclString(const QString& tclContent);

    // Import from DE1 app / Visualizer JSON format (different field names than native format)
    static Profile loadFromDE1AppJson(const QString& jsonContent);

    // === BLE Data Generation ===
    // Generate profile header for upload (5 bytes)
    QByteArray toHeaderBytes() const;

    // Generate all frame data for upload (8 bytes each + extension frames + tail)
    QList<QByteArray> toFrameBytes() const;

    // Generate a single frame for direct control mode
    QByteArray toDirectControlFrame(int frameIndex, const ProfileFrame& frame) const;

    // === Validation ===
    bool isValid() const;
    QStringList validationErrors() const;

private:
    // Metadata
    QString m_title = "Default";
    QString m_author;
    QString m_profileNotes;
    QString m_beverageType = "espresso";
    QString m_profileType = "settings_2c";  // Advanced by default

    // Targets
    double m_targetWeight = 36.0;
    double m_targetVolume = 36.0;
    StopAtType m_stopAtType = StopAtType::Weight;  // Default to weight-based stop

    // Temperature
    double m_espressoTemperature = 93.0;
    QList<double> m_temperaturePresets = {88.0, 90.0, 93.0, 96.0};

    // Limits
    double m_maximumPressure = 12.0;
    double m_maximumFlow = 6.0;
    double m_minimumPressure = 0.0;

    // Frames
    int m_preinfuseFrameCount = 0;
    QList<ProfileFrame> m_steps;

    // Mode
    Mode m_mode = Mode::FrameBased;

    // Recipe mode
    bool m_isRecipeMode = false;
    RecipeParams m_recipeParams;
};

Q_DECLARE_METATYPE(Profile)
