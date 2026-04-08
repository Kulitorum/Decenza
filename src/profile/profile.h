#pragma once

#include <QString>
#include <QList>
#include <QJsonDocument>
#include <QByteArray>
#include <QDebug>
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
 * - .json: Unified format (compatible with de1app v2)
 * - .tcl: de1app format (Tcl list syntax, importable)
 */
class Profile {
public:
    // Execution modes
    enum class Mode {
        FrameBased,     // Upload to machine, execute autonomously
        DirectControl   // App sends live setpoints during shot
    };

    Profile() = default;

    // === Metadata ===
    QString title() const { return m_title; }
    void setTitle(const QString& title) {
        // Strip leading "*" (de1app modified indicator)
        m_title = title.startsWith(QLatin1Char('*')) ? title.mid(1).trimmed() : title;
    }

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

    // === Temperature Settings ===
    // Primary espresso temperature (often mirrors first frame temp)
    double espressoTemperature() const { return m_espressoTemperature; }
    void setEspressoTemperature(double temp) { m_espressoTemperature = temp; }

    // Temperature presets for quick adjustment (de1app feature)
    QList<double> temperaturePresets() const { return m_temperaturePresets; }
    void setTemperaturePresets(const QList<double>& presets) { m_temperaturePresets = presets; }

    // === Recommended Dose ===
    bool hasRecommendedDose() const { return m_hasRecommendedDose; }
    void setHasRecommendedDose(bool enabled) { m_hasRecommendedDose = enabled; }

    double recommendedDose() const { return m_recommendedDose; }
    void setRecommendedDose(double dose) { m_recommendedDose = dose; }

    // === Flow/Pressure Limits ===
    double maximumPressure() const { return m_maximumPressure; }
    void setMaximumPressure(double pressure) { m_maximumPressure = pressure; }

    double maximumFlow() const { return m_maximumFlow; }
    void setMaximumFlow(double flow) { m_maximumFlow = flow; }

    double minimumPressure() const { return m_minimumPressure; }
    void setMinimumPressure(double pressure) { m_minimumPressure = pressure; }

    // === Simple Profile Parameters (settings_2a/2b) ===
    // Used by Visualizer to reconstruct simple profiles
    double preinfusionTime() const { return m_preinfusionTime; }
    void setPreinfusionTime(double t) { m_preinfusionTime = t; }
    double preinfusionFlowRate() const { return m_preinfusionFlowRate; }
    void setPreinfusionFlowRate(double f) { m_preinfusionFlowRate = f; }
    double preinfusionStopPressure() const { return m_preinfusionStopPressure; }
    void setPreinfusionStopPressure(double p) { m_preinfusionStopPressure = p; }
    double espressoPressure() const { return m_espressoPressure; }
    void setEspressoPressure(double p) { m_espressoPressure = p; }
    double espressoHoldTime() const { return m_espressoHoldTime; }
    void setEspressoHoldTime(double t) { m_espressoHoldTime = t; }
    double espressoDeclineTime() const { return m_espressoDeclineTime; }
    void setEspressoDeclineTime(double t) { m_espressoDeclineTime = t; }
    double pressureEnd() const { return m_pressureEnd; }
    void setPressureEnd(double p) { m_pressureEnd = p; }
    double flowProfileHold() const { return m_flowProfileHold; }
    void setFlowProfileHold(double f) { m_flowProfileHold = f; }
    double flowProfileHoldTime() const { return m_flowProfileHoldTime; }
    void setFlowProfileHoldTime(double t) { m_flowProfileHoldTime = t; }
    double flowProfileDecline() const { return m_flowProfileDecline; }
    void setFlowProfileDecline(double f) { m_flowProfileDecline = f; }
    double flowProfileDeclineTime() const { return m_flowProfileDeclineTime; }
    void setFlowProfileDeclineTime(double t) { m_flowProfileDeclineTime = t; }
    double maximumFlowRangeDefault() const { return m_maximumFlowRangeDefault; }
    void setMaximumFlowRangeDefault(double r) { m_maximumFlowRangeDefault = r; }
    double maximumPressureRangeDefault() const { return m_maximumPressureRangeDefault; }
    void setMaximumPressureRangeDefault(double r) { m_maximumPressureRangeDefault = r; }

