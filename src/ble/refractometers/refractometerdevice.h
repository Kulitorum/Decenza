#pragma once

#include <QBluetoothDeviceInfo>
#include <QObject>
#include <QString>

/**
 * Abstract base for refractometer drivers.
 *
 * Defines the QObject surface that BLEManager, MainController, and QML
 * consume so a single `Refractometer` context property can carry either a
 * DiFluid R2 or a DiFluid R1 (or future models). Implementations own their
 * own transport and protocol handling.
 *
 * The Q_PROPERTYs and signals must live on the base so QML reaches them via
 * a base-typed context property without per-subclass meta wiring.
 */
class RefractometerDevice : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(double tds READ tds NOTIFY tdsChanged)
    Q_PROPERTY(double temperature READ temperature NOTIFY temperatureChanged)
    Q_PROPERTY(bool measuring READ isMeasuring NOTIFY measuringChanged)
    Q_PROPERTY(QString name READ name CONSTANT)

public:
    using QObject::QObject;
    ~RefractometerDevice() override = default;

    virtual bool isConnected() const = 0;
    virtual double tds() const = 0;
    virtual double temperature() const = 0;
    virtual bool isMeasuring() const = 0;
    virtual QString name() const = 0;

    virtual void connectToDevice(const QBluetoothDeviceInfo& device) = 0;
    Q_INVOKABLE virtual void disconnectFromDevice() = 0;
    Q_INVOKABLE virtual void requestMeasurement() = 0;

signals:
    void connectedChanged();
    void tdsChanged(double tds);
    void temperatureChanged(double temperature);
    void measuringChanged();
    void measurementComplete();
    void errorOccurred(const QString& error);
    void logMessage(const QString& message);
};
