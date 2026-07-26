#pragma once

#include <QBluetoothDeviceInfo>
#include <QObject>
#include <QString>

/**
 * Abstract base for refractometer drivers.
 *
 * Defines the QObject surface that BLEManager, MainController, and QML
 * consume so a single `Refractometer` context property can carry any model.
 *
 * The Q_PROPERTYs and signals live on the base — concrete drivers MUST NOT
 * redeclare them. Redeclaring with `NOTIFY` would shadow the base signal in
 * Qt's meta-system; QML bindings that resolve through the base context
 * property would silently miss the subclass emission.
 */
class RefractometerDevice : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(double tds READ tds NOTIFY tdsChanged)
    Q_PROPERTY(double temperature READ temperature NOTIFY temperatureChanged)
    Q_PROPERTY(bool measuring READ isMeasuring NOTIFY measuringChanged)
    Q_PROPERTY(QString name READ name NOTIFY nameChanged)
    // Whether the device starts a measurement by itself. Reflects the DEVICE's own
    // stored setting, read back from it — not a Decenza preference. Devices with no
    // such feature report false and ignore attempts to set it.
    Q_PROPERTY(bool autoTest READ autoTest NOTIFY autoTestChanged)

public:
    using QObject::QObject;
    ~RefractometerDevice() override = default;

    // Physically implausible reading ceiling shared by all refractometer
    // drivers. Real coffee never approaches this; a value above it is a
    // hardware fault (R2 has an explicit out-of-range sentinel that lands
    // in the TDS field, R1 doesn't but the gate still catches bad decrypts).
    static constexpr double MAX_PLAUSIBLE_TDS = 35.0;

    virtual bool isConnected() const = 0;
    virtual double tds() const = 0;
    virtual double temperature() const = 0;
    virtual bool isMeasuring() const = 0;
    virtual QString name() const = 0;

    // Auto Test: the device starts a measurement itself when it detects the sample
    // being loaded. Off by default on devices that support it; a device that does not
    // reports false and silently ignores setAutoTest(). The setting lives on the
    // device and persists there, so enabling it is a one-time action, not a Decenza
    // preference we have to store and re-apply.
    virtual bool autoTest() const { return false; }
    Q_INVOKABLE virtual void setAutoTest(bool enabled) { Q_UNUSED(enabled); }
    // Whether this device supports Auto Test at all — drives whether UI offers it.
    Q_INVOKABLE virtual bool supportsAutoTest() const { return false; }
    // Whether the device can average in firmware. The base falls back to a single
    // measurement, which is a safe answer but not an honest label — UI offering an
    // "average" that silently takes one reading would be a lie, so it asks first.
    Q_INVOKABLE virtual bool supportsAveraging() const { return false; }

    // How many tests a DEVICE-INITIATED measurement takes. Distinct from
    // requestAveragedMeasurement(), which averages a run we asked for: this governs
    // runs the device starts on its own — the physical button and Auto Test — and so
    // is the only way to make those averaged. Devices without the notion ignore it.
    Q_INVOKABLE virtual void setDeviceTestCount(int count) { Q_UNUSED(count); }

    virtual void connectToDevice(const QBluetoothDeviceInfo& device) = 0;
    Q_INVOKABLE virtual void disconnectFromDevice() = 0;
    Q_INVOKABLE virtual void requestMeasurement() = 0;

    // Averaged measurement over `testCount` tests, where the device supports it.
    // Averaging several readings is standard practice for refractometry — a single
    // reading on espresso carries real run-to-run scatter.
    //
    // The base implementation falls back to a single measurement, so a device with no
    // averaging (DiFluidR1) needs no code and a caller always gets a reading rather
    // than silence. Drivers that do support it clamp the count to their own range.
    Q_INVOKABLE virtual void requestAveragedMeasurement(int testCount) {
        Q_UNUSED(testCount);
        requestMeasurement();
    }

signals:
    void connectedChanged();
    void tdsChanged(double tds);
    void temperatureChanged(double temperature);
    void measuringChanged();
    void nameChanged();
    void autoTestChanged();
    void measurementComplete();
    // Progress through a multi-test averaged run: `completed` of `total` tests done.
    // Lets the UI show that several tests are being taken rather than appearing hung,
    // without putting a half-finished average where a final reading belongs.
    void averageProgress(int completed, int total);
    void errorOccurred(const QString& error);
    void logMessage(const QString& message);
};
