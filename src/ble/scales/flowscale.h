#pragma once

#include "../scaledevice.h"

class Settings;

/**
 * FlowScale - A virtual scale that estimates cup weight from DE1 flow data.
 *
 * Used as a fallback when no physical BLE scale is connected.
 * Integrates flow rate over time, then subtracts puck absorption to
 * estimate the weight of espresso in the cup.
 *
 * Model: cup_weight = raw_flow_integral - (dose × 0.95 + 6.0)
 *
 * The two absorption components are:
 *   - 6.0g fixed: water retained by group head, shower screen, basket
 *   - 0.95 × dose: water absorbed by the coffee puck itself
 *
 * Can be disabled via Settings::useFlowScale().
 */
class FlowScale : public ScaleDevice {
    Q_OBJECT

    Q_PROPERTY(double rawFlowIntegral READ rawFlowIntegral NOTIFY rawFlowIntegralChanged)

public:
    explicit FlowScale(QObject* parent = nullptr);

    // ScaleDevice interface
    void connectToDevice(const QBluetoothDeviceInfo& device) override;
    QString name() const override { return "Flow Scale"; }
    QString type() const override { return "flow"; }
    bool isFlowScale() const override { return true; }

    // Settings injection (for useFlowScale toggle)
    void setSettings(Settings* settings);

    // Set dose weight for puck absorption compensation (call before each shot)
    void setDose(double doseGrams);

    // Raw flow integral (for diagnostics)
    double rawFlowIntegral() const { return m_rawFlowIntegral; }
    Q_INVOKABLE void resetRawFlowIntegral() { m_rawFlowIntegral = 0.0; emit rawFlowIntegralChanged(); }
    Q_INVOKABLE void resetWeight();

public slots:
    void tare() override;

    // Override from ScaleDevice - receives flow samples from DE1
    void addFlowSample(double flowRate, double deltaTime) override;

    // Reset for new shot
    void reset();

    // For simulator integration - set weight directly
    void setSimulatedWeight(double weight);

signals:
    void rawFlowIntegralChanged();

private:
    void updateEstimatedWeight();  // Recalculate cup weight from raw integral

    double m_accumulatedWeight = 0.0;
    double m_rawFlowIntegral = 0.0;  // Raw flow integral for diagnostics
    double m_dose = 0.0;             // Dose weight in grams (for puck absorption)
    Settings* m_settings = nullptr;
};
