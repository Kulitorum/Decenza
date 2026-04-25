#pragma once

#include <QObject>
#include <QSettings>

// Hardware calibration settings sent to the DE1 firmware:
// heater tweaks, hot-water flow rate, steam two-tap stop.
class SettingsHardware : public QObject {
    Q_OBJECT

    Q_PROPERTY(int heaterIdleTemp READ heaterIdleTemp WRITE setHeaterIdleTemp NOTIFY heaterIdleTempChanged)
    Q_PROPERTY(int heaterWarmupFlow READ heaterWarmupFlow WRITE setHeaterWarmupFlow NOTIFY heaterWarmupFlowChanged)
    Q_PROPERTY(int heaterTestFlow READ heaterTestFlow WRITE setHeaterTestFlow NOTIFY heaterTestFlowChanged)
    Q_PROPERTY(int heaterWarmupTimeout READ heaterWarmupTimeout WRITE setHeaterWarmupTimeout NOTIFY heaterWarmupTimeoutChanged)
    Q_PROPERTY(int hotWaterFlowRate READ hotWaterFlowRate WRITE setHotWaterFlowRate NOTIFY hotWaterFlowRateChanged)
    Q_PROPERTY(bool steamTwoTapStop READ steamTwoTapStop WRITE setSteamTwoTapStop NOTIFY steamTwoTapStopChanged)

public:
    explicit SettingsHardware(QObject* parent = nullptr);

    int heaterIdleTemp() const;
    void setHeaterIdleTemp(int value);

    int heaterWarmupFlow() const;
    void setHeaterWarmupFlow(int value);

    int heaterTestFlow() const;
    void setHeaterTestFlow(int value);

    int heaterWarmupTimeout() const;
    void setHeaterWarmupTimeout(int value);

    int hotWaterFlowRate() const;
    void setHotWaterFlowRate(int value);

    bool steamTwoTapStop() const;
    void setSteamTwoTapStop(bool value);

signals:
    void heaterIdleTempChanged();
    void heaterWarmupFlowChanged();
    void heaterTestFlowChanged();
    void heaterWarmupTimeoutChanged();
    void hotWaterFlowRateChanged();
    void steamTwoTapStopChanged();

private:
    mutable QSettings m_settings;
};