    bool tempStepsEnabled() const { return m_tempStepsEnabled; }
    void setTempStepsEnabled(bool e) { m_tempStepsEnabled = e; }

    // === Advanced Limits (de1app settings_2c2) ===
    double tankDesiredWaterTemperature() const { return m_tankDesiredWaterTemperature; }
    void setTankDesiredWaterTemperature(double temp) { m_tankDesiredWaterTemperature = temp; }

    double maximumFlowRangeAdvanced() const { return m_maximumFlowRangeAdvanced; }
    void setMaximumFlowRangeAdvanced(double range) { m_maximumFlowRangeAdvanced = range; }

    double maximumPressureRangeAdvanced() const { return m_maximumPressureRangeAdvanced; }
    void setMaximumPressureRangeAdvanced(double range) { m_maximumPressureRangeAdvanced = range; }

    // === Steps/Frames ===
    const QList<ProfileFrame>& steps() const { return m_steps; }
    void setSteps(const QList<ProfileFrame>& steps) {
        if (steps.size() > MAX_FRAMES) {
            qWarning() << "Profile::setSteps: truncating" << steps.size() << "frames to MAX_FRAMES" << MAX_FRAMES;
            m_steps = steps.mid(0, MAX_FRAMES);
        } else {
            m_steps = steps;
        }
    }
    bool addStep(const ProfileFrame& step) {
        if (m_steps.size() >= MAX_FRAMES) {
            qWarning() << "Profile::addStep: already at MAX_FRAMES" << MAX_FRAMES;
            return false;
        }
        m_steps.append(step);
        return true;
    }
    bool insertStep(int index, const ProfileFrame& step) {
        if (m_steps.size() >= MAX_FRAMES) {
            qWarning() << "Profile::insertStep: already at MAX_FRAMES" << MAX_FRAMES;
            return false;
        }
        if (index < 0 || index > m_steps.size()) {
            qWarning() << "Profile::insertStep: index" << index << "out of range [0," << m_steps.size() << "]";
            return false;
        }
        m_steps.insert(index, step);
        return true;
    }
    bool removeStep(int index) {
        if (index < 0 || index >= m_steps.size()) {
            qWarning() << "Profile::removeStep: index" << index << "out of range [0," << m_steps.size() << ")";
            return false;
        }
        m_steps.removeAt(index);
        return true;
    }
    void moveStep(int from, int to);
    void setStepAt(int index, const ProfileFrame& step) {
        if (index < 0 || index >= m_steps.size()) {
            qWarning() << "Profile::setStepAt: index" << index << "out of range [0," << m_steps.size() << ")";
            return;
        }
        m_steps[index] = step;
    }

    int preinfuseFrameCount() const { return m_preinfuseFrameCount; }
    void setPreinfuseFrameCount(int count) { m_preinfuseFrameCount = count; }

    // Max frames the DE1 accepts (hardware limit)
    static constexpr int MAX_FRAMES = 20;

    // === Execution Mode ===
    Mode mode() const { return m_mode; }
    void setMode(Mode mode) { m_mode = mode; }

    // === Editor Type ===
    // Derived from title + profileType at runtime — never stored in JSON.
    // Returns: "dflow", "aflow", "pressure", "flow", "advanced"
    QString editorType() const;

    // === Recipe Parameters ===
    RecipeParams recipeParams() const { return m_recipeParams; }
    void setRecipeParams(const RecipeParams& params) { m_recipeParams = params; }

    // Regenerate frames from stored recipe parameters
    void regenerateFromRecipe();

    // Regenerate frames from scalar fields for simple profiles (settings_2a/2b)
    void regenerateSimpleFrames();

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

    // === BLE Data Generation ===
    // Generate profile header for upload (5 bytes)
    QByteArray toHeaderBytes() const;

    // Generate all frame data for upload (8 bytes each + extension frames + tail)
    QList<QByteArray> toFrameBytes() const;

