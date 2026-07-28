#pragma once

#include "refractometerdevice.h"

#include <QObject>
#include <QPointer>
#include <QString>

// A stable QML identity for the connected refractometer, if there is one.
//
// Same shape and the same reason as ScaleDeviceProxy (see scaledeviceproxy.h): the name was a
// context property because main() re-points it — at a DiFluid R1 or R2 driver when one connects,
// and back at nullptr when it goes away, five sites in all — and a QML_SINGLETON is created once
// per type and cannot be re-pointed.
//
// The difference from the scale is that this one is genuinely absent most of the time. A
// refractometer is only connected while the post-shot review page has it open, so "no device" is
// the normal state rather than a teardown edge. Every getter therefore returns a defined value
// for that case, and every method is a no-op — the disconnected reading is 0 TDS, not measuring,
// no name.
//
// FOR QML: the proxy is never null. `Refractometer && Refractometer.measuring` still reads
// correctly, but `typeof Refractometer !== "undefined"` and `Refractometer !== null` are now
// always true and guard nothing. Test the state: `Refractometer.connected`.
class RefractometerProxy : public QObject {
    Q_OBJECT

    // Mirrors RefractometerDevice's property set. `supportsAutoTest` is CONSTANT on the device
    // and is NOT constant here — it is a fact about which device is attached, and an R1 and an R2
    // answer differently.
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(double tds READ tds NOTIFY tdsChanged)
    Q_PROPERTY(double temperature READ temperature NOTIFY temperatureChanged)
    Q_PROPERTY(bool measuring READ measuring NOTIFY measuringChanged)
    Q_PROPERTY(QString name READ name NOTIFY nameChanged)
    Q_PROPERTY(bool autoTest READ autoTest NOTIFY autoTestChanged)
    Q_PROPERTY(bool supportsAutoTest READ supportsAutoTest NOTIFY targetChanged)

public:
    explicit RefractometerProxy(QObject* parent = nullptr);

    void setTarget(RefractometerDevice* target);
    RefractometerDevice* target() const { return m_target; }

    bool connected() const { return m_target && m_target->isConnected(); }
    double tds() const { return m_target ? m_target->tds() : 0.0; }
    double temperature() const { return m_target ? m_target->temperature() : 0.0; }
    bool measuring() const { return m_target && m_target->isMeasuring(); }
    QString name() const { return m_target ? m_target->name() : QString(); }
    bool autoTest() const { return m_target && m_target->autoTest(); }
    bool supportsAutoTest() const { return m_target && m_target->supportsAutoTest(); }

public slots:
    // The whole Q_INVOKABLE surface of RefractometerDevice, so migrating from a context property
    // does not quietly remove anything QML could previously call.
    void setAutoTest(bool enabled);
    void setDeviceTestCount(int count);
    void disconnectFromDevice();
    void requestMeasurement();
    void requestAveragedMeasurement(int testCount);

signals:
    void connectedChanged();
    void tdsChanged(double tds);
    void temperatureChanged(double temperature);
    void measuringChanged();
    void nameChanged();
    void autoTestChanged();
    void measurementComplete();
    void averageProgress(int completed, int total);
    void errorOccurred(const QString& error);
    void logMessage(const QString& message);

    void targetChanged();

private:
    QPointer<RefractometerDevice> m_target;
};
