#pragma once

#include "../scaledevice.h"

class Settings;

/**
 * FlowScale - A virtual scale that estimates weight from DE1 flow data.
 *
 * This is used as a fallback when no physical scale is connected.
 * It integrates flow rate over time to estimate weight (assuming ~1g/mL).
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

    // Settings injection for calibration factor
    void setSettings(Settings* settings);

    // Raw flow integral (for calibration)
    double rawFlowIntegral() const { return m_rawFlowIntegral; }
    Q_INVOKABLE void resetRawFlowIntegral() { m_rawFlowIntegral = 0.0; emit rawFlowIntegralChanged(); }
    Q_INVOKABLE void resetWeight();  // Reset accumulated weight (for verification)

public slots:
    void tare() override;

    // Override from ScaleDevice - receives flow samples from DE1
    void addFlowSample(double flowRate, double deltaTime) override;

    // Reset for new shot
    void reset();

signals:
    void rawFlowIntegralChanged();

private:
    double m_accumulatedWeight = 0.0;
    double m_rawFlowIntegral = 0.0;  // Uncalibrated flow integral for calibration
    Settings* m_settings = nullptr;
};