    // Generate a single frame for direct control mode
    QByteArray toDirectControlFrame(int frameIndex, const ProfileFrame& frame) const;

    // === Read-Only Flag ===
    // Matches de1app's read_only field (integer):
    //   0 = not read-only (user-created or edited copy)
    //   1 = read-only (built-in / author-protected)
    //   2 = reset to original requested
    // Built-in profiles are always treated as read-only regardless of this flag
    // (enforced by ProfileManager).
    int readOnly() const { return m_readOnly; }
    void setReadOnly(int readOnly) { m_readOnly = readOnly; }
    bool isReadOnly() const { return m_readOnly == 1; }

    // === AI Knowledge Base ===
    // Persisted in JSON as "knowledge_base_id" — survives Save As and reboots
    QString knowledgeBaseId() const { return m_knowledgeBaseId; }
    void setKnowledgeBaseId(const QString& id) { m_knowledgeBaseId = id; }

    // === AI Description ===
    // Generate a compact text description of the frame sequence for AI analysis
    QString describeFrames() const;
    // Convenience: deserialize JSON then call describeFrames()
    static QString describeFramesFromJson(const QString& json);

    // === Validation ===
    bool isValid() const;
    QStringList validationErrors() const;

    // Count consecutive leading frames with exit conditions (preinfusion frames)
    static int countPreinfuseFrames(const QList<ProfileFrame>& steps);

    // Compare two profiles for functional equality (profile-level fields + all frames).
    // Returns false if either profile has no steps.
    static bool functionallyEqual(const Profile& a, const Profile& b);

private:
    // AI knowledge base ID — persisted in profile JSON, computed at load time if missing
    QString m_knowledgeBaseId;

    // Metadata
    QString m_title = "Default";
    QString m_author;
    QString m_profileNotes;
    QString m_beverageType = "espresso";
    QString m_profileType = "settings_2c";  // Advanced by default

    // Targets
    double m_targetWeight = 36.0;
    double m_targetVolume = 0.0;

    // Temperature
    double m_espressoTemperature = 93.0;
    QList<double> m_temperaturePresets = {88.0, 90.0, 93.0, 96.0};

    // Recommended dose
    bool m_hasRecommendedDose = false;
    double m_recommendedDose = 18.0;

    // Limits
    double m_maximumPressure = 12.0;
    double m_maximumFlow = 6.0;
    double m_minimumPressure = 0.0;

    // Advanced limits (de1app settings_2c2)
    double m_tankDesiredWaterTemperature = 0.0;
    double m_maximumFlowRangeAdvanced = 0.6;
    double m_maximumPressureRangeAdvanced = 0.6;

    // Simple profile parameters (settings_2a/2b scalar params for frame generation)
    // Stored in JSON so frames can be regenerated when loading profiles with empty steps
    double m_preinfusionTime = 5.0;
    double m_preinfusionFlowRate = 4.0;
    double m_preinfusionStopPressure = 4.0;
    double m_espressoPressure = 9.2;        // settings_2a only
    double m_espressoHoldTime = 10.0;
    double m_espressoDeclineTime = 25.0;
    double m_pressureEnd = 4.0;             // settings_2a only
    double m_flowProfileHold = 2.0;         // settings_2b only
    double m_flowProfileHoldTime = 8.0;     // settings_2b only
    double m_flowProfileDecline = 1.2;      // settings_2b only
    double m_flowProfileDeclineTime = 17.0; // settings_2b only
    double m_maximumFlowRangeDefault = 1.0; // settings_2a limiter range
    double m_maximumPressureRangeDefault = 0.9; // settings_2b limiter range
    bool m_tempStepsEnabled = false;

    // Frames
    int m_preinfuseFrameCount = 0;
    QList<ProfileFrame> m_steps;

    // Mode
    Mode m_mode = Mode::FrameBased;

    // Recipe parameters (for D-Flow/A-Flow/Pressure/Flow editors)
    RecipeParams m_recipeParams;

    // Read-only flag (de1app compatibility: 0=editable, 1=read-only, 2=reset)
    int m_readOnly = 0;
};

Q_DECLARE_METATYPE(Profile)
