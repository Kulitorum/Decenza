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
    void tare() override { ++m_tareCount; }
    void stopTimer() override { ++m_stopTimerCount; }

    // Configurable behavior
    bool isFlowScale() const override { return m_isFlowScale; }
    QString name() const override { return QStringLiteral("MockScale"); }
    QString type() const override { return m_type; }

    // Test helpers — expose protected setters
    void mockSetConnected(bool connected) { setConnected(connected); }
    void mockSetWeight(double weight) { setWeight(weight); }
    // Number of tare() commands issued. Callers that gate WHEN a tare may be sent
    // need to assert on "no tare was sent", which no signal can express.
    int tareCount() const { return m_tareCount; }
    void resetTareCount() { m_tareCount = 0; }
    // Number of stopTimer() commands issued — the scale's on-device clock is not
    // otherwise observable from the app side.
    int stopTimerCount() const { return m_stopTimerCount; }

    // Test configuration
    void setIsFlowScale(bool flow) { m_isFlowScale = flow; }
    // Default stays "mock" — deliberately NOT a canonical ScaleTypeIds id, so the
    // existing tests keep exercising the non-canonical path. Set a real id (e.g.
    // "decent") to test callers that distinguish real scales from virtual ones.
    void setType(const QString& type) { m_type = type; }

private:
    bool m_isFlowScale = false;
    int m_tareCount = 0;
    int m_stopTimerCount = 0;
    QString m_type = QStringLiteral("mock");
};
