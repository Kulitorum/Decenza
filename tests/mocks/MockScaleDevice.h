#pragma once

#include "ble/scaledevice.h"

// Minimal concrete ScaleDevice for testing.
// Exposes protected setters as public and implements pure virtuals as no-ops.
class MockScaleDevice : public ScaleDevice {
    Q_OBJECT
public:
    explicit MockScaleDevice(QObject* parent = nullptr) : ScaleDevice(parent) {}

    // Pure virtual implementations (no-ops)
    void connectToDevice(const QBluetoothDeviceInfo&) override {}
    void tare() override {}

    // Configurable behavior
    bool isFlowScale() const override { return m_isFlowScale; }
    QString name() const override { return QStringLiteral("MockScale"); }
    QString type() const override { return QStringLiteral("mock"); }

    // Test helpers — expose protected setters
    void mockSetConnected(bool connected) { setConnected(connected); }
    void mockSetWeight(double weight) { setWeight(weight); }

    // Test configuration
    void setIsFlowScale(bool flow) { m_isFlowScale = flow; }

private:
    bool m_isFlowScale = false;
};
