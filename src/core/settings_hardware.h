#pragma once

#include <QObject>
#include "appsettings.h"

// Hardware selection and calibration settings for the DE1 and connected peripherals:
// heater tweaks, hot-water flow rate, steam two-tap stop, and the saved Belka PORTAL
// pairing and display-sync preference.
class SettingsHardware : public QObject {
    Q_OBJECT

    Q_PROPERTY(int heaterIdleTemp READ heaterIdleTemp WRITE setHeaterIdleTemp NOTIFY heaterIdleTempChanged FINAL)
    Q_PROPERTY(int heaterWarmupFlow READ heaterWarmupFlow WRITE setHeaterWarmupFlow NOTIFY heaterWarmupFlowChanged FINAL)
    Q_PROPERTY(int heaterTestFlow READ heaterTestFlow WRITE setHeaterTestFlow NOTIFY heaterTestFlowChanged FINAL)
    Q_PROPERTY(int heaterWarmupTimeout READ heaterWarmupTimeout WRITE setHeaterWarmupTimeout NOTIFY heaterWarmupTimeoutChanged FINAL)
    Q_PROPERTY(int hotWaterFlowRate READ hotWaterFlowRate WRITE setHotWaterFlowRate NOTIFY hotWaterFlowRateChanged FINAL)
    Q_PROPERTY(bool steamTwoTapStop READ steamTwoTapStop WRITE setSteamTwoTapStop NOTIFY steamTwoTapStopChanged FINAL)
    Q_PROPERTY(int fanThreshold READ fanThreshold WRITE setFanThreshold NOTIFY fanThresholdChanged FINAL)

    Q_PROPERTY(QString portalAddress READ portalAddress NOTIFY portalDeviceChanged FINAL)
    Q_PROPERTY(QString portalName READ portalName NOTIFY portalDeviceChanged FINAL)
    Q_PROPERTY(bool portalSyncDisplay READ portalSyncDisplay WRITE setPortalSyncDisplay NOTIFY portalSyncDisplayChanged FINAL)

public:
    QString portalAddress() const;
    QString portalName() const;
    void setPortalDevice(const QString& address, const QString& name);
    bool portalSyncDisplay() const;
    void setPortalSyncDisplay(bool enabled);
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

    int fanThreshold() const;
    void setFanThreshold(int value);

    // --- Connection-priority weak-device classification (D9, #1093/#1176) ---
    // INTERNAL: deliberately NOT a Q_PROPERTY, no NOTIFY, no QML/Settings-UI
    // binding (settings-architecture rule — operator access is MCP-only).
    // Dumb storage: BLEManager owns the epoch-scoped gating + invariant.
    // Persisted so a proven dual-HIGH-incapable radio starts BOTH BLE links
    // at BALANCED across restarts. `detectionEpoch` is the gate (BLEManager
    // re-detects only when it differs from kBleDetectionEpoch — a deliberate
    // global-reset lever, NOT per build); `buildCode` is retained DIAGNOSTIC
    // ONLY ("last classified by build N"). cpEpoch() returns -1 when no epoch
    // key is stored (a legacy pre-epoch record → BLEManager migrates it
    // forward). MCP reset clears the whole latch (incl. the epoch key).
    bool cpLatched() const;
    QString cpTriggerKind() const;
    QString cpSetTimeIso() const;
    int cpBuildCode() const;
    int cpEpoch() const;  // -1 ⇒ legacy record (no detectionEpoch key)
    void setConnectionPriorityLatch(const QString& triggerKind,
                                    const QString& setTimeIso, int buildCode,
                                    int detectionEpoch);
    void clearConnectionPriorityLatch();

    // Backoff policy mode (observe-mode change). Distinct from the latch:
    // deliberately NOT build-scoped — it is an explicit operator choice that
    // must survive app restarts AND build upgrades, so the build-scoped
    // rehydrate/safety-valve logic never reads or rewrites it. Stored as a
    // sibling key under the same group; clearConnectionPriorityLatch() must
    // NOT remove it. Absent/unrecognized ⇒ caller treats as "enforce".
    QString cpMode() const;
    void setCpMode(const QString& mode);

signals:
    void portalDeviceChanged();
    void portalSyncDisplayChanged();
    void heaterIdleTempChanged();
    void heaterWarmupFlowChanged();
    void heaterTestFlowChanged();
    void heaterWarmupTimeoutChanged();
    void hotWaterFlowRateChanged();
    void steamTwoTapStopChanged();
    void fanThresholdChanged();

private:
    mutable AppSettings m_settings;
};
